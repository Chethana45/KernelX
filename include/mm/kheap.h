#ifndef MM_KHEAP_H
#define MM_KHEAP_H

#include <kernel/types.h>

#define KHEAP_MAGIC 0x4B484541  /* "KHEA" */

void   kheap_init(uint32_t start_addr, size_t initial_size);
void*  kmalloc(size_t size);
void*  kzalloc(size_t size);
void*  kmalloc_aligned(size_t size, size_t alignment);
void   kfree(void* ptr);

size_t kheap_get_allocated_bytes(void);
size_t kheap_get_free_bytes(void);
size_t kheap_get_total_bytes(void);

#endif /* MM_KHEAP_H */
