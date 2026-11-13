#ifndef MM_PMM_H
#define MM_PMM_H

#include <kernel/types.h>
#include <kernel/multiboot.h>

#define PAGE_SIZE 4096

void     pmm_init(multiboot_info_t* mbd);
void*    pmm_alloc_frame(void);
void*    pmm_alloc_frame_low(uint32_t max_addr);
void     pmm_free_frame(void* frame_addr);
void     pmm_set_frame(uint32_t frame_index);
void     pmm_clear_frame(uint32_t frame_index);
bool     pmm_test_frame(uint32_t frame_index);

size_t   pmm_get_total_frames(void);
size_t   pmm_get_free_frames(void);
size_t   pmm_get_used_frames(void);
uint32_t pmm_get_total_memory_kb(void);

#endif /* MM_PMM_H */
