#include "progmod.h"
#include "progman.h"
#include "../syscall.h"
#include "../lib/palette.h"

static int textmode_init(prog_instance_t *inst) {
    (void)inst;

    uint8_t palette[16][3];
    if (palette_parse_pal("C:/OSLET/cga.pal", palette) != 0) {
        sys_win_msgbox("Could not load the CGA palette.", "OK", "Text Mode");
        return -1;
    }

    progman_kill_tasks();
    if (sys_gfx_text_mode(palette) != 0) {
        sys_win_force_full_redraw();
        sys_win_msgbox("Could not enter text mode.", "OK", "Text Mode");
        return -1;
    }
    if (sys_spawn_async_args("C:/SHELL.ELF", "/d") <= 0) {
        if (sys_gfx_enter() != 0) {
            sys_write("Could not restore the desktop display.\n");
            sys_exit();
            return -1;
        }
        sys_win_force_full_redraw();
        sys_win_msgbox("Could not start the shell.", "OK", "Text Mode");
        return -1;
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
