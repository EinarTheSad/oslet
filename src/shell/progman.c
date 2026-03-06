#include "progman.h"
#include "../syscall.h"
#include "../lib/string.h"

/* Registry of available modules */
static const progmod_t *registry[PROGMAN_REGISTRY_MAX];
static int registry_count = 0;

/* Running instances */
prog_instance_t instances[PROGMAN_INSTANCES_MAX];
static uint16_t next_instance_id = 1;

void progman_init(void) {
    registry_count = 0;
    next_instance_id = 1;

    prog_instance_t *inst;
    PROGMAN_FOREACH_INSTANCE(inst) {
        inst->module = 0;
        inst->instance_id = 0;
        inst->state = PROG_STATE_IDLE;
        inst->window_count = 0;
        inst->user_data = 0;
        for (int j = 0; j < PROG_MAX_WINDOWS; j++) {
            inst->windows[j] = 0;
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
    prog_instance_t *inst;
    PROGMAN_FOREACH_INSTANCE(inst) {
        if (inst->state == PROG_STATE_IDLE)
            return inst;
    }
    return 0;
}

int progman_is_running(const char *name) {
    prog_instance_t *inst;
    PROGMAN_FOREACH_RUNNING(inst) {
        if (inst->module && strcmp(inst->module->name, name) == 0) {
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
    prog_instance_t *inst;
    PROGMAN_FOREACH_RUNNING(inst) {
        if (inst->instance_id == instance_id) {
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
    prog_instance_t *inst;
    PROGMAN_FOREACH_RUNNING(inst) {
        if (inst->module && inst->module->update) {
            inst->module->update(inst);
        }
    }
}

prog_instance_t* progman_find_by_window(void *form) {
    if (!form) return 0;

    prog_instance_t *inst;
    PROGMAN_FOREACH_RUNNING(inst) {
        for (int j = 0; j < inst->window_count; j++) {
            if (inst->windows[j] == form)
                return inst;
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
    prog_instance_t *inst;
    PROGMAN_FOREACH_RUNNING(inst) {
        count++;
    }
    return count;
}

prog_instance_t* progman_get_instance(int index) {
    if (index < 0 || index >= PROGMAN_INSTANCES_MAX)
        return 0;
    return &instances[index];
}

int progman_kill_tasks(void) {
    /* Terminate any running ELF tasks (except the shell and the process calling the function) */
    sys_taskinfo_t tasks[32];
    int tcount = sys_get_tasks(tasks, 32);
    uint32_t mypid = sys_getpid();
    for (int i = 0; i < tcount; i++) {
        if (tasks[i].tid == mypid) continue;
        if (strcasecmp(tasks[i].name, "SHELL.ELF") == 0) continue;
        if (str_ends_with_icase(tasks[i].name, ".ELF")) {
            sys_kill(tasks[i].tid);
        }
    }

    /* End all running applets */
    uint16_t to_close[PROGMAN_INSTANCES_MAX];
    int tc = 0;
    for (int i = 0; i < PROGMAN_INSTANCES_MAX; i++) {
        prog_instance_t *pi = progman_get_instance(i);
        if (pi && pi->state == PROG_STATE_RUNNING) {
            to_close[tc++] = pi->instance_id;
        }
    }

    for (int i = 0; i < tc; i++) {
        progman_close(to_close[i]);
    }

    return 0;
}