#include <fs/vfs.h>
#include <mm/kheap.h>
#include <kernel/printk.h>

static vfs_node_t* vfs_root = NULL;

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

void vfs_init(void) {
    vfs_root = NULL;
}

vfs_node_t* vfs_get_root(void) {
    return vfs_root;
}

int vfs_mount(const char* path, vfs_node_t* fs_root) {
    if (!path || !fs_root) return -1;

    if (kstrcmp(path, "/") == 0) {
        vfs_root = fs_root;
        return 0;
    }

    vfs_node_t* mount_point = vfs_resolve_path(path, "/");
    if (!mount_point || !(mount_point->type & VFS_TYPE_DIRECTORY)) return -1;
    if (mount_point->mount_target != NULL) return -1;
    if (!(fs_root->type & VFS_TYPE_DIRECTORY)) return -1;

    mount_point->type |= VFS_TYPE_MOUNTPOINT;
    mount_point->mount_target = fs_root;
    /* The mounted root's '..' points to the directory containing the mountpoint. */
    fs_root->parent = mount_point->parent;
    return 0;
}

/*
 * Canonicalize relative/absolute path tokens:
 * Combines cwd and path, resolves '.', '..', and duplicate slashes.
 */
static int canonicalize_path(char* out, const char* path, const char* cwd) {
    char temp[VFS_PATH_MAX * 2];
    char* tokens[32];
    int token_count = 0;
    size_t tlen = 0;

    if (!out || !path || path[0] == '\0') return -1;

    if (path[0] != '/') {
        const char* c = (cwd && cwd[0] == '/') ? cwd : "/";
        while (*c) {
            if (tlen >= sizeof(temp) - 1) return -1;
            temp[tlen++] = *c++;
        }
        if (tlen == 0 || temp[tlen - 1] != '/') {
            if (tlen >= sizeof(temp) - 1) return -1;
            temp[tlen++] = '/';
        }
    }

    while (*path) {
        if (tlen >= sizeof(temp) - 1) return -1;
        temp[tlen++] = *path++;
    }
    temp[tlen] = '\0';

    char* ptr = temp;
    while (*ptr) {
        while (*ptr == '/') ptr++;
        if (!*ptr) break;

        char* token = ptr;
        while (*ptr && *ptr != '/') ptr++;
        size_t token_len = (size_t)(ptr - token);
        if (*ptr) *ptr++ = '\0';

        if (token_len == 1 && token[0] == '.') continue;
        if (token_len == 2 && token[0] == '.' && token[1] == '.') {
            if (token_count > 0) token_count--;
            continue;
        }
        if (token_len == 0 || token_len >= VFS_NAME_MAX || token_count >= 32) return -1;
        tokens[token_count++] = token;
    }

    size_t out_len = 0;
    out[out_len++] = '/';
    for (int i = 0; i < token_count; i++) {
        size_t n = kstrlen(tokens[i]);
        if (out_len + n + (i ? 1 : 0) >= VFS_PATH_MAX) return -1;
        if (i) out[out_len++] = '/';
        for (size_t j = 0; j < n; j++) out[out_len++] = tokens[i][j];
    }
    out[out_len] = '\0';
    return 0;
}

int vfs_normalize_path(char* out, const char* path, const char* cwd) {
    return canonicalize_path(out, path, cwd);
}

vfs_node_t* vfs_resolve_path(const char* path, const char* cwd) {
    if (!vfs_root) return NULL;
    if (!path || path[0] == '\0') return NULL;

    char canon[VFS_PATH_MAX];
    if (canonicalize_path(canon, path, cwd) != 0) return NULL;

    if (kstrcmp(canon, "/") == 0) {
        vfs_node_t* root = vfs_root;
        while (root->mount_target != NULL) {
            root = root->mount_target;
        }
        return root;
    }

    vfs_node_t* curr = vfs_root;
    if (curr->mount_target != NULL) {
        curr = curr->mount_target;
    }

    char temp[VFS_PATH_MAX];
    kstrncpy(temp, canon, VFS_PATH_MAX);

    char* ptr = temp + 1; /* Skip leading '/' */
    while (*ptr != '\0') {
        char* token = ptr;
        while (*ptr != '/' && *ptr != '\0') ptr++;
        if (*ptr != '\0') *ptr++ = '\0';

        vfs_node_t* next = vfs_finddir(curr, token);
        if (!next) {
            return NULL;
        }

        curr = next;
        while (curr->mount_target != NULL) {
            curr = curr->mount_target;
        }
    }

    return curr;
}

int32_t vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (!node || !buffer) return -1;
    while (node->mount_target != NULL) node = node->mount_target;
    if (node->ops && node->ops->read) {
        return node->ops->read(node, offset, size, buffer);
    }
    return -1;
}

int32_t vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    if (!node || !buffer) return -1;
    while (node->mount_target != NULL) node = node->mount_target;
    if (node->ops && node->ops->write) {
        return node->ops->write(node, offset, size, buffer);
    }
    return -1;
}

