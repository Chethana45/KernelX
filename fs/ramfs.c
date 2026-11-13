#include <fs/ramfs.h>
#include <mm/kheap.h>
#include <kernel/printk.h>

static uint32_t next_inode = 100;

typedef struct ramfs_entry {
    char                name[VFS_NAME_MAX];
    vfs_node_t*         node;
    struct ramfs_entry* next;
} ramfs_entry_t;

typedef struct ramfs_dir {
    ramfs_entry_t* children;
    uint32_t       count;
} ramfs_dir_t;

typedef struct ramfs_file {
    uint8_t* data;
    uint32_t capacity;
    uint32_t size;
} ramfs_file_t;

static vfs_ops_t ramfs_dir_ops;
static vfs_ops_t ramfs_file_ops;

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

static void kmemset(void* dest, uint8_t value, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    for (size_t i = 0; i < n; i++) d[i] = value;
}

static bool ramfs_valid_name(const char* name) {
    if (!name || !name[0] || name[0] == '/') return false;
    size_t n = kstrlen(name);
    if (n >= VFS_NAME_MAX || (n == 1 && name[0] == '.') ||
        (n == 2 && name[0] == '.' && name[1] == '.')) return false;
    for (size_t i = 0; i < n; i++) if (name[i] == '/') return false;
    return true;
}

/* ========================================================================= */
/* RAMFS File Operations                                                     */
/* ========================================================================= */
static int32_t ramfs_file_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (!node || !buffer || !node->internal) return -1;
    ramfs_file_t* file = (ramfs_file_t*)node->internal;

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

static int32_t ramfs_file_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    if (!node || (!buffer && size != 0) || !node->internal) return -1;
    if (size == 0) return 0;
    if (offset > 0xFFFFFFFFu - size) return -1;

    ramfs_file_t* file = (ramfs_file_t*)node->internal;
    uint32_t needed = offset + size;

    if (needed > file->capacity) {
        if (needed > 0xFFFFFF00u) return -1;
        uint32_t new_cap = (needed + 255u) & ~255u;
        uint8_t* new_data = (uint8_t*)kmalloc(new_cap);
        if (!new_data) return -1;

        kmemset(new_data, 0, new_cap);
        if (file->data && file->size) {
            kmemcpy(new_data, file->data, file->size);
            kfree(file->data);
        }
        file->data = new_data;
        file->capacity = new_cap;
    }

    /* Sparse writes must read as zeroes in the unwritten gap. */
    if (offset > file->size) {
        kmemset(file->data + file->size, 0, offset - file->size);
    }

    kmemcpy(file->data + offset, buffer, size);
    if (needed > file->size) {
        file->size = needed;
        node->size = needed;
    }
    return (int32_t)size;
}

static int ramfs_file_truncate(vfs_node_t* node) {
    if (!node || !node->internal) return -1;
    ramfs_file_t* file = (ramfs_file_t*)node->internal;
    if (file->data) kfree(file->data);
    file->data = NULL;
    file->capacity = 0;
    file->size = 0;
    node->size = 0;
    return 0;
}

static int ramfs_file_stat(vfs_node_t* node, vfs_stat_t* stat) {
    if (!node || !stat) return -1;
    ramfs_file_t* file = (ramfs_file_t*)node->internal;

    stat->ino = node->ino;
    stat->type = node->type;
    stat->size = file ? file->size : node->size;
    stat->permissions = node->permissions;
    stat->flags = node->flags;
    return 0;
}

/* ========================================================================= */
/* RAMFS Directory Operations                                                */
/* ========================================================================= */
static int ramfs_dir_readdir(vfs_node_t* node, uint32_t index, vfs_dirent_t* dirent) {
    if (!node || !dirent || !node->internal) return -1;
    ramfs_dir_t* dir = (ramfs_dir_t*)node->internal;

    /* Entry 0 is '.', Entry 1 is '..' */
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
    ramfs_entry_t* curr = dir->children;
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

    return -1; /* End of directory */
}

static vfs_node_t* ramfs_dir_finddir(vfs_node_t* node, const char* name) {
    if (!node || !name || !node->internal) return NULL;
    ramfs_dir_t* dir = (ramfs_dir_t*)node->internal;

    if (kstrcmp(name, ".") == 0) {
        return node;
    }
    if (kstrcmp(name, "..") == 0) {
        return node->parent ? node->parent : node;
    }

    ramfs_entry_t* curr = dir->children;
    while (curr != NULL) {
        if (kstrcmp(curr->name, name) == 0) {
            return curr->node;
        }
        curr = curr->next;
    }

    return NULL;
}

static int ramfs_dir_create(vfs_node_t* parent, const char* name, uint32_t mode) {
    if (!parent || !ramfs_valid_name(name)) return -1;
    ramfs_dir_t* pdir = (ramfs_dir_t*)parent->internal;
    if (!pdir) return -1;

    /* Check if already exists */
    if (ramfs_dir_finddir(parent, name) != NULL) {
        return -1; /* EEXIST */
    }

    /* Allocate child VFS node */
    vfs_node_t* child = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!child) return -1;

    ramfs_file_t* file = (ramfs_file_t*)kmalloc(sizeof(ramfs_file_t));
    if (!file) {
        kfree(child);
        return -1;
    }

    file->data = NULL;
    file->capacity = 0;
    file->size = 0;

    kstrncpy(child->name, name, VFS_NAME_MAX);
    child->type = VFS_TYPE_FILE;
    child->permissions = mode ? mode : (VFS_PERM_READ | VFS_PERM_WRITE);
    child->size = 0;
    child->ino = next_inode++;
    child->flags = 0;
    child->ops = &ramfs_file_ops;
    child->internal = file;
    child->parent = parent;
    child->mount_target = NULL;
    child->open_count = 0;

    /* Add entry to parent directory */
    ramfs_entry_t* entry = (ramfs_entry_t*)kmalloc(sizeof(ramfs_entry_t));
    if (!entry) {
        kfree(file);
        kfree(child);
        return -1;
    }

    kstrncpy(entry->name, name, VFS_NAME_MAX);
    entry->node = child;
    entry->next = pdir->children;
    pdir->children = entry;
    pdir->count++;

    return 0;
}

