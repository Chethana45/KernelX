#ifndef FS_VFS_H
#define FS_VFS_H

#include <kernel/types.h>

#define VFS_TYPE_FILE       0x01
#define VFS_TYPE_DIRECTORY  0x02
#define VFS_TYPE_MOUNTPOINT 0x04

#define VFS_NODE_READONLY    0x00000001u

#define VFS_PERM_READ       0x04
#define VFS_PERM_WRITE      0x02
#define VFS_PERM_EXEC       0x01

#define VFS_O_RDONLY        0x0000
#define VFS_O_WRONLY        0x0001
#define VFS_O_RDWR          0x0002
#define VFS_O_CREAT         0x0040
#define VFS_O_TRUNC         0x0200
#define VFS_O_APPEND        0x0400

#define VFS_SEEK_SET        0
#define VFS_SEEK_CUR        1
#define VFS_SEEK_END        2

#define VFS_NAME_MAX        64
#define VFS_PATH_MAX        256

/* Forward declarations */
struct vfs_node;

/* Directory entry structure */
typedef struct vfs_dirent {
    char     name[VFS_NAME_MAX];
    uint32_t ino;
    uint32_t type;
    uint32_t size;
} vfs_dirent_t;

/* File metadata (stat) */
typedef struct vfs_stat {
    uint32_t ino;
    uint32_t type;
    uint32_t size;
    uint32_t permissions;
    uint32_t flags;
} vfs_stat_t;

/* Node operations interface */
typedef struct vfs_ops {
    int32_t (*read)(struct vfs_node* node, uint32_t offset, uint32_t size, uint8_t* buffer);
    int32_t (*write)(struct vfs_node* node, uint32_t offset, uint32_t size, const uint8_t* buffer);
    int     (*truncate)(struct vfs_node* node);
    int     (*open)(struct vfs_node* node, uint32_t flags);
    int     (*close)(struct vfs_node* node);
    int     (*readdir)(struct vfs_node* node, uint32_t index, vfs_dirent_t* dirent);
    struct vfs_node* (*finddir)(struct vfs_node* node, const char* name);
    int     (*create)(struct vfs_node* parent, const char* name, uint32_t mode);
    int     (*mkdir)(struct vfs_node* parent, const char* name, uint32_t mode);
    int     (*unlink)(struct vfs_node* parent, const char* name);
    int     (*stat)(struct vfs_node* node, vfs_stat_t* stat);
} vfs_ops_t;

/* VFS Node structure */
typedef struct vfs_node {
    char             name[VFS_NAME_MAX];
    uint32_t         type;
    uint32_t         permissions;
    uint32_t         size;
    uint32_t         ino;
    uint32_t         flags;
    vfs_ops_t*       ops;
    void*            internal;       /* Filesystem-specific private data */
    struct vfs_node* parent;
    struct vfs_node* mount_target;   /* If this node is a mountpoint */
    uint32_t         open_count;     /* Active VFS opens; prevents unsafe unlink */
} vfs_node_t;

/* Global VFS operations */
void        vfs_init(void);
int         vfs_mount(const char* path, vfs_node_t* fs_root);
vfs_node_t* vfs_get_root(void);
vfs_node_t* vfs_resolve_path(const char* path, const char* cwd);
int         vfs_normalize_path(char* out, const char* path, const char* cwd);

int32_t     vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
int32_t     vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer);
int         vfs_truncate(vfs_node_t* node);
int         vfs_open(vfs_node_t* node, uint32_t flags);
int         vfs_close(vfs_node_t* node);
int         vfs_readdir(vfs_node_t* node, uint32_t index, vfs_dirent_t* dirent);
vfs_node_t* vfs_finddir(vfs_node_t* node, const char* name);
int         vfs_create(const char* path, const char* cwd, uint32_t mode);
int         vfs_mkdir(const char* path, const char* cwd, uint32_t mode);
int         vfs_unlink(const char* path, const char* cwd);
int         vfs_stat(const char* path, const char* cwd, vfs_stat_t* stat);

#endif /* FS_VFS_H */
