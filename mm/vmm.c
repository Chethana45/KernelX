#include <mm/vmm.h>
#include <mm/pmm.h>
#include <kernel/printk.h>

#define INITIAL_PAGE_TABLES 12

static uint32_t kernel_directory[ENTRIES_PER_DIR] __attribute__((aligned(PAGE_SIZE)));
static uint32_t initial_page_tables[INITIAL_PAGE_TABLES][ENTRIES_PER_TABLE] __attribute__((aligned(PAGE_SIZE)));

static uint32_t page_tables_count = 0;
static bool     paging_enabled = false;

static inline void tlb_flush_single(uint32_t addr) {
    __asm__ volatile ("invlpg (%0)" : : "r"(addr) : "memory");
}

void vmm_init(void) {
    /* 1. Clear Page Directory */
    for (size_t i = 0; i < ENTRIES_PER_DIR; i++) {
        kernel_directory[i] = 0;
    }

    /* 2. Identity map initial 48 MB (12 Page Tables * 4 MB each) */
    page_tables_count = INITIAL_PAGE_TABLES;
    for (size_t t = 0; t < INITIAL_PAGE_TABLES; t++) {
        for (size_t p = 0; p < ENTRIES_PER_TABLE; p++) {
            uint32_t phys_addr = (uint32_t)((t * ENTRIES_PER_TABLE + p) * PAGE_SIZE);
            initial_page_tables[t][p] = phys_addr | PAGE_PRESENT | PAGE_WRITABLE;
        }
        /* Point PDE to corresponding page table */
        kernel_directory[t] = ((uint32_t)&initial_page_tables[t][0]) | PAGE_PRESENT | PAGE_WRITABLE;
    }

    /* 3. Load Page Directory physical address into CR3 */
    __asm__ volatile ("mov %0, %%cr3" : : "r"(&kernel_directory) : "memory");

    /* 4. Enable Paging (Set PG bit 31 and WP bit 16 in CR0 for kernel write protection) */
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80010000; /* Bit 31: PG (Paging), Bit 16: WP (Write Protect) */
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");

    paging_enabled = true;
}

int vmm_map_page(uint32_t virt_addr, uint32_t phys_addr, uint32_t flags) {
    uint32_t pde_index = (virt_addr >> 22) & 0x3FF;
    uint32_t pte_index = (virt_addr >> 12) & 0x3FF;

    uint32_t* page_table = NULL;

    /* Check if Page Table is present in Page Directory */
    if (kernel_directory[pde_index] & PAGE_PRESENT) {
        page_table = (uint32_t*)(kernel_directory[pde_index] & ~0xFFF);
    } else {
        /* Allocate a new frame for the Page Table from mapped memory (<48MB) */
        void* pt_frame = pmm_alloc_frame_low(0x03000000);
        if (!pt_frame) {
            return -1; /* Out of memory in mapped region */
        }
        page_table = (uint32_t*)pt_frame;

        /* Zero out newly allocated Page Table */
        for (size_t i = 0; i < ENTRIES_PER_TABLE; i++) {
            page_table[i] = 0;
        }

        kernel_directory[pde_index] = ((uint32_t)page_table) | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
        page_tables_count++;
    }

    /* Map virtual page to physical frame */
    page_table[pte_index] = (phys_addr & ~0xFFF) | (flags & 0xFFF) | PAGE_PRESENT;

    /* Invalidate TLB for updated virtual address */
    tlb_flush_single(virt_addr);
    return 0;
}

void vmm_unmap_page(uint32_t virt_addr) {
    uint32_t pde_index = (virt_addr >> 22) & 0x3FF;
    uint32_t pte_index = (virt_addr >> 12) & 0x3FF;

    if (!(kernel_directory[pde_index] & PAGE_PRESENT)) {
        return;
    }

    uint32_t* page_table = (uint32_t*)(kernel_directory[pde_index] & ~0xFFF);
    page_table[pte_index] = 0;

    tlb_flush_single(virt_addr);
}

