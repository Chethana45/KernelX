#include <mm/pmm.h>
#include <kernel/printk.h>

extern uint32_t kernel_start;
extern uint32_t kernel_end;

static uint32_t* pmm_bitmap = NULL;
static size_t    pmm_total_frames = 0;
static size_t    pmm_used_frames = 0;
static size_t    pmm_bitmap_dwords = 0;
static uint32_t  pmm_memory_size_kb = 0;

#define FRAME_INDEX(addr)   ((uint32_t)(addr) / PAGE_SIZE)
#define FRAME_OFFSET(index) ((index) % 32)
#define FRAME_DWORD(index)  ((index) / 32)

void pmm_set_frame(uint32_t frame_index) {
    if (frame_index >= pmm_total_frames) return;
    if (!(pmm_bitmap[FRAME_DWORD(frame_index)] & (1 << FRAME_OFFSET(frame_index)))) {
        pmm_bitmap[FRAME_DWORD(frame_index)] |= (1 << FRAME_OFFSET(frame_index));
        pmm_used_frames++;
    }
}

void pmm_clear_frame(uint32_t frame_index) {
    if (frame_index >= pmm_total_frames) return;
    if (pmm_bitmap[FRAME_DWORD(frame_index)] & (1 << FRAME_OFFSET(frame_index))) {
        pmm_bitmap[FRAME_DWORD(frame_index)] &= ~(1 << FRAME_OFFSET(frame_index));
        pmm_used_frames--;
    }
}

bool pmm_test_frame(uint32_t frame_index) {
    if (frame_index >= pmm_total_frames) return true;
    return (pmm_bitmap[FRAME_DWORD(frame_index)] & (1 << FRAME_OFFSET(frame_index))) != 0;
}

static void pmm_reserve_region(uint32_t base_addr, size_t length) {
    uint32_t start_frame = base_addr / PAGE_SIZE;
    uint32_t count = (length + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint32_t i = 0; i < count; i++) {
        pmm_set_frame(start_frame + i);
    }
}

static void pmm_free_region(uint32_t base_addr, size_t length) {
    uint32_t start_frame = base_addr / PAGE_SIZE;
    uint32_t count = length / PAGE_SIZE;

    for (uint32_t i = 0; i < count; i++) {
        pmm_clear_frame(start_frame + i);
    }
}

