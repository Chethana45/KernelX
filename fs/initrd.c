#include <fs/initrd.h>
#include <mm/kheap.h>
#include <kernel/printk.h>

/* Standard POSIX USTAR Header (512 bytes) */
typedef struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag[1];
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
} tar_header_t;

typedef struct initrd_entry {
    char                 name[VFS_NAME_MAX];
    vfs_node_t*          node;
    struct initrd_entry* next;
} initrd_entry_t;

typedef struct initrd_dir {
    initrd_entry_t* children;
    uint32_t        count;
} initrd_dir_t;

typedef struct initrd_file {
    const uint8_t* data;
    uint32_t       size;
} initrd_file_t;

static vfs_ops_t initrd_dir_ops;
static vfs_ops_t initrd_file_ops;

static bool     initrd_mounted = false;
static uint32_t initrd_file_count = 0;
static uint32_t next_initrd_ino = 5000;

static size_t kstrlen(const char* s) {
    size_t len = 0;
    while (s && s[len]) len++;
    return len;
}

static void kstrncpy(char* dest, const char* src, size_t n) {
    size_t i = 0;
    while (src && src[i] && i < n - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static int kstrcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static void kmemcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
}


/* ========================================================================= */
/* Read-Only Operations for Initrd Nodes                                     */
/* ========================================================================= */
static int32_t initrd_file_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (!node || !buffer || !node->internal) return -1;
    initrd_file_t* file = (initrd_file_t*)node->internal;

    if (offset >= file->size) {
        return 0; /* EOF */
    }

    uint32_t read_bytes = size;
    if (read_bytes > file->size - offset) {
        read_bytes = file->size - offset;
    }

    kmemcpy(buffer, file->data + offset, read_bytes);
    return (int32_t)read_bytes;
}

static int32_t initrd_file_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    return -1; /* Read-only filesystem! */
}

static int initrd_file_stat(vfs_node_t* node, vfs_stat_t* stat) {
    if (!node || !stat) return -1;
    initrd_file_t* file = (initrd_file_t*)node->internal;

    stat->ino = node->ino;
    stat->type = node->type;
    stat->size = file ? file->size : node->size;
    stat->permissions = node->permissions;
    stat->flags = node->flags;
    return 0;
}

static int initrd_dir_readdir(vfs_node_t* node, uint32_t index, vfs_dirent_t* dirent) {
    if (!node || !dirent || !node->internal) return -1;
    initrd_dir_t* dir = (initrd_dir_t*)node->internal;

    if (index == 0) {
        kstrncpy(dirent->name, ".", VFS_NAME_MAX);
        dirent->ino = node->ino;
        dirent->type = VFS_TYPE_DIRECTORY;
        dirent->size = 0;
        return 0;
    } else if (index == 1) {
        kstrncpy(dirent->name, "..", VFS_NAME_MAX);
        dirent->ino = node->parent ? node->parent->ino : node->ino;
        dirent->type = VFS_TYPE_DIRECTORY;
        dirent->size = 0;
        return 0;
    }

    uint32_t curr_idx = 2;
    initrd_entry_t* curr = dir->children;
    while (curr != NULL) {
        if (curr_idx == index) {
            kstrncpy(dirent->name, curr->name, VFS_NAME_MAX);
            dirent->ino = curr->node->ino;
            dirent->type = curr->node->type;
            dirent->size = curr->node->size;
            return 0;
        }
        curr_idx++;
        curr = curr->next;
    }

    return -1;
}

static vfs_node_t* initrd_dir_finddir(vfs_node_t* node, const char* name) {
    if (!node || !name || !node->internal) return NULL;
    initrd_dir_t* dir = (initrd_dir_t*)node->internal;

    if (kstrcmp(name, ".") == 0) {
        return node;
    }
    if (kstrcmp(name, "..") == 0) {
        return node->parent ? node->parent : node;
    }

    initrd_entry_t* curr = dir->children;
    while (curr != NULL) {
        if (kstrcmp(curr->name, name) == 0) {
            return curr->node;
        }
        curr = curr->next;
    }

    return NULL;
}

static int initrd_dir_stat(vfs_node_t* node, vfs_stat_t* stat) {
    if (!node || !stat) return -1;
    stat->ino = node->ino;
    stat->type = node->type;
    stat->size = 0;
    stat->permissions = node->permissions;
    stat->flags = node->flags;
    return 0;
}

