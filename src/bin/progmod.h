#pragma once
#include <stdint.h>

typedef struct prog_instance prog_instance_t;

/* Module lifecycle function types */
typedef int  (*prog_init_fn)(prog_instance_t *inst);
typedef void (*prog_update_fn)(prog_instance_t *inst);
typedef int  (*prog_event_fn)(prog_instance_t *inst, int window_idx, int event);
typedef void (*prog_cleanup_fn)(prog_instance_t *inst);

/* Program module definition (static, compile-time) */
typedef struct {
    char name[32];
    char icon_path[64];
    prog_init_fn init;
    prog_update_fn update;
    prog_event_fn handle_event;
    prog_cleanup_fn cleanup;
    uint16_t flags;
} progmod_t;

/* Module flags */
#define PROG_FLAG_SINGLETON    0x0001
#define PROG_FLAG_NO_MINIMIZE  0x0002

/* Instance states */
#define PROG_STATE_IDLE     0
#define PROG_STATE_RUNNING  1
#define PROG_STATE_CLOSING  2

/* Maximum windows per instance */
#define PROG_MAX_WINDOWS    4

/* Program instance (runtime, per-launch) */
struct prog_instance {
    const progmod_t *module;
    uint16_t instance_id;
    uint8_t state;
    uint8_t window_count;
    void *windows[PROG_MAX_WINDOWS];
    void *user_data;
};

/* Event return values from handle_event */
#define PROG_EVENT_NONE       0  /* No action needed */
#define PROG_EVENT_HANDLED    1  /* Event was processed */
#define PROG_EVENT_CLOSE      2  /* Request to close instance */
#define PROG_EVENT_REDRAW     3  /* Request full redraw */

static inline int prog_register_window(prog_instance_t *inst, void *form) {
    if (!inst || !form || inst->window_count >= PROG_MAX_WINDOWS)
        return -1;
    inst->windows[inst->window_count++] = form;
    return inst->window_count - 1;
}

static inline void prog_unregister_window(prog_instance_t *inst, void *form) {
    if (!inst || !form) return;
    for (int i = 0; i < inst->window_count; i++) {
        if (inst->windows[i] == form) {
            /* Shift remaining windows down */
            for (int j = i; j < inst->window_count - 1; j++) {
                inst->windows[j] = inst->windows[j + 1];
            }
            inst->window_count--;
            inst->windows[inst->window_count] = 0;
            return;
        }
    }
}
