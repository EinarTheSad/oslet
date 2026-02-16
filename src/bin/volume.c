#include "../bin/progmod.h"
#include "../bin/progman.h"
#include "../syscall.h"
#include "../lib/string.h"
#include "../lib/stdio.h"
#include "../lib/stdlib.h"

#define CTRL_FRAME_ID      2
#define CTRL_SCROLLBAR_ID  3
#define CTRL_LABEL_TOP     4
#define CTRL_LABEL_MID     6
#define CTRL_LABEL_BOTTOM  5

typedef struct {
    void *form;
    uint8_t volume; /* effective volume 0..100 (0 = muted) */
} volume_state_t;

static int volume_init(prog_instance_t *inst);
static int volume_event(prog_instance_t *inst, int win_idx, int event);
static void volume_cleanup(prog_instance_t *inst);

const progmod_t volume_module = {
    .name = "Volume",
    .icon_path = "C:/ICONS/SOUND.ICO",
    .init = volume_init,
    .update = 0,
    .handle_event = volume_event,
    .cleanup = volume_cleanup,
    .flags = PROG_FLAG_SINGLETON
};

static void update_volume_display(volume_state_t *state) {
    char buf[16];

    /* Middle marker shows effective volume */
    snprintf(buf, sizeof(buf), "%d", state->volume);
    ctrl_set_text(state->form, CTRL_LABEL_MID, buf);

    /* Put scrollbar in inverted position */
    gui_control_t *sb = sys_win_get_control(state->form, CTRL_SCROLLBAR_ID);
    if (sb) sb->cursor_pos = 100 - state->volume;

    sys_win_draw(state->form);
}

static void apply_volume(volume_state_t *state) {
    uint8_t hw_vol;

    if (state->volume == 0) {
        hw_vol = 0; /* muted */
    } else {
        /* scale 1..100 -> 1..31 */
        hw_vol = (state->volume * 31 + 50) / 100; /* rounded */
        if (hw_vol == 0) hw_vol = 1; /* keep audible volumes >0 */
    }

    sys_sound_set_volume(hw_vol, hw_vol);
}