void vmm_destroy_page_table(uint32_t virt_addr) {
    uint32_t pde_index = (virt_addr >> 22) & 0x3FF;

    if (!(kernel_directory[pde_index] & PAGE_PRESENT)) {
        return;
    }

    /* Do not destroy the initial static page tables (0 to INITIAL_PAGE_TABLES - 1) */
    if (pde_index < INITIAL_PAGE_TABLES) {
        return;
    }

    uint32_t pt_phys = kernel_directory[pde_index] & ~0xFFF;
    kernel_directory[pde_index] = 0;
    if (page_tables_count > INITIAL_PAGE_TABLES) {
        page_tables_count--;
    }

    pmm_free_frame((void*)pt_phys);
    tlb_flush_single(virt_addr);
}

uint32_t vmm_get_directory_addr(void) {
    return (uint32_t)&kernel_directory;
}

uint32_t vmm_get_phys_addr(uint32_t virt_addr) {
    uint32_t pde_index = (virt_addr >> 22) & 0x3FF;
    uint32_t pte_index = (virt_addr >> 12) & 0x3FF;

    if (!(kernel_directory[pde_index] & PAGE_PRESENT)) {
        return 0;
    }

    uint32_t* page_table = (uint32_t*)(kernel_directory[pde_index] & ~0xFFF);
    if (!(page_table[pte_index] & PAGE_PRESENT)) {
        return 0;
    }

    return (page_table[pte_index] & ~0xFFF) | (virt_addr & 0xFFF);
}

uint32_t vmm_get_page_tables_count(void) {
    return page_tables_count;
}

bool vmm_is_paging_enabled(void) {
    return paging_enabled;
}

uint32_t vmm_get_current_cr3(void) {
    uint32_t val;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(val));
    return val;
}

void vmm_switch_address_space(uint32_t cr3) {
    if (cr3 == 0) {
        cr3 = (uint32_t)&kernel_directory;
    }
    uint32_t current_cr3 = vmm_get_current_cr3();
    if (current_cr3 != cr3) {
        __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3) : "memory");
    }
}

uint32_t vmm_create_address_space(void) {
    void* pdir_frame = pmm_alloc_frame_low(0x03000000);
    if (!pdir_frame) return 0;

    uint32_t* new_pdir = (uint32_t*)pdir_frame;

    for (size_t i = 0; i < ENTRIES_PER_DIR; i++) {
        new_pdir[i] = 0;
    }

    /* Clone kernel mappings (first 12 tables = 48 MB, supervisor privilege) */
    for (size_t i = 0; i < INITIAL_PAGE_TABLES; i++) {
        new_pdir[i] = kernel_directory[i];
    }

    return (uint32_t)new_pdir;
}

void vmm_destroy_address_space(uint32_t cr3) {
    if (cr3 == 0 || cr3 == (uint32_t)&kernel_directory) return;

    uint32_t* pdir = (uint32_t*)cr3;

    for (size_t i = INITIAL_PAGE_TABLES; i < ENTRIES_PER_DIR; i++) {
        if (pdir[i] & PAGE_PRESENT) {
            uint32_t* pt = (uint32_t*)(pdir[i] & ~0xFFF);
            for (size_t p = 0; p < ENTRIES_PER_TABLE; p++) {
                if (pt[p] & PAGE_PRESENT) {
                    pmm_free_frame((void*)(pt[p] & ~0xFFF));
                    pt[p] = 0;
                }
            }
            pmm_free_frame((void*)pt);
            pdir[i] = 0;
        }
    }

    pmm_free_frame((void*)pdir);
}

