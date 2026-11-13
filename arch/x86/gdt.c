#include <arch/x86/gdt.h>
#include <kernel/printk.h>

extern void gdt_flush(uint32_t gdt_ptr_addr);
extern void tss_flush(void);

static gdt_entry_t gdt_entries[GDT_ENTRIES];
static gdt_ptr_t   gdt_ptr;
static tss_entry_t tss;

void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt_entries[num].base_low    = (uint16_t)(base & 0xFFFF);
    gdt_entries[num].base_middle = (uint8_t)((base >> 16) & 0xFF);
    gdt_entries[num].base_high   = (uint8_t)((base >> 24) & 0xFF);

    gdt_entries[num].limit_low   = (uint16_t)(limit & 0xFFFF);
    gdt_entries[num].granularity = (uint8_t)((limit >> 16) & 0x0F);

    gdt_entries[num].granularity |= (uint8_t)(gran & 0xF0);
    gdt_entries[num].access      = access;
}

void tss_set_stack(uint32_t esp0) {
    tss.esp0 = esp0;
}

void gdt_init(void) {
    gdt_ptr.limit = (uint16_t)(sizeof(gdt_entry_t) * GDT_ENTRIES - 1);
    gdt_ptr.base  = (uint32_t)&gdt_entries;

    /* 0x00: Null descriptor */
    gdt_set_gate(0, 0, 0, 0, 0);

    /* 0x08: Kernel Code Segment (Base=0, Limit=4GB, Ring 0, Exec/Read) */
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

    /* 0x10: Kernel Data Segment (Base=0, Limit=4GB, Ring 0, Read/Write) */
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    /* 0x18: User Code Segment (Base=0, Limit=4GB, Ring 3, Exec/Read) */
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);

    /* 0x20: User Data Segment (Base=0, Limit=4GB, Ring 3, Read/Write) */
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);

    /* 0x28: Task State Segment (TSS) */
    uint32_t tss_base = (uint32_t)&tss;
    uint32_t tss_limit = sizeof(tss_entry_t) - 1;

    /* Zero out TSS structure */
    uint8_t* p = (uint8_t*)&tss;
    for (size_t i = 0; i < sizeof(tss_entry_t); i++) {
        p[i] = 0;
    }

    tss.ss0 = KERNEL_DS;
    tss.esp0 = 0; /* Updated dynamically on context switch */
    tss.iomap_base = sizeof(tss_entry_t);

    /* TSS descriptor: Access=0x89 (Present, Ring 0, 32-bit TSS available), Gran=0x00 (byte limit) */
    gdt_set_gate(5, tss_base, tss_limit, 0x89, 0x00);

    /* Flush and activate GDT & TSS */
    gdt_flush((uint32_t)&gdt_ptr);
    tss_flush();
}
