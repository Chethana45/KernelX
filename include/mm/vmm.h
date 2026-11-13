#ifndef MM_VMM_H
#define MM_VMM_H

#include <kernel/types.h>

#define PAGE_PRESENT        0x001
#define PAGE_WRITABLE       0x002
#define PAGE_USER           0x004
#define PAGE_WRITETHROUGH   0x008
#define PAGE_NOT_CACHEABLE  0x010
#define PAGE_ACCESSED       0x020
#define PAGE_DIRTY          0x040

#define ENTRIES_PER_TABLE   1024
#define ENTRIES_PER_DIR     1024

#define USER_SPACE_START    0x40000000
#define USER_SPACE_END      0xC0000000

void     vmm_init(void);
int      vmm_map_page(uint32_t virt_addr, uint32_t phys_addr, uint32_t flags);
void     vmm_unmap_page(uint32_t virt_addr);
void     vmm_destroy_page_table(uint32_t virt_addr);
uint32_t vmm_get_phys_addr(uint32_t virt_addr);
uint32_t vmm_get_page_tables_count(void);
bool     vmm_is_paging_enabled(void);
uint32_t vmm_get_directory_addr(void);

/* Process Address Space Management */
uint32_t vmm_create_address_space(void);
void     vmm_destroy_address_space(uint32_t cr3);
void     vmm_switch_address_space(uint32_t cr3);
uint32_t vmm_get_current_cr3(void);
int      vmm_map_user_page(uint32_t cr3, uint32_t virt_addr, uint32_t phys_addr, uint32_t flags);
bool     vmm_validate_user_ptr(const void* ptr, size_t size, bool write_intent);

#endif /* MM_VMM_H */