static vfs_ops_t initrd_dir_ops = {
    .read = NULL,
    .write = NULL,
    .truncate = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = initrd_dir_readdir,
    .finddir = initrd_dir_finddir,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .stat = initrd_dir_stat
};

static vfs_ops_t initrd_file_ops = {
    .read = initrd_file_read,
    .write = initrd_file_write,
    .truncate = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .stat = initrd_file_stat
};

static vfs_node_t* create_initrd_dir_node(const char* name, vfs_node_t* parent) {
    vfs_node_t* node = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!node) return NULL;

    initrd_dir_t* dir = (initrd_dir_t*)kmalloc(sizeof(initrd_dir_t));
    if (!dir) {
        kfree(node);
        return NULL;
    }

    dir->children = NULL;
    dir->count = 0;

    kstrncpy(node->name, name, VFS_NAME_MAX);
    node->type = VFS_TYPE_DIRECTORY;
    node->permissions = VFS_PERM_READ | VFS_PERM_EXEC;
    node->size = 0;
    node->ino = next_initrd_ino++;
    node->flags = VFS_NODE_READONLY;
    node->ops = &initrd_dir_ops;
    node->internal = dir;
    node->parent = parent;
    node->mount_target = NULL;
    node->open_count = 0;
    return node;
}

static vfs_node_t* create_initrd_file_node(const char* name, vfs_node_t* parent, const uint8_t* data, uint32_t size) {
    vfs_node_t* node = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!node) return NULL;

    initrd_file_t* file = (initrd_file_t*)kmalloc(sizeof(initrd_file_t));
    if (!file) {
        kfree(node);
        return NULL;
    }

    file->data = data;
    file->size = size;

    kstrncpy(node->name, name, VFS_NAME_MAX);
    node->type = VFS_TYPE_FILE;
    node->permissions = VFS_PERM_READ;
    node->size = size;
    node->ino = next_initrd_ino++;
    node->flags = VFS_NODE_READONLY;
    node->ops = &initrd_file_ops;
    node->internal = file;
    node->parent = parent;
    node->mount_target = NULL;
    node->open_count = 0;
    return node;
}

static int add_initrd_child(vfs_node_t* parent, vfs_node_t* child) {
    if (!parent || !child || !parent->internal) return -1;
    initrd_dir_t* dir = (initrd_dir_t*)parent->internal;
    if (initrd_dir_finddir(parent, child->name) != NULL) return -1;

    initrd_entry_t* entry = (initrd_entry_t*)kmalloc(sizeof(initrd_entry_t));
    if (!entry) return -1;

    kstrncpy(entry->name, child->name, VFS_NAME_MAX);
    entry->node = child;
    entry->next = dir->children;
    dir->children = entry;
    dir->count++;
    child->parent = parent;
    return 0;
}

static void destroy_initrd_node(vfs_node_t* node) {
    if (!node) return;
    if (node->type & VFS_TYPE_DIRECTORY) {
        initrd_dir_t* dir = (initrd_dir_t*)node->internal;
        if (dir) {
            initrd_entry_t* entry = dir->children;
            while (entry) {
                initrd_entry_t* next = entry->next;
                destroy_initrd_node(entry->node);
                kfree(entry);
                entry = next;
            }
            kfree(dir);
        }
    } else if (node->internal) {
        /* File data belongs to the Multiboot module; only free metadata. */
        kfree(node->internal);
    }
    kfree(node);
}

