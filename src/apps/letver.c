#include "../syscall.h"
#include "../lib/stdio.h"
#include "../lib/string.h"

static void *Form1 = 0;

// Controls for Form1
static gui_control_t Form1_controls[] = {
    { .type = CTRL_PICTUREBOX, .x = 2, .y = 0, .w = 296, .h = 82, .fg = 0, .bg = 7, .text = "C:/OSLET/BANNER.BMP", .id = 1, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0, .cached_bitmap_orig = NULL, .cached_bitmap_scaled = NULL, .pressed = 0, .checked = 0, .group_id = 0, .cursor_pos = 0, .max_length = 0, .scroll_offset = 0, .is_focused = 0, .sel_start = -1, .sel_end = -1, .dropdown_open = 0, .item_count = 0, .hovered_item = -1 },
    { .type = CTRL_BUTTON, .x = 228, .y = 127, .w = 65, .h = 22, .fg = 0, .bg = -1, .text = "OK", .id = 2, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0, .cached_bitmap_orig = NULL, .cached_bitmap_scaled = NULL, .pressed = 0, .checked = 0, .group_id = 0, .cursor_pos = 0, .max_length = 0, .scroll_offset = 0, .is_focused = 0, .sel_start = -1, .sel_end = -1, .dropdown_open = 0, .item_count = 0, .hovered_item = -1 },
    { .type = CTRL_LABEL, .x = 7, .y = 89, .w = 0, .h = 0, .fg = 0, .bg = -1, .text = "", .id = 3, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0, .cached_bitmap_orig = NULL, .cached_bitmap_scaled = NULL, .pressed = 0, .checked = 0, .group_id = 0, .cursor_pos = 0, .max_length = 0, .scroll_offset = 0, .is_focused = 0, .sel_start = -1, .sel_end = -1, .dropdown_open = 0, .item_count = 0, .hovered_item = -1 },
    { .type = CTRL_LABEL, .x = 7, .y = 105, .w = 0, .h = 0, .fg = 0, .bg = -1, .text = "This project is licensed under GPL3.", .id = 4, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0, .cached_bitmap_orig = NULL, .cached_bitmap_scaled = NULL, .pressed = 0, .checked = 0, .group_id = 0, .cursor_pos = 0, .max_length = 0, .scroll_offset = 0, .is_focused = 0, .sel_start = -1, .sel_end = -1, .dropdown_open = 0, .item_count = 0, .hovered_item = -1 },
    { .type = CTRL_LABEL, .x = 7, .y = 129, .w = 0, .h = 0, .fg = 0, .bg = -1, .text = "", .id = 5, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0, .cached_bitmap_orig = NULL, .cached_bitmap_scaled = NULL, .pressed = 0, .checked = 0, .group_id = 0, .cursor_pos = 0, .max_length = 0, .scroll_offset = 0, .is_focused = 0, .sel_start = -1, .sel_end = -1, .dropdown_open = 0, .item_count = 0, .hovered_item = -1 }
};

static int Form1_handle_event(void *form, int event, void *userdata) {
    (void)userdata;
    if (event == -1 || event == -2) {
        sys_win_restore_form(form);
        return 0;
    }
    if (event > 0) {
        switch (event) {
            case 2: /* OK */
                return 1;
                break;
        }
    }
    return 0;
}

__attribute__((section(".entry"), used))
void _start(void) {
    Form1 = sys_win_create_form("About osLET", 222, 122, 300, 175);
    if (!Form1) {
        sys_exit();
        return;
    }
    sys_win_set_icon(Form1, "C:/ICONS/METALET.ICO");
    for (int i = 0; i < 5; i++) {
        sys_win_add_control(Form1, &Form1_controls[i]);
    }
    /* Fill dynamic labels: kernel version and memory resources percentage */
    sys_meminfo_t meminfo;
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

    sys_win_draw(Form1);

    sys_win_force_full_redraw();

    sys_win_run_event_loop(Form1, Form1_handle_event, NULL);

    sys_win_destroy_form(Form1);
    sys_exit();
}