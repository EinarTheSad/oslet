#include "timer.h"
#include "io.h"
#include "irq.h"
#include "console.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_BASE_FREQ 1193182

extern void perform_task_switch(void);

static volatile uint32_t timer_ticks = 0;
static volatile int scheduling_enabled = 0;

static void timer_handler(void) {
    timer_ticks++;
    
    if (scheduling_enabled) {
        perform_task_switch();
    }
}

void timer_init(uint32_t frequency) {
    if (frequency == 0 || frequency > PIT_BASE_FREQ)
        frequency = 100;
    
    uint32_t divisor = PIT_BASE_FREQ / frequency;
    
    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));
    
    irq_install_handler(0, timer_handler);
}

uint32_t timer_get_ticks(void) {
    return timer_ticks;
}

void timer_wait(uint32_t ticks) {
    uint32_t target = timer_ticks + ticks;
    while (timer_ticks < target) {
        __asm__ volatile ("hlt");
    }
}

void timer_enable_scheduling(void) {
    scheduling_enabled = 1;
    printf("Scheduling enabled\n");
}