/* Insert a normalized TAR path, creating missing parent directories. */
static int initrd_insert_path(vfs_node_t* root, const char* path, bool is_dir,
                              const uint8_t* data, uint32_t size) {
    char work[256];
    size_t plen = kstrlen(path);
    if (!path || plen == 0 || plen >= sizeof(work) || path[0] == '/') return -1;
    kstrncpy(work, path, sizeof(work));

    /* Ignore leading ./ components emitted by common tar tools. */
    char* scan = work;
    while (scan[0] == '.' && scan[1] == '/') scan += 2;
    if (scan != work) {
        char normalized[256];
        kstrncpy(normalized, scan, sizeof(normalized));
        kstrncpy(work, normalized, sizeof(work));
    }

    plen = kstrlen(work);
    while (plen > 0 && work[plen - 1] == '/') work[--plen] = '\0';
    if (plen == 0) return -1;

    vfs_node_t* parent = root;
    char* cursor = work;

    while (*cursor) {
        char* component = cursor;
        while (*cursor && *cursor != '/') cursor++;
        bool last = (*cursor == '\0');
        if (!last) *cursor++ = '\0';

        size_t clen = kstrlen(component);
        if (clen == 0 || clen >= VFS_NAME_MAX ||
            (clen == 1 && component[0] == '.') ||
            (clen == 2 && component[0] == '.' && component[1] == '.')) return -1;

        vfs_node_t* existing = initrd_dir_finddir(parent, component);
        if (!last) {
            if (!existing) {
                existing = create_initrd_dir_node(component, parent);
                if (!existing) return -1;
                if (add_initrd_child(parent, existing) != 0) { destroy_initrd_node(existing); return -1; }
                initrd_file_count++;
            }
            if (!(existing->type & VFS_TYPE_DIRECTORY)) return -1;
            parent = existing;
            continue;
        }

        if (existing) {
            /* Duplicate archive entries are ignored; never replace an earlier node. */
            return 0;
        }

        vfs_node_t* node;
        if (is_dir) {
            node = create_initrd_dir_node(component, parent);
        } else {
            node = create_initrd_file_node(component, parent, data, size);
        }
        if (!node) return -1;
        if (add_initrd_child(parent, node) != 0) { destroy_initrd_node(node); return -1; }
        initrd_file_count++;
        return 0;
    }
    return -1;
}

static bool tar_block_is_zero(const uint8_t* block) {
    for (uint32_t i = 0; i < 512; i++) if (block[i] != 0) return false;
    return true;
}

static bool parse_octal_checked(const char* field, size_t len, uint32_t* out) {
    uint32_t value = 0;
    bool saw_digit = false;
    for (size_t i = 0; i < len; i++) {
        char c = field[i];
        if (c >= '0' && c <= '7') {
            saw_digit = true;
            if (value > (0xFFFFFFFFu >> 3)) return false;
            value = (value << 3) | (uint32_t)(c - '0');
        } else if (c == '\0' || c == ' ') {
            if (saw_digit) break;
        } else {
            return false;
        }
    }
    *out = value;
    return true;
}

/* Parse a bounded POSIX ustar archive without trusting header lengths or paths. */
static vfs_node_t* parse_tar_archive(const uint8_t* archive_start, uint32_t archive_size) {
    if (!archive_start || archive_size < 512) return NULL;
    vfs_node_t* root = create_initrd_dir_node("initrd", NULL);
    if (!root) return NULL;

    uint32_t offset = 0;
    while (archive_size - offset >= 512) {
        const uint8_t* block = archive_start + offset;
        if (tar_block_is_zero(block)) break;

        const tar_header_t* header = (const tar_header_t*)block;
        uint32_t file_size = 0;
        if (!parse_octal_checked(header->size, sizeof(header->size), &file_size)) {
            printk("[INITRD] Invalid TAR size field at offset %u\n", offset);
            destroy_initrd_node(root);
            return NULL;
        }

        /* Verify the standard TAR checksum (checksum bytes treated as spaces). */
        uint32_t checksum = 0, expected = 0;
        for (uint32_t i = 0; i < 512; i++) {
            checksum += (i >= 148 && i < 156) ? (uint8_t)' ' : block[i];
        }
        if (!parse_octal_checked(header->chksum, sizeof(header->chksum), &expected) ||
            checksum != expected) {
            printk("[INITRD] TAR checksum mismatch at offset %u\n", offset);
            destroy_initrd_node(root);
            return NULL;
        }

        uint32_t available = archive_size - offset - 512;
        if (file_size > available || file_size > 0xFFFFFE00u) {
            printk("[INITRD] TAR entry exceeds module bounds at offset %u\n", offset);
            destroy_initrd_node(root);
            return NULL;
        }
        uint32_t padded_size = (file_size + 511u) & ~511u;
        if (padded_size > available) {
            printk("[INITRD] Truncated TAR payload at offset %u\n", offset);
            destroy_initrd_node(root);
            return NULL;
        }

        char full_path[256];
        char name[101];
        char prefix[156];
        kstrncpy(name, header->name, sizeof(name));
        kstrncpy(prefix, header->prefix, sizeof(prefix));

        if (prefix[0]) {
            size_t a = kstrlen(prefix), b = kstrlen(name);
            if (a + 1 + b >= sizeof(full_path)) { destroy_initrd_node(root); return NULL; }
            for (size_t i = 0; i < a; i++) full_path[i] = prefix[i];
            full_path[a] = '/';
            for (size_t i = 0; i <= b; i++) full_path[a + 1 + i] = name[i];
        } else {
            kstrncpy(full_path, name, sizeof(full_path));
        }

        char type = header->typeflag[0];
        if (type == '5' || type == '0' || type == '\0') {
            if (initrd_insert_path(root, full_path, type == '5',
                                   block + 512, file_size) != 0) {
                printk("[INITRD] Rejected TAR path: %s\n", full_path);
                destroy_initrd_node(root);
                return NULL;
            }
        }

        offset += 512 + padded_size;
    }
    return root;
}

