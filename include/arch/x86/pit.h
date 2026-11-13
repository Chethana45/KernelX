#ifndef ARCH_X86_PIT_H
#define ARCH_X86_PIT_H

#include <kernel/types.h>

#define PIT_BASE_FREQUENCY 1193182

#define PIT_CHANNEL0_DATA 0x40
#define PIT_CHANNEL1_DATA 0x41
#define PIT_CHANNEL2_DATA 0x42
#define PIT_COMMAND_PORT  0x43

void     pit_init(uint32_t frequency);
uint32_t pit_get_ticks(void);
uint32_t pit_get_uptime_sec(void);
void     pit_sleep(uint32_t ms);

#endif /* ARCH_X86_PIT_H */
