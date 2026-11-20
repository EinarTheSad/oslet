#include "progmod.h"
#include "progman.h"
#include "../syscall.h"
#include "../lib/stdio.h"

#define SHUTDOWN_STEPS 50
#define SHUTDOWN_STEP_MS 20

static int shutdown_init(prog_instance_t *inst) {
    (void)inst;

    sys_display_info_t display;
    sys_gfx_region_t region = { 0 };
    int display_ready = 0;

    if (sys_gfx_get_display_info(&display) == 0 &&
        display.width > 0 && display.height > 0) {
        region.width = (int)display.width;
        region.height = (int)display.height;
        display_ready = 1;
    }

    if (display_ready) {
        for (int step = 1; step <= SHUTDOWN_STEPS; step++) {
            int previous_y = ((step - 1) * region.height) / SHUTDOWN_STEPS;
            int next_y = (step * region.height) / SHUTDOWN_STEPS;
            sys_gfx_region_t strip = region;
            strip.data = 0;
            strip.y = previous_y;
            strip.height = next_y - previous_y;

            if (strip.height > 0)
                sys_gfx_dither_rect(&strip, -1, COLOR_BLUE);
            sys_gfx_swap();
            sys_busywait(SHUTDOWN_STEP_MS);
        }
    }

    int resp = sys_win_msgbox("This will end your osLET session.",
                              "ICON=SHUTDOWN;Power off|Reboot|Cancel",
                              "Shutdown");

    if (resp == 1 || resp == 2) {
        progman_kill_tasks();
        sys_gfx_exit();

        if (resp == 1)
            sys_shutdown();
        else if (resp == 2)
            sys_reboot();
    } else {
        sys_win_force_full_redraw();
    }

    progman_close(inst->instance_id);

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