int vmm_map_user_page(uint32_t cr3, uint32_t virt_addr, uint32_t phys_addr, uint32_t flags) {
    if (cr3 == 0) cr3 = (uint32_t)&kernel_directory;

    uint32_t pde_index = (virt_addr >> 22) & 0x3FF;
    uint32_t pte_index = (virt_addr >> 12) & 0x3FF;

    uint32_t* pdir = (uint32_t*)cr3;
    uint32_t* pt = NULL;

    if (pdir[pde_index] & PAGE_PRESENT) {
        pt = (uint32_t*)(pdir[pde_index] & ~0xFFF);
        if (flags & PAGE_USER) {
            pdir[pde_index] |= PAGE_USER;
        }
        if (flags & PAGE_WRITABLE) {
            pdir[pde_index] |= PAGE_WRITABLE;
        }
    } else {
        void* pt_frame = pmm_alloc_frame_low(0x03000000);
        if (!pt_frame) return -1;

        pt = (uint32_t*)pt_frame;
        for (size_t i = 0; i < ENTRIES_PER_TABLE; i++) {
            pt[i] = 0;
        }

        uint32_t pde_flags = PAGE_PRESENT;
        if (flags & PAGE_USER) {
            pde_flags |= PAGE_USER;
        }
        if (flags & PAGE_WRITABLE) {
            pde_flags |= PAGE_WRITABLE;
        }
        pdir[pde_index] = ((uint32_t)pt) | pde_flags;
    }

    pt[pte_index] = (phys_addr & ~0xFFF) | (flags & 0xFFF) | PAGE_PRESENT;
    tlb_flush_single(virt_addr);
    return 0;
}

bool vmm_validate_user_ptr(const void* ptr, size_t size, bool write_intent) {
    if (!ptr) return false;

    uint32_t start = (uint32_t)ptr;
    if (start < USER_SPACE_START || start >= USER_SPACE_END) {
        return false;
    }

    uint32_t cr3 = vmm_get_current_cr3();
    uint32_t* pdir = (uint32_t*)cr3;

    if (size == 0) {
        /* Zero-length buffer: validate that pointer itself is mapped in user space with correct permissions */
        uint32_t pde_index = (start >> 22) & 0x3FF;
        uint32_t pte_index = (start >> 12) & 0x3FF;

        if (!(pdir[pde_index] & PAGE_PRESENT) || !(pdir[pde_index] & PAGE_USER)) {
            return false;
        }
        uint32_t* pt = (uint32_t*)(pdir[pde_index] & ~0xFFF);
        if (!(pt[pte_index] & PAGE_PRESENT) || !(pt[pte_index] & PAGE_USER)) {
            return false;
        }
        if (write_intent) {
            if (!(pdir[pde_index] & PAGE_WRITABLE) || !(pt[pte_index] & PAGE_WRITABLE)) {
                return false;
            }
        }
        return true;
    }

    /* Check for integer overflow in address + size */
    uint64_t end_check = (uint64_t)start + (uint64_t)size;
    if (end_check > (uint64_t)USER_SPACE_END) {
        return false;
    }

    uint32_t end = (uint32_t)end_check;
    uint32_t page_start = start & ~0xFFF;
    uint32_t page_end = (end - 1) & ~0xFFF;

    for (uint32_t addr = page_start; ; addr += PAGE_SIZE) {
        uint32_t pde_index = (addr >> 22) & 0x3FF;
        uint32_t pte_index = (addr >> 12) & 0x3FF;

        if (!(pdir[pde_index] & PAGE_PRESENT) || !(pdir[pde_index] & PAGE_USER)) {
            return false;
        }

        uint32_t* pt = (uint32_t*)(pdir[pde_index] & ~0xFFF);
        if (!(pt[pte_index] & PAGE_PRESENT) || !(pt[pte_index] & PAGE_USER)) {
            return false;
        }

        if (write_intent) {
            if (!(pdir[pde_index] & PAGE_WRITABLE) || !(pt[pte_index] & PAGE_WRITABLE)) {
                return false;
            }
        }

        if (addr >= page_end) break;
        if (addr > 0xFFFFF000) break; /* Avoid 32-bit wrap around */
    }

    return true;
}
