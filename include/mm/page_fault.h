#ifndef MM_PAGE_FAULT_H
#define MM_PAGE_FAULT_H

#include <kernel/types.h>
#include <arch/x86/isr.h>

typedef bool (*page_fault_hook_t)(registers_t* regs, uint32_t faulting_addr);

void page_fault_init(void);
void page_fault_handler(registers_t* regs);
void page_fault_set_hook(page_fault_hook_t hook);

#endif /* MM_PAGE_FAULT_H */
