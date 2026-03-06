#include "progmod.h"
#include "progman.h"
#include "../syscall.h"
#include "../lib/string.h"

static int shutdown_init(prog_instance_t *inst) {
    (void)inst;

    int resp = sys_win_msgbox("This will end your osLET session.",
                              "ICON=SHUTDOWN;Power off|Reboot|Cancel",
                              "Shutdown");

    if (resp != 3) {
        progman_kill_tasks();
        sys_gfx_exit();

        if (resp == 1)
            sys_shutdown();
        else if (resp == 2)
            sys_reboot();
    }

    return 0; /* app exits immediately after handling */
}

const progmod_t shutdown_module = {
    .name = "Shutdown",
    .icon_path = "C:/ICONS/SHUTDOWN.ICO",
    .init = shutdown_init,
    .update = 0,
    .handle_event = 0,
    .cleanup = 0,
    .flags = 0
};