/* Fallback TAR archive synthesized in memory if bootloader provided no module */
static vfs_node_t* create_fallback_initrd(void) {
    vfs_node_t* root = create_initrd_dir_node("initrd", NULL);
    if (!root) return NULL;

    static const char welcome_data[] = "Welcome to KernelX Educational Operating System!\nPhase 7: Virtual File System & Initial Ramdisk Initialized.\n";
    static const char readme_data[]  = "KernelX Phase 7: Initial ramdisk loaded via boot archive parser.\nRead-only archive protection active.\n";
    static const char syscfg_data[]  = "version=1.0\narch=x86_32\nsubsystems=vfs,ramfs,initrd\n";

    vfs_node_t* f1 = create_initrd_file_node("welcome.txt", root, (const uint8_t*)welcome_data, sizeof(welcome_data) - 1);
    vfs_node_t* f2 = create_initrd_file_node("readme.txt", root, (const uint8_t*)readme_data, sizeof(readme_data) - 1);
    vfs_node_t* f3 = create_initrd_file_node("system.cfg", root, (const uint8_t*)syscfg_data, sizeof(syscfg_data) - 1);

    if (f1) add_initrd_child(root, f1);
    if (f2) add_initrd_child(root, f2);
    if (f3) add_initrd_child(root, f3);

    initrd_file_count = 3;
    return root;
}

int initrd_init(multiboot_info_t* mbd) {
    vfs_node_t* initrd_root = NULL;
    bool module_present = mbd && (mbd->flags & MULTIBOOT_INFO_MODS) &&
                          mbd->mods_count > 0 && mbd->mods_addr != 0;
    initrd_file_count = 0;
    initrd_mounted = false;

    if (module_present) {
        multiboot_module_t* mod = (multiboot_module_t*)mbd->mods_addr;
        if (mod->mod_end <= mod->mod_start) {
            printk("[INITRD] Invalid Multiboot module bounds\n");
            return -1;
        }
        uint32_t mod_len = mod->mod_end - mod->mod_start;
        printk("[INITRD] Found Multiboot module at %p (size: %u bytes)\n",
               (void*)mod->mod_start, mod_len);
        initrd_root = parse_tar_archive((const uint8_t*)mod->mod_start, mod_len);
        if (!initrd_root) {
            printk("[INITRD] Archive rejected; refusing to silently substitute fallback files\n");
            return -1;
        }
    } else {
        printk("[INITRD] No Multiboot module supplied; using built-in fallback archive\n");
        initrd_root = create_fallback_initrd();
    }

    if (!initrd_root) return -1;

    /* Mount under /initrd; do not overwrite an existing mount. */
    if (!vfs_resolve_path("/initrd", "/")) {
        if (vfs_mkdir("/initrd", "/", 0755) != 0) {
            destroy_initrd_node(initrd_root);
            return -1;
        }
    }
    int res = vfs_mount("/initrd", initrd_root);
    if (res == 0) initrd_mounted = true;
    else destroy_initrd_node(initrd_root);
    return res;
}

bool initrd_is_mounted(void) {
    return initrd_mounted;
}

uint32_t initrd_get_file_count(void) {
    return initrd_file_count;
}
