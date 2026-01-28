#include "progman.h"
#include "../syscall.h"
#include "../lib/string.h"

/* Registry of available modules */
static const progmod_t *registry[PROGMAN_REGISTRY_MAX];
static int registry_count = 0;

/* Running instances */
static prog_instance_t instances[PROGMAN_INSTANCES_MAX];
static uint16_t next_instance_id = 1;

void progman_init(void) {
    registry_count = 0;
    next_instance_id = 1;

    for (int i = 0; i < PROGMAN_INSTANCES_MAX; i++) {
        instances[i].module = 0;
        instances[i].instance_id = 0;
        instances[i].state = PROG_STATE_IDLE;
        instances[i].window_count = 0;
        instances[i].user_data = 0;
        for (int j = 0; j < PROG_MAX_WINDOWS; j++) {
            instances[i].windows[j] = 0;
        }
    }
}

int progman_register(const progmod_t *module) {
    if (!module || registry_count >= PROGMAN_REGISTRY_MAX)
        return -1;

    /* Check for duplicate */
    for (int i = 0; i < registry_count; i++) {
        if (strcmp(registry[i]->name, module->name) == 0)
            return -1;
    }

    registry[registry_count++] = module;
    return 0;
}

static const progmod_t* find_module_by_name(const char *name) {
    for (int i = 0; i < registry_count; i++) {
        if (strcmp(registry[i]->name, name) == 0)
            return registry[i];
    }
    return 0;
}

static prog_instance_t* find_free_instance(void) {
    for (int i = 0; i < PROGMAN_INSTANCES_MAX; i++) {
        if (instances[i].state == PROG_STATE_IDLE)
            return &instances[i];
    }
    return 0;
}

int progman_is_running(const char *name) {
    for (int i = 0; i < PROGMAN_INSTANCES_MAX; i++) {
        if (instances[i].state == PROG_STATE_RUNNING &&
            instances[i].module &&
            strcmp(instances[i].module->name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static char g_pending_module_icon[64] = {0};

int progman_launch(const char *name) {
    const progmod_t *module = find_module_by_name(name);
    if (!module)
        return -1;

    /* Check singleton constraint */
    if ((module->flags & PROG_FLAG_SINGLETON) && progman_is_running(name))
        return -1;

    /* Find free slot */
    prog_instance_t *inst = find_free_instance();
    if (!inst)
        return -1;

    /* Initialize instance */
    inst->module = module;
    inst->instance_id = next_instance_id++;
    inst->state = PROG_STATE_RUNNING;
    inst->window_count = 0;
    inst->user_data = 0;
    for (int i = 0; i < PROG_MAX_WINDOWS; i++) {
        inst->windows[i] = 0;
    }

    /* Call init function */
    if (module->init) {
        int result = module->init(inst);
        if (result != 0) {
            /* Init failed */
            inst->state = PROG_STATE_IDLE;
            inst->module = 0;
            return -1;
        }
    }

    /* If a module launch was requested with an icon override, apply it to created windows */
    if (g_pending_module_icon[0]) {
        for (int i = 0; i < inst->window_count; i++) {
            if (inst->windows[i]) {
                sys_win_set_icon(inst->windows[i], g_pending_module_icon);
            }
        }
    }

    return inst->instance_id;
}

int progman_launch_with_icon(const char *name, const char *icon_path) {
    if (icon_path && icon_path[0]) {
        strncpy(g_pending_module_icon, icon_path, sizeof(g_pending_module_icon) - 1);
        g_pending_module_icon[sizeof(g_pending_module_icon) - 1] = '\0';
    } else {
        g_pending_module_icon[0] = '\0';
    }

    int res = progman_launch(name);

    /* Clear pending icon after launch attempt */
    g_pending_module_icon[0] = '\0';
    return res;
}

void progman_close(uint16_t instance_id) {
    for (int i = 0; i < PROGMAN_INSTANCES_MAX; i++) {
        if (instances[i].instance_id == instance_id &&
            instances[i].state == PROG_STATE_RUNNING) {

            prog_instance_t *inst = &instances[i];
            inst->state = PROG_STATE_CLOSING;

            /* Call cleanup */
            if (inst->module && inst->module->cleanup) {
                inst->module->cleanup(inst);
            }

            /* Destroy remaining windows */
            for (int j = 0; j < inst->window_count; j++) {
                if (inst->windows[j]) {
                    sys_win_destroy_form(inst->windows[j]);
                    inst->windows[j] = 0;
                }
            }

            /* Reset instance */
            inst->state = PROG_STATE_IDLE;
            inst->module = 0;
            inst->instance_id = 0;
            inst->window_count = 0;
            inst->user_data = 0;
            return;
        }
    }
}

void progman_update_all(void) {
    for (int i = 0; i < PROGMAN_INSTANCES_MAX; i++) {
        if (instances[i].state == PROG_STATE_RUNNING &&
            instances[i].module &&
            instances[i].module->update) {
            instances[i].module->update(&instances[i]);
        }
    }
}

prog_instance_t* progman_find_by_window(void *form) {
    if (!form) return 0;

    for (int i = 0; i < PROGMAN_INSTANCES_MAX; i++) {
        if (instances[i].state != PROG_STATE_RUNNING)
            continue;

        for (int j = 0; j < instances[i].window_count; j++) {
            if (instances[i].windows[j] == form)
                return &instances[i];
        }
    }
    return 0;
}

int progman_route_event(void *form, int event) {
    prog_instance_t *inst = progman_find_by_window(form);
    if (!inst || !inst->module || !inst->module->handle_event)
        return PROG_EVENT_NONE;

    /* Find window index */
    int window_idx = -1;
    for (int i = 0; i < inst->window_count; i++) {
        if (inst->windows[i] == form) {
            window_idx = i;
            break;
        }
    }

    return inst->module->handle_event(inst, window_idx, event);
}

int progman_get_registered_count(void) {
    return registry_count;
}

const progmod_t* progman_get_registered(int index) {
    if (index < 0 || index >= registry_count)
        return 0;
    return registry[index];
}

int progman_get_running_count(void) {
    int count = 0;
    for (int i = 0; i < PROGMAN_INSTANCES_MAX; i++) {
        if (instances[i].state == PROG_STATE_RUNNING)
            count++;
    }
    return count;
}

prog_instance_t* progman_get_instance(int index) {
    if (index < 0 || index >= PROGMAN_INSTANCES_MAX)
        return 0;
    return &instances[index];
}