static int volume_init(prog_instance_t *inst) {
    volume_state_t *state = malloc(sizeof(volume_state_t));
    if (!state) return -1;

    inst->user_data = state;

    /* default: 66; if SB16 present, read actual hardware volume */
    state->volume = 66;
    if (sys_sound_detected()) {
        uint32_t packed = sys_sound_get_volume();
        uint8_t left = (packed >> 8) & 0xFF;
        uint8_t right = packed & 0xFF;
        /* clamp 5-bit values */
        left &= 0x1F; right &= 0x1F;
        uint8_t avg = (left + right + 1) / 2;
        /* Map 0..31 -> 0..100 */
        state->volume = (avg * 100 + 15) / 31;
    }

    state->form = sys_win_create_form("Sound", 555, 278, 85, 175);
    if (!state->form) {
        free(state);
        return -1;
    }

    prog_register_window(inst, state->form);

    gui_control_t controls[] = {
        { .type = CTRL_FRAME,     .x = 6,  .y = 5,  .w = 73, .h = 140, .fg = 0, .bg = 7,  .text = "Volume", .id = CTRL_FRAME_ID,     .font_type = 0, .font_size = 12, .border = 0, .border_color = 0, .cached_bitmap_orig = NULL, .cached_bitmap_scaled = NULL, .pressed = 0, .checked = 0, .group_id = 0, .cursor_pos = 0, .max_length = 0, .scroll_offset = 0, .is_focused = 0, .sel_start = -1, .sel_end = -1, .dropdown_open = 0, .item_count = 0, .hovered_item = -1 },
        { .type = CTRL_SCROLLBAR, .x = 31, .y = 24, .w = 18, .h = 114, .fg = 0, .bg = 7,  .text = "",       .id = CTRL_SCROLLBAR_ID, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0, .cached_bitmap_orig = NULL, .cached_bitmap_scaled = NULL, .pressed = 0, .checked = 0, .group_id = 0, .cursor_pos = 0, .max_length = 100, .scroll_offset = 0, .is_focused = 0, .sel_start = -1, .sel_end = -1, .dropdown_open = 0, .item_count = 0, .hovered_item = -1 },
        { .type = CTRL_LABEL,     .x = 55, .y = 24, .w = 0,  .h = 0,   .fg = 0, .bg = -1, .text = "100",     .id = CTRL_LABEL_TOP,    .font_type = 0, .font_size = 10, .border = 0, .border_color = 0, .cached_bitmap_orig = NULL, .cached_bitmap_scaled = NULL, .pressed = 0, .checked = 0, .group_id = 0, .cursor_pos = 0, .max_length = 0, .scroll_offset = 0, .is_focused = 0, .sel_start = -1, .sel_end = -1, .dropdown_open = 0, .item_count = 0, .hovered_item = -1 },
        { .type = CTRL_LABEL,     .x = 55, .y = 72, .w = 0,  .h = 0,   .fg = 0, .bg = -1, .text = "100",     .id = CTRL_LABEL_MID,    .font_type = 0, .font_size = 10, .border = 0, .border_color = 0, .cached_bitmap_orig = NULL, .cached_bitmap_scaled = NULL, .pressed = 0, .checked = 0, .group_id = 0, .cursor_pos = 0, .max_length = 0, .scroll_offset = 0, .is_focused = 0, .sel_start = -1, .sel_end = -1, .dropdown_open = 0, .item_count = 0, .hovered_item = -1 },
        { .type = CTRL_LABEL,     .x = 55, .y = 120, .w = 0, .h = 0,   .fg = 0, .bg = -1, .text = "0",       .id = CTRL_LABEL_BOTTOM, .font_type = 0, .font_size = 10, .border = 0, .border_color = 0, .cached_bitmap_orig = NULL, .cached_bitmap_scaled = NULL, .pressed = 0, .checked = 0, .group_id = 0, .cursor_pos = 0, .max_length = 0, .scroll_offset = 0, .is_focused = 0, .sel_start = -1, .sel_end = -1, .dropdown_open = 0, .item_count = 0, .hovered_item = -1 }
    };

    for (int i = 0; i < (int)(sizeof(controls)/sizeof(controls[0])); ++i) {
        sys_win_add_control(state->form, &controls[i]);
    }

    /* reflect initial state in UI and hardware */
    update_volume_display(state);
    apply_volume(state);
    sys_win_draw(state->form);

    return 0;
}

static int volume_event(prog_instance_t *inst, int win_idx, int event) {
    (void)win_idx;

    if (event == -1 || event == -2) return PROG_EVENT_REDRAW;

    volume_state_t *state = (volume_state_t*)inst->user_data;
    if (!state) return PROG_EVENT_NONE;

    gui_form_t *form = (gui_form_t*)state->form;
    int clicked_id = form->clicked_id;

    if (clicked_id == CTRL_SCROLLBAR_ID) {
        gui_control_t *sb = sys_win_get_control(state->form, CTRL_SCROLLBAR_ID);
        if (!sb) return PROG_EVENT_NONE;

        /* inverted mapping: scrollbar=0 -> volume=100, scrollbar=100 -> volume=0 */
        int inv = 100 - sb->cursor_pos;
        if (inv < 0) inv = 0;
        if (inv > 100) inv = 100;

        state->volume = (uint8_t)inv;
        update_volume_display(state);
        apply_volume(state);
        if (sb->pressed == 0) {
            sys_sound_play_wav("C:/SOUNDS/DING.WAV");
        }
        return PROG_EVENT_HANDLED;
    }

    return PROG_EVENT_NONE;
}

static void volume_cleanup(prog_instance_t *inst) {
    if (!inst || !inst->user_data) return;

    volume_state_t *state = (volume_state_t*)inst->user_data;
    free(state);
    inst->user_data = 0;
}
