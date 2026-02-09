#include "progmod.h"
#include "progman.h"
#include "../syscall.h"
#include "../lib/string.h"

static int shutdown_init(prog_instance_t *inst) {
    (void)inst;

    int resp = sys_win_msgbox("Do you want to shut down osLET?",
                              "ICON=SHUTDOWN;Yes|No",
                              "Shutdown");

    if (resp == 1) {
        progman_kill_tasks();
        sys_gfx_exit();
        sys_shutdown();
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
