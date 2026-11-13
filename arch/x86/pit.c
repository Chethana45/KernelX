#include <arch/x86/pit.h>
#include <arch/x86/isr.h>
#include <arch/x86/pic.h>
#include <arch/x86/io.h>
#include <sched/sched.h>

static volatile uint32_t pit_ticks = 0;
static uint32_t pit_frequency = 100;

static void pit_callback(registers_t* regs) {
    (void)regs;
    pit_ticks++;
    if (scheduler_is_active()) {
        scheduler_tick();
    }
}

void pit_init(uint32_t frequency) {
    if (frequency == 0) frequency = 100;
    pit_frequency = frequency;

    uint32_t divisor = PIT_BASE_FREQUENCY / frequency;
    if (divisor > 65535) divisor = 65535;
    if (divisor < 1) divisor = 1;

    /* 0x36: Channel 0, lobyte/hibyte access, Mode 3 square wave generator */
    outb(PIT_COMMAND_PORT, 0x36);

    uint8_t low  = (uint8_t)(divisor & 0xFF);
    uint8_t high = (uint8_t)((divisor >> 8) & 0xFF);

    outb(PIT_CHANNEL0_DATA, low);
    outb(PIT_CHANNEL0_DATA, high);

    /* Register callback on IRQ0 (vector 32) */
    isr_register_handler(IRQ0, pit_callback);

    /* Unmask IRQ0 on 8259 PIC */
    pic_clear_mask(0);
}

uint32_t pit_get_ticks(void) {
    return pit_ticks;
}

uint32_t pit_get_uptime_sec(void) {
    if (pit_frequency == 0) return 0;
    return pit_ticks / pit_frequency;
}

void pit_sleep(uint32_t ms) {
    if (pit_frequency == 0) return;
    uint32_t target_ticks = (ms * pit_frequency) / 1000;
    if (target_ticks == 0) target_ticks = 1;

    uint32_t start = pit_ticks;
    while ((pit_ticks - start) < target_ticks) {
        __asm__ volatile ("hlt");
    }
}
