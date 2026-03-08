#include "progmod.h"
#include "progman.h"
#include "../syscall.h"
#include "../lib/string.h"

static int is_shell_running(void) {
    sys_taskinfo_t tasks[16];
    int count = sys_get_tasks(tasks, 16);

    for (int i = 0; i < count; i++) {
        if (strcasecmp(tasks[i].name, "SHELL.ELF") == 0) {
            return 1;
        }
    }
    return 0;
}

static int textmode_init(prog_instance_t *inst) {
    (void)inst;

    progman_kill_tasks();
    sys_gfx_exit();

    if (!is_shell_running()) {
        void *vc = sys_vc_create();
        if (vc) {
            /* attach to our own task so the child inherits it */
            sys_vc_attach(vc, sys_getpid());
        }
        sys_spawn_async("SHELL.ELF");
    }

    sys_exit();

    return 0; /* Never reached */
}

const progmod_t textmode_module = {
    .name = "Text Mode",
    .icon_path = "C:/ICONS/AGIX.ICO",
    .init = textmode_init,
    .update = 0,
    .handle_event = 0,
    .cleanup = 0,
    .flags = 0
};