static int ramfs_dir_mkdir(vfs_node_t* parent, const char* name, uint32_t mode) {
    if (!parent || !ramfs_valid_name(name)) return -1;
    ramfs_dir_t* pdir = (ramfs_dir_t*)parent->internal;
    if (!pdir) return -1;

    if (ramfs_dir_finddir(parent, name) != NULL) {
        return -1;
    }

    vfs_node_t* child = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!child) return -1;

    ramfs_dir_t* cdir = (ramfs_dir_t*)kmalloc(sizeof(ramfs_dir_t));
    if (!cdir) {
        kfree(child);
        return -1;
    }

    cdir->children = NULL;
    cdir->count = 0;

    kstrncpy(child->name, name, VFS_NAME_MAX);
    child->type = VFS_TYPE_DIRECTORY;
    child->permissions = mode ? mode : (VFS_PERM_READ | VFS_PERM_WRITE | VFS_PERM_EXEC);
    child->size = 0;
    child->ino = next_inode++;
    child->flags = 0;
    child->ops = &ramfs_dir_ops;
    child->internal = cdir;
    child->parent = parent;
    child->mount_target = NULL;
    child->open_count = 0;

    ramfs_entry_t* entry = (ramfs_entry_t*)kmalloc(sizeof(ramfs_entry_t));
    if (!entry) {
        kfree(cdir);
        kfree(child);
        return -1;
    }

    kstrncpy(entry->name, name, VFS_NAME_MAX);
    entry->node = child;
    entry->next = pdir->children;
    pdir->children = entry;
    pdir->count++;

    return 0;
}

static int ramfs_dir_unlink(vfs_node_t* parent, const char* name) {
    if (!parent || !ramfs_valid_name(name) || !parent->internal) return -1;
    ramfs_dir_t* pdir = (ramfs_dir_t*)parent->internal;

    ramfs_entry_t* prev = NULL;
    ramfs_entry_t* curr = pdir->children;

    while (curr != NULL) {
        if (kstrcmp(curr->name, name) == 0) {
            if (curr->node->open_count != 0 || curr->node->mount_target != NULL) return -1;
            /* If it's a directory, ensure it is empty */
            if (curr->node->type & VFS_TYPE_DIRECTORY) {
                ramfs_dir_t* d = (ramfs_dir_t*)curr->node->internal;
                if (d && d->children != NULL) {
                    return -1; /* ENOTEMPTY */
                }
                if (d) kfree(d);
            } else if (curr->node->type & VFS_TYPE_FILE) {
                ramfs_file_t* f = (ramfs_file_t*)curr->node->internal;
                if (f) {
                    if (f->data) kfree(f->data);
                    kfree(f);
                }
            }

            kfree(curr->node);

            /* Remove entry from linked list */
            if (prev == NULL) {
                pdir->children = curr->next;
            } else {
                prev->next = curr->next;
            }
            kfree(curr);
            pdir->count--;
            return 0;
        }
        prev = curr;
        curr = curr->next;
    }

    return -1; /* ENOENT */
}

static int ramfs_dir_stat(vfs_node_t* node, vfs_stat_t* stat) {
    if (!node || !stat) return -1;
    stat->ino = node->ino;
    stat->type = node->type;
    stat->size = 0;
    stat->permissions = node->permissions;
    stat->flags = node->flags;
    return 0;
}

static vfs_ops_t ramfs_dir_ops = {
    .read = NULL,
    .write = NULL,
    .truncate = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = ramfs_dir_readdir,
    .finddir = ramfs_dir_finddir,
    .create = ramfs_dir_create,
    .mkdir = ramfs_dir_mkdir,
    .unlink = ramfs_dir_unlink,
    .stat = ramfs_dir_stat
};

static vfs_ops_t ramfs_file_ops = {
    .read = ramfs_file_read,
    .write = ramfs_file_write,
    .truncate = ramfs_file_truncate,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .stat = ramfs_file_stat
};

vfs_node_t* ramfs_create_root(void) {
    vfs_node_t* root = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!root) return NULL;

    ramfs_dir_t* dir = (ramfs_dir_t*)kmalloc(sizeof(ramfs_dir_t));
    if (!dir) {
        kfree(root);
        return NULL;
    }

    dir->children = NULL;
    dir->count = 0;

    kstrncpy(root->name, "/", VFS_NAME_MAX);
    root->type = VFS_TYPE_DIRECTORY;
    root->permissions = VFS_PERM_READ | VFS_PERM_WRITE | VFS_PERM_EXEC;
    root->size = 0;
    root->ino = 1;
    root->flags = 0;
    root->ops = &ramfs_dir_ops;
    root->internal = dir;
    root->parent = NULL;
    root->mount_target = NULL;
    root->open_count = 0;

    return root;
}

int ramfs_mount(const char* mount_point) {
    vfs_node_t* root = ramfs_create_root();
    if (!root) return -1;
    if (vfs_mount(mount_point, root) != 0) {
        ramfs_dir_t* dir = (ramfs_dir_t*)root->internal;
        if (dir) kfree(dir);
        kfree(root);
        return -1;
    }
    return 0;
}
