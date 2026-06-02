#include "progmod.h"
#include "progman.h"
#include "../syscall.h"
#include "../lib/string.h"

static int textmode_init(prog_instance_t *inst) {
    (void)inst;

    progman_kill_tasks();
    sys_gfx_exit();
    sys_spawn_async_args("C:/SHELL.ELF", "/d");

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
