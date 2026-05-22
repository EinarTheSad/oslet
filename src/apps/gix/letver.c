#include "../../syscall.h"
#include "../../lib/stdio.h"
#include "../../lib/string.h"
#include "../../lib/app.h"
#include "../../lib/gix_app.h"

OSLET_APP("System Information", OSLET_KIND_GIX, "C:/ICONS/METALET.ICO", OSLET_APP_FLAG_NONE);

static void *Form1 = 0;

// Controls for Form1
static gui_control_t Form1_controls[] = {
    { .type = CTRL_PICTUREBOX, .x = 2, .y = 0, .w = 296, .h = 82, .fg = 0, .bg = 7, .text = "C:/OSLET/BANNER.BMP", .id = 1, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_BUTTON, .x = 228, .y = 127, .w = 65, .h = 22, .fg = 0, .bg = -1, .text = "OK", .id = 2, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_LABEL, .x = 7, .y = 89, .w = 0, .h = 0, .fg = 0, .bg = -1, .text = "", .id = 3, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_LABEL, .x = 7, .y = 105, .w = 0, .h = 0, .fg = 0, .bg = -1, .text = "This project is licensed under GPL3.", .id = 4, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_LABEL, .x = 7, .y = 129, .w = 0, .h = 0, .fg = 0, .bg = -1, .text = "", .id = 5, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 }
};

static int Form1_handle_event(void *form, int event, void *userdata) {
    (void)userdata; (void)form;
    if (event == 2) 
        return 1;

    if (event == -2) {
        sys_win_draw(form);
        sys_win_force_full_redraw();
    }

    return 0;
}

static void Form1_init(void *form, void *userdata) {
    (void)userdata;
    Form1 = form;

    /* Fill dynamic labels: kernel version and memory resources percentage */
    sys_meminfo_t meminfo = {0};
    sys_get_meminfo(&meminfo);

    int percent_free = 0;
    if (meminfo.total_kb != 0) {
        percent_free = (int)((meminfo.free_kb * 100) / meminfo.total_kb);
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "osLET %s, EinarTheSad 2025-2026", (const char*)sys_version());
    ctrl_set_text(Form1, 3, buf);

    char buf2[64];
    snprintf(buf2, sizeof(buf2), "Memory: %d%% free", percent_free);
    ctrl_set_text(Form1, 5, buf2);
}

__attribute__((section(".entry"), used))
void _start(void) {
    static gix_app_desc_t app = {
        .title = "About osLET",
        .icon_path = "C:/ICONS/METALET.ICO",
        .x = 222,
        .y = 122,
        .w = 300,
        .h = 175,
        .resizable = 0,
        .controls = Form1_controls,
        .control_count = 5,
        .on_init = Form1_init,
        .on_event = Form1_handle_event
    };

    gix_app_run(&app);
}
