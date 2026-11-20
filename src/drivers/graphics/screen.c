#include "screen.h"
#include "../graphics.h"
#include "../../task/timer.h"
#include <stdint.h>

#define DISPLAY_CONFIRM_SECONDS 15u

static uint32_t started;
static uint32_t timeout_ticks;

int display_switch_try(size_t index) {
    uint32_t flags;
    __asm__ volatile("pushfl\n\tpopl %0\n\tcli" : "=r"(flags) :: "memory");
    uint32_t frequency = timer_get_frequency();
    int result = -1;
    if (frequency && frequency <= UINT32_MAX / DISPLAY_CONFIRM_SECONDS) {
        result = gfx_vbe_try_mode(index);
        if (result == 0) {
            started = timer_get_ticks();
            timeout_ticks = frequency * DISPLAY_CONFIRM_SECONDS;
        }
    }
    __asm__ volatile("pushl %0\n\tpopfl" :: "r"(flags) : "cc", "memory");
    return result;
}

int display_switch_poll(void) {
    uint32_t flags;
    __asm__ volatile("pushfl\n\tpopl %0\n\tcli" : "=r"(flags) :: "memory");
    int result = 0;
    if (gfx_vbe_mode_pending() &&
        (uint32_t)(timer_get_ticks() - started) >= timeout_ticks) {
        result = gfx_vbe_revert_mode() == 0 ? 1 : DISPLAY_SWITCH_RESTORE_FAILED;
    }
    __asm__ volatile("pushl %0\n\tpopfl" :: "r"(flags) : "cc", "memory");
    return result;
}

int display_switch_confirm(void) {
    uint32_t flags;
    __asm__ volatile("pushfl\n\tpopl %0\n\tcli" : "=r"(flags) :: "memory");
    int result = display_switch_poll();
    if (result == 0) result = gfx_vbe_confirm_mode();
    else if (result == 1) result = DISPLAY_SWITCH_EXPIRED;
    __asm__ volatile("pushl %0\n\tpopfl" :: "r"(flags) : "cc", "memory");
    return result;
}

int display_switch_revert(void) {
    uint32_t flags;
    __asm__ volatile("pushfl\n\tpopl %0\n\tcli" : "=r"(flags) :: "memory");
    int result = DISPLAY_SWITCH_FAILED;
    if (gfx_vbe_mode_pending())
        result = gfx_vbe_revert_mode() == 0 ? 0 : DISPLAY_SWITCH_RESTORE_FAILED;
    __asm__ volatile("pushl %0\n\tpopfl" :: "r"(flags) : "cc", "memory");
    return result;
}
