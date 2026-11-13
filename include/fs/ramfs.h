#ifndef FS_RAMFS_H
#define FS_RAMFS_H

#include <fs/vfs.h>

vfs_node_t* ramfs_create_root(void);
int         ramfs_mount(const char* mount_point);

#endif /* FS_RAMFS_H */
