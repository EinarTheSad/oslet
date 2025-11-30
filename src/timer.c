#include "timer.h"
#include "irq/io.h"
#include "irq/irq.h"
#include "console.h"
#include "task.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_BASE_FREQ 1193182

static volatile uint32_t timer_ticks = 0;
static volatile int scheduling_enabled = 0;

static void timer_handler(void) {
    timer_ticks++;
    
    if (scheduling_enabled) {
        task_tick();
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
    if (!scheduling_enabled) {
        /* Scheduler off, busy-wait */
        uint32_t target = timer_ticks + ticks;
        while (timer_ticks < target) {
            __asm__ volatile ("hlt");
        }
        return;
    }
    
    /* Scheduler on, check other tasks */
    task_t *current = task_get_current();
    if (!current) {
        /* Fallback to busy-wait */
        uint32_t target = timer_ticks + ticks;
        while (timer_ticks < target) {
            __asm__ volatile ("hlt");
        }
        return;
    }
    
    /* Count other tasks */
    int other_tasks = 0;
    task_t *t = current->next;
    int safety = 0;
    
    while (t != current && safety < 100) {
        if (t && (t->state == TASK_READY || t->state == TASK_RUNNING)) {
            other_tasks++;
        }
        if (t) t = t->next;
        safety++;
    }
    
    if (other_tasks > 0) {
        uint32_t ms = (ticks * 1000) / 100;
        task_sleep(ms);
    } else {
        uint32_t target = timer_ticks + ticks;
        while (timer_ticks < target) {
            __asm__ volatile ("hlt");
        }
    }
}

void timer_enable_scheduling(void) {
    scheduling_enabled = 1;
}