int vfs_truncate(vfs_node_t* node) {
    if (!node) return -1;
    while (node->mount_target != NULL) node = node->mount_target;
    if (!node->ops || !node->ops->truncate) return -1;
    return node->ops->truncate(node);
}

int vfs_open(vfs_node_t* node, uint32_t flags) {
    if (!node) return -1;
    while (node->mount_target != NULL) node = node->mount_target;

    uint32_t access = flags & 3u;
    if (access == 3u) return -1;
    bool wants_write = (access == VFS_O_WRONLY || access == VFS_O_RDWR);
    if (wants_write && ((node->flags & VFS_NODE_READONLY) ||
                        !node->ops || !node->ops->write)) return -1;
    if (node->open_count == 0xFFFFFFFFu) return -1;
    if (node->ops && node->ops->open && node->ops->open(node, flags) != 0) return -1;
    node->open_count++;
    return 0;
}

int vfs_close(vfs_node_t* node) {
    if (!node) return -1;
    while (node->mount_target != NULL) node = node->mount_target;
    if (node->open_count == 0) return -1;
    if (node->ops && node->ops->close && node->ops->close(node) != 0) return -1;
    node->open_count--;
    return 0;
}

int vfs_readdir(vfs_node_t* node, uint32_t index, vfs_dirent_t* dirent) {
    if (!node || !dirent) return -1;
    while (node->mount_target != NULL) node = node->mount_target;
    if (node->ops && node->ops->readdir) {
        return node->ops->readdir(node, index, dirent);
    }
    return -1;
}

vfs_node_t* vfs_finddir(vfs_node_t* node, const char* name) {
    if (!node || !name) return NULL;
    while (node->mount_target != NULL) node = node->mount_target;
    if (node->ops && node->ops->finddir) {
        return node->ops->finddir(node, name);
    }
    return NULL;
}

/* Helper to split canonical path into parent directory and target filename */
static int split_parent_path(const char* path, const char* cwd, char* parent_path, char* target_name) {
    char canon[VFS_PATH_MAX];
    if (canonicalize_path(canon, path, cwd) != 0) return -1;

    if (kstrcmp(canon, "/") == 0) {
        return -1; /* Root has no parent */
    }

    int last_slash = -1;
    for (int i = 0; canon[i] != '\0'; i++) {
        if (canon[i] == '/') last_slash = i;
    }

    if (last_slash == 0) {
        parent_path[0] = '/';
        parent_path[1] = '\0';
        kstrncpy(target_name, canon + 1, VFS_NAME_MAX);
    } else {
        for (int i = 0; i < last_slash; i++) {
            parent_path[i] = canon[i];
        }
        parent_path[last_slash] = '\0';
        kstrncpy(target_name, canon + last_slash + 1, VFS_NAME_MAX);
    }
    return 0;
}

int vfs_create(const char* path, const char* cwd, uint32_t mode) {
    char parent_path[VFS_PATH_MAX];
    char name[VFS_NAME_MAX];

    if (split_parent_path(path, cwd, parent_path, name) != 0) {
        return -1;
    }

    vfs_node_t* parent = vfs_resolve_path(parent_path, "/");
    if (!parent) return -1;
    while (parent->mount_target != NULL) parent = parent->mount_target;

    if (parent->ops && parent->ops->create) {
        return parent->ops->create(parent, name, mode);
    }
    return -1;
}

int vfs_mkdir(const char* path, const char* cwd, uint32_t mode) {
    char parent_path[VFS_PATH_MAX];
    char name[VFS_NAME_MAX];

    if (split_parent_path(path, cwd, parent_path, name) != 0) {
        return -1;
    }

    vfs_node_t* parent = vfs_resolve_path(parent_path, "/");
    if (!parent) return -1;
    while (parent->mount_target != NULL) parent = parent->mount_target;

    if (parent->ops && parent->ops->mkdir) {
        return parent->ops->mkdir(parent, name, mode);
    }
    return -1;
}

int vfs_unlink(const char* path, const char* cwd) {
    char parent_path[VFS_PATH_MAX];
    char name[VFS_NAME_MAX];

    if (split_parent_path(path, cwd, parent_path, name) != 0) {
        return -1;
    }

    vfs_node_t* parent = vfs_resolve_path(parent_path, "/");
    if (!parent) return -1;
    while (parent->mount_target != NULL) parent = parent->mount_target;

    if (parent->ops && parent->ops->unlink) {
        return parent->ops->unlink(parent, name);
    }
    return -1;
}

int vfs_stat(const char* path, const char* cwd, vfs_stat_t* stat) {
    if (!stat) return -1;
    vfs_node_t* node = vfs_resolve_path(path, cwd);
    if (!node) return -1;

    while (node->mount_target != NULL) node = node->mount_target;

    if (node->ops && node->ops->stat) {
        return node->ops->stat(node, stat);
    }

    stat->ino = node->ino;
    stat->type = node->type;
    stat->size = node->size;
    stat->permissions = node->permissions;
    stat->flags = node->flags;
    return 0;
}