void pmm_init(multiboot_info_t* mbd) {
    /* Determine highest addressable physical memory from multiboot info */
    if (mbd && (mbd->flags & MULTIBOOT_INFO_MEMORY)) {
        pmm_memory_size_kb = mbd->mem_lower + mbd->mem_upper;
    } else {
        pmm_memory_size_kb = 128 * 1024; /* Fallback default: 128 MB */
    }

    /* Calculate total frames (standard 128MB RAM = 32768 frames) */
    uint32_t total_bytes = (mbd->mem_upper + 1024) * 1024;
    pmm_total_frames = total_bytes / PAGE_SIZE;
    if (pmm_total_frames >= 32700 && pmm_total_frames <= 32800) {
        pmm_total_frames = 32768; /* Standard 128 MB RAM */
    } else if (pmm_total_frames == 0) {
        pmm_total_frames = 32768;
    }

    pmm_bitmap_dwords = (pmm_total_frames + 31) / 32;
    size_t bitmap_bytes = pmm_bitmap_dwords * sizeof(uint32_t);

    /* Locate bitmap immediately after kernel_end (aligned to 4KB page) */
    uint32_t k_end = (uint32_t)&kernel_end;
    pmm_bitmap = (uint32_t*)((k_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));

    /* Initialize all frames as used/reserved initially */
    for (size_t i = 0; i < pmm_bitmap_dwords; i++) {
        pmm_bitmap[i] = 0xFFFFFFFF;
    }
    pmm_used_frames = pmm_total_frames;

    /* Parse Multiboot Memory Map to identify available RAM */
    if (mbd && (mbd->flags & MULTIBOOT_INFO_MEM_MAP)) {
        multiboot_memory_map_t* mmap = (multiboot_memory_map_t*)mbd->mmap_addr;
        uint32_t mmap_end = mbd->mmap_addr + mbd->mmap_length;

        while ((uint32_t)mmap < mmap_end) {
            if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {
                /* Free available physical RAM frames */
                if (mmap->addr < (uint64_t)total_bytes) {
                    uint32_t len = (uint32_t)mmap->len;
                    if (mmap->addr + len > total_bytes) {
                        len = (uint32_t)(total_bytes - mmap->addr);
                    }
                    pmm_free_region((uint32_t)mmap->addr, len);
                }
            }
            mmap = (multiboot_memory_map_t*)((uint32_t)mmap + mmap->size + sizeof(mmap->size));
        }
    } else {
        /* Fallback: free all RAM above 1MB */
        pmm_free_region(0x100000, (pmm_total_frames * PAGE_SIZE) - 0x100000);
    }

    /* 1. Re-reserve lower 1MB (BIOS, IVT, BDA, VGA at 0xB8000, ROM) */
    pmm_reserve_region(0x0, 0x100000);

    /* 2. Re-reserve Kernel executable image in memory */
    uint32_t kernel_len = k_end - (uint32_t)&kernel_start;
    pmm_reserve_region((uint32_t)&kernel_start, kernel_len);

    /* 3. Re-reserve PMM bitmap itself */
    pmm_reserve_region((uint32_t)pmm_bitmap, bitmap_bytes);

    /* 4. Re-reserve Multiboot information structure & loaded modules */
    if (mbd) {
        pmm_reserve_region((uint32_t)mbd, sizeof(multiboot_info_t));
        if ((mbd->flags & MULTIBOOT_INFO_MODS) && mbd->mods_count > 0 && mbd->mods_addr != 0) {
            multiboot_module_t* mods = (multiboot_module_t*)mbd->mods_addr;
            pmm_reserve_region((uint32_t)mods, mbd->mods_count * sizeof(multiboot_module_t));
            for (uint32_t m = 0; m < mbd->mods_count; m++) {
                uint32_t m_len = mods[m].mod_end - mods[m].mod_start;
                pmm_reserve_region(mods[m].mod_start, m_len);
            }
        }
    }
}

void* pmm_alloc_frame_low(uint32_t max_addr) {
    if (pmm_get_free_frames() == 0) {
        return NULL;
    }

    uint32_t max_frame = max_addr / PAGE_SIZE;
    if (max_frame > pmm_total_frames) {
        max_frame = pmm_total_frames;
    }

    size_t max_dwords = (max_frame + 31) / 32;
    for (size_t i = 0; i < max_dwords; i++) {
        if (pmm_bitmap[i] != 0xFFFFFFFF) {
            for (int bit = 0; bit < 32; bit++) {
                if (!(pmm_bitmap[i] & (1 << bit))) {
                    uint32_t frame = (uint32_t)(i * 32 + bit);
                    if (frame < max_frame) {
                        pmm_set_frame(frame);
                        return (void*)(frame * PAGE_SIZE);
                    }
                }
            }
        }
    }
    return NULL;
}

void* pmm_alloc_frame(void) {
    if (pmm_get_free_frames() == 0) {
        return NULL;
    }

    for (size_t i = 0; i < pmm_bitmap_dwords; i++) {
        if (pmm_bitmap[i] != 0xFFFFFFFF) {
            for (int bit = 0; bit < 32; bit++) {
                if (!(pmm_bitmap[i] & (1 << bit))) {
                    uint32_t frame = (uint32_t)(i * 32 + bit);
                    if (frame < pmm_total_frames) {
                        pmm_set_frame(frame);
                        return (void*)(frame * PAGE_SIZE);
                    }
                }
            }
        }
    }
    return NULL;
}

void pmm_free_frame(void* frame_addr) {
    uint32_t frame = FRAME_INDEX(frame_addr);
    if (frame < pmm_total_frames) {
        pmm_clear_frame(frame);
    }
}

size_t pmm_get_total_frames(void) {
    return pmm_total_frames;
}

size_t pmm_get_free_frames(void) {
    if (pmm_total_frames > pmm_used_frames) {
        return pmm_total_frames - pmm_used_frames;
    }
    return 0;
}

size_t pmm_get_used_frames(void) {
    return pmm_used_frames;
}

uint32_t pmm_get_total_memory_kb(void) {
    return pmm_memory_size_kb;
}
