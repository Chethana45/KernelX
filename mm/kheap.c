#include <mm/kheap.h>
#include <mm/vmm.h>
#include <kernel/printk.h>

typedef struct kheap_block {
    uint32_t            magic;
    size_t              size;    /* Size of payload data in bytes */
    bool                is_free;
    struct kheap_block* next;
    struct kheap_block* prev;
} kheap_block_t;

#define BLOCK_HEADER_SIZE sizeof(kheap_block_t)
#define ALIGN8(s)         (((s) + 7) & ~7)

static kheap_block_t* heap_start = NULL;
static size_t heap_total_size = 0;
static size_t heap_allocated_bytes = 0;

void kheap_init(uint32_t start_addr, size_t initial_size) {
    heap_start = (kheap_block_t*)start_addr;
    heap_total_size = initial_size;
    heap_allocated_bytes = 0;

    heap_start->magic   = KHEAP_MAGIC;
    heap_start->size    = initial_size - BLOCK_HEADER_SIZE;
    heap_start->is_free = true;
    heap_start->next    = NULL;
    heap_start->prev    = NULL;
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;
    size = ALIGN8(size);

    kheap_block_t* current = heap_start;
    while (current != NULL) {
        if (current->magic != KHEAP_MAGIC) {
            panic("Kernel Heap Corruption: Invalid Block Magic!");
        }

        if (current->is_free && current->size >= size) {
            /* Check if block can be split */
            if (current->size >= size + BLOCK_HEADER_SIZE + 16) {
                kheap_block_t* new_block = (kheap_block_t*)((uintptr_t)current + BLOCK_HEADER_SIZE + size);
                new_block->magic   = KHEAP_MAGIC;
                new_block->size    = current->size - size - BLOCK_HEADER_SIZE;
                new_block->is_free = true;
                new_block->next    = current->next;
                new_block->prev    = current;

                if (current->next != NULL) {
                    current->next->prev = new_block;
                }
                current->next = new_block;
                current->size = size;
            }

            current->is_free = false;
            heap_allocated_bytes += (current->size + BLOCK_HEADER_SIZE);
            return (void*)((uintptr_t)current + BLOCK_HEADER_SIZE);
        }
        current = current->next;
    }

    return NULL; /* Out of heap memory */
}

void* kzalloc(size_t size) {
    void* ptr = kmalloc(size);
    if (!ptr) return NULL;

    uint8_t* p = (uint8_t*)ptr;
    for (size_t i = 0; i < size; i++) {
        p[i] = 0;
    }
    return ptr;
}

void* kmalloc_aligned(size_t size, size_t alignment) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        alignment = 8;
    }

    /* Allocate extra space to fit alignment offset and back-pointer */
    size_t total_size = size + alignment + sizeof(void*);
    void* raw = kmalloc(total_size);
    if (!raw) return NULL;

    uintptr_t raw_addr = (uintptr_t)raw + sizeof(void*);
    uintptr_t aligned_addr = (raw_addr + (alignment - 1)) & ~(alignment - 1);

    /* Store original raw pointer right before the aligned address */
    void** ptr_storage = (void**)(aligned_addr - sizeof(void*));
    *ptr_storage = raw;

    return (void*)aligned_addr;
}

void kfree(void* ptr) {
    if (!ptr) return;

    kheap_block_t* block = (kheap_block_t*)((uintptr_t)ptr - BLOCK_HEADER_SIZE);
    if (block->magic != KHEAP_MAGIC) {
        panic("Kernel Heap Corruption: Invalid Magic on kfree()!");
    }

    if (block->is_free) {
        return; /* Prevent double-free */
    }

    block->is_free = true;
    heap_allocated_bytes -= (block->size + BLOCK_HEADER_SIZE);

    /* Coalesce with next block if free */
    if (block->next != NULL && block->next->is_free) {
        block->size += (BLOCK_HEADER_SIZE + block->next->size);
        block->next = block->next->next;
        if (block->next != NULL) {
            block->next->prev = block;
        }
    }

    /* Coalesce with prev block if free */
    if (block->prev != NULL && block->prev->is_free) {
        block->prev->size += (BLOCK_HEADER_SIZE + block->size);
        block->prev->next = block->next;
        if (block->next != NULL) {
            block->next->prev = block->prev;
        }
    }
}

size_t kheap_get_allocated_bytes(void) {
    return heap_allocated_bytes;
}

size_t kheap_get_free_bytes(void) {
    if (heap_total_size > heap_allocated_bytes) {
        return heap_total_size - heap_allocated_bytes;
    }
    return 0;
}

size_t kheap_get_total_bytes(void) {
    return heap_total_size;
}
