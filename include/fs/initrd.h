#ifndef FS_INITRD_H
#define FS_INITRD_H

#include <fs/vfs.h>
#include <kernel/multiboot.h>

int initrd_init(multiboot_info_t* mbd);
vfs_node_t* initrd_mount(const char* mount_point);
bool initrd_is_mounted(void);
uint32_t initrd_get_file_count(void);

#endif /* FS_INITRD_H */
