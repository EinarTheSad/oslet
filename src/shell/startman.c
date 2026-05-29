#include "progmod.h"
#include "progman.h"
#include "../syscall.h"
#include "../win/wm_config.h"
#include "../lib/string.h"
#include "../lib/elf.h"
#include "../lib/app.h"

#define CTRL_APP_BASE 100
#define CTRL_BACK_BUTTON 99
#define CTRL_START_SCROLLBAR 98

#define ICON_W 48
#define ICON_H 58
#define PADDING_X 12
#define PADDING_Y 8
#define SCROLLBAR_W 18
#define SCROLLBAR_PAD 1
#define COLS 2
#define COLS_CHILD 4

#define MAIN_WIDTH 131
#define MAIN_HEIGHT 348
#define WIN_X 17
#define WIN_Y 89

#define SUBWIN_MAX_WIDTH 330
#define SUBWIN_MAX_HEIGHT 200
#define SUBWIN_X 150
#define SUBWIN_Y 89
#define CASCADE_OFFSET_X 40
#define CASCADE_OFFSET_Y 40

#define MAIN_GRP_PATH "C:/OSLET/START/MAIN.GRP"
#define MAX_APPS 32

#define ENTRY_ELF 0
#define ENTRY_GRP 1
#define ENTRY_MODULE 2

typedef struct {
    char name[32];       /* Display name */
    char path[64];       /* Full path to ELF/GRP or module name */
    char icon_path[64];  /* Icon path (from GRP or default) */
    int type;            /* ENTRY_ELF, ENTRY_GRP, or ENTRY_MODULE */
} app_entry_t;

typedef struct {
    void *form;
    int app_count;
    app_entry_t apps[MAX_APPS];
    char grp_path[64];
    char grp_title[64];
    int is_main_window;
    int scroll_offset;
} startman_state_t;

static char g_pending_grp_path[64] = {0};

static int startman_init(prog_instance_t *inst);
static int startman_event(prog_instance_t *inst, int win_idx, int event);
static void startman_cleanup(prog_instance_t *inst);

const progmod_t startman_module = {
    .name = "Start Manager",
    .icon_path = "C:/ICONS/EXE.ICO",
    .init = startman_init,
    .update = 0,
    .handle_event = startman_event,
    .cleanup = startman_cleanup,
    .flags = 0  /* Allow multiple instances for group windows */
};

static void title_case(char *dst, const char *src, int max_len) {
    int i = 0;
    int capitalize_next = 1;

    while (*src && i < max_len - 1) {
        if (*src == '_') {
            dst[i++] = ' ';
            capitalize_next = 1;
        } else {
            dst[i++] = capitalize_next ? toupper(*src) : tolower(*src);
            capitalize_next = 0;
        }
        src++;
    }
    dst[i] = '\0';
}

static int is_grp_file(const char *name) {
    int len = strlen(name);
    if (len < 5) return 0;  /* Need at least "x.grp" */

    const char *ext = name + len - 4;
    return (ext[0] == '.' &&
            (ext[1] == 'G' || ext[1] == 'g') &&
            (ext[2] == 'R' || ext[2] == 'r') &&
            (ext[3] == 'P' || ext[3] == 'p'));
}

/* Trim leading/trailing spaces/tabs in-place */
static void trim_inplace(char *s) {
    char *p = s;
    /* Trim leading whitespace */
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) {
        char *dst = s;
        while (*p) *dst++ = *p++;
        *dst = '\0';
    }
    /* Trim trailing whitespace */
    int len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) s[--len] = '\0';
}

static void sort_apps(app_entry_t *apps, int count) {
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (strcasecmp(apps[j].name, apps[j + 1].name) > 0) {
                app_entry_t tmp = apps[j];
                apps[j] = apps[j + 1];
                apps[j + 1] = tmp;
            }
        }
    }
}

static void extract_filename(char *dst, const char *path, int max_len) {
    const char *last_slash = path;
    const char *p = path;

    /* Find last slash */
    while (*p) {
        if (*p == '/' || *p == '\\')
            last_slash = p + 1;
        p++;
    }

    /* Copy filename */
    strncpy(dst, last_slash, max_len - 1);
    dst[max_len - 1] = '\0';
}

/* GRP format: one entry per line
 *   C:/PATH/FILE.ELF                       - ELF with default icon
 *   C:/PATH/FILE.ELF|C:/ICON.ICO           - ELF with custom icon
 *   C:/PATH/FILE.ELF||Display Name         - ELF with default icon and custom display name
 *   C:/PATH/FILE.ELF|C:/ICON.ICO|Display Name - ELF with custom icon and custom display name
 *   C:/PATH/FILE.GRP                       - GRP with default icon
 *   C:/PATH/FILE.GRP|C:/ICON.ICO           - GRP with custom icon
 *   !ModuleName                            - Internal module with default icon
 *   !ModuleName|C:/ICON.ICO                - Internal module with custom icon
 *   !ModuleName||Display Name              - Internal module with custom display name
 * Lines starting with ; or # are comments
 * Empty lines are ignored
 */
static int load_grp(startman_state_t *state, const char *grp_path) {
    int fd = sys_open(grp_path, "r");
    if (fd < 0)
        return 0;

    char buf[1024];
    int bytes = sys_read(fd, buf, sizeof(buf) - 1);
    sys_close(fd);

    if (bytes <= 0)
        return 0;

    buf[bytes] = '\0';

    int app_count = 0;
    char *line = buf;

    while (*line && app_count < MAX_APPS) {
        /* Skip leading whitespace */
        while (*line == ' ' || *line == '\t')
            line++;

        /* Skip empty lines and comments */
        if (*line == '\0' || *line == '\r' || *line == '\n' ||
            *line == ';' || *line == '#') {
            while (*line && *line != '\n')
                line++;
            if (*line == '\n')
                line++;
            continue;
        }

        /* Extract entry until end of line */
        char entry[128];
        int i = 0;
        while (*line && *line != '\r' && *line != '\n' && i < 127) {
            entry[i++] = *line++;
        }
        entry[i] = '\0';

        /* Trim trailing whitespace */
        while (i > 0 && (entry[i-1] == ' ' || entry[i-1] == '\t'))
            entry[--i] = '\0';

        /* Skip to next line */
        while (*line && *line != '\n')
            line++;
        if (*line == '\n')
            line++;

        if (i == 0)
            continue;

        /* Parse entry: split by | for optional icon path and optional display name
         * Format:
         *   path                           - no icon, default name
         *   path|icon                      - custom icon, default name
         *   path||name                     - default icon, custom name
         *   path|icon|name                 - custom icon and custom name
         */
        app_entry_t *app = &state->apps[app_count];
        char *pipe1 = strchr(entry, '|');
        char *pipe2 = NULL;
        char path_part[64] = {0};
        char icon_part[64] = {0};
        char name_part[64] = {0};

        if (pipe1) {
            pipe2 = strchr(pipe1 + 1, '|');
            /* path part */
            int path_len = pipe1 - entry;
            if (path_len >= (int)sizeof(path_part))
                path_len = sizeof(path_part) - 1;
            strncpy(path_part, entry, path_len);
            path_part[path_len] = '\0';

            if (pipe2) {
                /* icon between pipe1 and pipe2 */
                int icon_len = pipe2 - (pipe1 + 1);
                if (icon_len >= (int)sizeof(icon_part))
                    icon_len = sizeof(icon_part) - 1;
                strncpy(icon_part, pipe1 + 1, icon_len);
                icon_part[icon_len] = '\0';

                /* name after pipe2 */
                strncpy(name_part, pipe2 + 1, sizeof(name_part) - 1);
                name_part[sizeof(name_part) - 1] = '\0';
            } else {
                /* only one pipe - icon (or empty) until end */
                strncpy(icon_part, pipe1 + 1, sizeof(icon_part) - 1);
                icon_part[sizeof(icon_part) - 1] = '\0';
            }
        } else {
            strncpy(path_part, entry, sizeof(path_part) - 1);
            path_part[sizeof(path_part) - 1] = '\0';
        }

        /* Trim whitespace from parts */
        trim_inplace(path_part);
        trim_inplace(icon_part);
        trim_inplace(name_part);

        /* Determine entry type */
        if (path_part[0] == '!') {
            /* Internal module */
            app->type = ENTRY_MODULE;
            if (name_part[0]) {
                strncpy(app->name, name_part, sizeof(app->name) - 1);
                app->name[sizeof(app->name) - 1] = '\0';
            } else {
                strncpy(app->name, path_part + 1, sizeof(app->name) - 1);
                app->name[sizeof(app->name) - 1] = '\0';
            }
            strcpy(app->path, app->name);
            if (icon_part[0])
                strcpy(app->icon_path, icon_part);
            else
                strcpy(app->icon_path, "C:/ICONS/EXE.ICO");
        } else {
            /* File path - extract filename for display name */
            strcpy(app->path, path_part);

            char filename[32];
            extract_filename(filename, path_part, sizeof(filename));

            oslet_app_info_t app_info;
            int have_app_info = 0;
            oslet_app_info_init(&app_info);
            if (!is_grp_file(filename) && oslet_app_read_info(path_part, &app_info) == 0) {
                have_app_info = 1;
            }

            /* Build display name (metadata, explicit GRP name, or filename) */
            if (name_part[0]) {
                strncpy(app->name, name_part, sizeof(app->name) - 1);
                app->name[sizeof(app->name) - 1] = '\0';
            } else if (have_app_info && app_info.name[0]) {
                strncpy(app->name, app_info.name, sizeof(app->name) - 1);
                app->name[sizeof(app->name) - 1] = '\0';
            } else {
                char raw_name[32];
                int name_len = strlen(filename);
                if (name_len > 4)
                    name_len -= 4;  /* Remove .ELF or .GRP */
                if (name_len >= (int)sizeof(raw_name))
                    name_len = sizeof(raw_name) - 1;
                for (int j = 0; j < name_len; j++)
                    raw_name[j] = filename[j];
                raw_name[name_len] = '\0';
                title_case(app->name, raw_name, sizeof(app->name));
            }

            /* Set type and default icon */
            if (is_grp_file(filename)) {
                app->type = ENTRY_GRP;
                if (icon_part[0])
                    strcpy(app->icon_path, icon_part);
                else
                    strcpy(app->icon_path, "C:/ICONS/GROUP.ICO");
            } else {
                app->type = ENTRY_ELF;
                if (icon_part[0])
                    strcpy(app->icon_path, icon_part);
                else if (have_app_info && app_info.icon_path[0])
                    strcpy(app->icon_path, app_info.icon_path);
                else
                    strcpy(app->icon_path, "C:/ICONS/EXE.ICO");
            }
        }

        app_count++;
    }

    state->app_count = app_count;
    sort_apps(state->apps, app_count);
    return app_count;
}

static char g_pending_grp_icon[64] = {0};
static char g_pending_grp_name[64] = {0};
static int g_pending_win_x = 0;
static int g_pending_win_y = 0;
static int g_main_geom_valid = 0;
static int g_main_win_x = WIN_X;
static int g_main_win_y = WIN_Y;
static int g_main_win_w = MAIN_WIDTH;
static int g_main_win_h = MAIN_HEIGHT;
static void rebuild_layout(startman_state_t *state);
static void open_group_window(const char *grp_path, const char *parent_grp, const char *icon_path, const char *display_name);

static int startman_total_icons(startman_state_t *state) {
    if (!state) return 0;
    return state->app_count + (state->is_main_window ? 0 : 1);
}

static int startman_total_rows(startman_state_t *state, int cols) {
    int total_icons = startman_total_icons(state);
    if (cols < 1) cols = 1;
    return (total_icons + cols - 1) / cols;
}

static int startman_scrollbar_x(int win_w) {
    return win_w - WM_FRAME_WIDTH - SCROLLBAR_PAD - SCROLLBAR_W;
}

static int startman_scrollbar_h(int win_h) {
    int h = win_h - WM_TITLEBAR_HEIGHT - (WM_FRAME_WIDTH * 2) - (SCROLLBAR_PAD * 2);
    if (h < SCROLLBAR_W * 3)
        h = SCROLLBAR_W * 3;
    return h;
}

static int startman_visible_rows(int win_h) {
    int client_h = win_h - WM_TITLEBAR_HEIGHT - (WM_FRAME_WIDTH * 2) - (SCROLLBAR_PAD * 2);
    int row_step = ICON_H + PADDING_Y;
    int rows = client_h / row_step;
    if (rows < 1) rows = 1;
    return rows;
}

static int startman_columns(int win_w, int use_scrollbar) {
    int right_edge = use_scrollbar
        ? startman_scrollbar_x(win_w) - SCROLLBAR_PAD
        : win_w - WM_FRAME_WIDTH - SCROLLBAR_PAD;
    int content_w = right_edge - PADDING_X;

    int cols = (content_w + PADDING_X) / (ICON_W + PADDING_X);
    if (cols < 1) cols = 1;
    if (cols > 10) cols = 10;
    return cols;
}

static void save_main_geometry(startman_state_t *state) {
    if (!state || !state->is_main_window || !state->form)
        return;

    gui_form_t *form = (gui_form_t *)state->form;
    window_t *win = &form->win;
    if (win->is_maximized && win->saved_w > 0 && win->saved_h > 0) {
        g_main_win_x = win->saved_x;
        g_main_win_y = win->saved_y;
        g_main_win_w = win->saved_w;
        g_main_win_h = win->saved_h;
    } else {
        g_main_win_x = win->x;
        g_main_win_y = win->y;
        g_main_win_w = win->w;
        g_main_win_h = win->h;
    }
    g_main_geom_valid = 1;
}

static int subwindow_slot_used(int slot) {
    int x = SUBWIN_X + slot * CASCADE_OFFSET_X;
    int y = SUBWIN_Y + slot * CASCADE_OFFSET_Y;

    for (int i = 0; i < PROGMAN_INSTANCES_MAX; i++) {
        prog_instance_t *other = progman_get_instance(i);
        if (!other || other->state != PROG_STATE_RUNNING || !other->module ||
            strcmp(other->module->name, "Start Manager") != 0 || !other->user_data)
            continue;

        startman_state_t *other_state = other->user_data;
        if (other_state->is_main_window || !other_state->form)
            continue;

        gui_form_t *form = (gui_form_t *)other_state->form;
        if (form->win.x == x && form->win.y == y)
            return 1;
    }

    return 0;
}

static void set_next_subwindow_position(void) {
    for (int slot = 0; slot < PROGMAN_INSTANCES_MAX; slot++) {
        if (!subwindow_slot_used(slot)) {
            g_pending_win_x = SUBWIN_X + slot * CASCADE_OFFSET_X;
            g_pending_win_y = SUBWIN_Y + slot * CASCADE_OFFSET_Y;
            return;
        }
    }

    g_pending_win_x = SUBWIN_X + PROGMAN_INSTANCES_MAX * CASCADE_OFFSET_X;
    g_pending_win_y = SUBWIN_Y + PROGMAN_INSTANCES_MAX * CASCADE_OFFSET_Y;
}

static int startman_init(prog_instance_t *inst) {
    startman_state_t *state = sys_malloc(sizeof(startman_state_t));
    if (!state)
        return -1;

    inst->user_data = state;
    state->app_count = 0;
    state->grp_path[0] = '\0';
    state->scroll_offset = 0;

    /* Check if we have a pending GRP path to open */
    if (g_pending_grp_path[0]) {
        /* This is a subgroup window */
        strcpy(state->grp_path, g_pending_grp_path);
        g_pending_grp_path[0] = '\0';  /* Clear for next use */
        if (g_pending_grp_name[0]) {
            strncpy(state->grp_title, g_pending_grp_name, sizeof(state->grp_title) - 1);
            state->grp_title[sizeof(state->grp_title) - 1] = '\0';
            g_pending_grp_name[0] = '\0';
        } else {
            state->grp_title[0] = '\0';
        }
        state->is_main_window = 0;
    } else {
        /* Main window - load from MAIN.GRP */
        strcpy(state->grp_path, MAIN_GRP_PATH);
        state->grp_title[0] = '\0';
        state->is_main_window = 1;
    }

    load_grp(state, state->grp_path);

    /* Build window title */
    char title[32];
    if (state->is_main_window) {
        strcpy(title, "Start Manager");
    } else {
        /* If a custom title was provided in the GRP, use it directly */
        if (state->grp_title[0]) {
            strncpy(title, state->grp_title, sizeof(title) - 1);
            title[sizeof(title) - 1] = '\0';
        } else {
            char grp_name[32];
            extract_filename(grp_name, state->grp_path, sizeof(grp_name));
            /* Remove .GRP extension */
            int len = strlen(grp_name);
            if (len > 4)
                grp_name[len - 4] = '\0';

            title_case(title, grp_name, sizeof(title));
        }
    }

    /* Calculate window position and size */
    int win_x = WIN_X;
    int win_y = WIN_Y;
    int win_w = MAIN_WIDTH;
    int win_h = MAIN_HEIGHT;

    int icon_offset = 0;
    int cols = state->is_main_window ? COLS : COLS_CHILD;

    if (state->is_main_window) {
        if (g_main_geom_valid) {
            win_x = g_main_win_x;
            win_y = g_main_win_y;
            win_w = g_main_win_w;
            win_h = g_main_win_h;
        }
    } else {
        /* Subgroup windows: use cascade position */
        win_x = g_pending_win_x;
        win_y = g_pending_win_y;

        /* +1 for ".." back button */
        int total_icons = state->app_count + 1;
        icon_offset = 1;

        /* Calculate rows needed */
        int rows = (total_icons + cols - 1) / cols;
        if (rows < 1) rows = 1;

        /* Dynamic height based on content */
        win_h = PADDING_Y + rows * (ICON_H + PADDING_Y) + 30;  /* +30 for title bar */
        if (win_h < 100) win_h = 100;
        if (win_h > SUBWIN_MAX_HEIGHT) win_h = SUBWIN_MAX_HEIGHT;

        /* Dynamic width if few icons */
        int cols_needed = (total_icons < cols) ? total_icons : cols;
        if (cols_needed < 1) cols_needed = 1;
        win_w = PADDING_X + cols_needed * (ICON_W + PADDING_X) + PADDING_X;
        if (win_w < 120) win_w = 120;
        if (win_w > SUBWIN_MAX_WIDTH) win_w = SUBWIN_MAX_WIDTH;
    }

    state->form = sys_win_create_form(title, win_x, win_y, win_w, win_h);
    if (!state->form) {
        sys_free(state);
        return -1;
    }
    /* Set form icon: subgroup windows may override with pending group icon */
    if (!state->is_main_window && g_pending_grp_icon[0]) {
        sys_win_set_icon(state->form, g_pending_grp_icon);
        g_pending_grp_icon[0] = '\0';
    } else {
        sys_win_set_icon(state->form, "C:/ICONS/GROUP.ICO");
    }

    /* Add back button only for subgroup windows */
    if (!state->is_main_window) {
        gui_control_t back = {0};
        back.type = CTRL_ICON;
        back.x = PADDING_X;
        back.y = PADDING_Y;
        back.w = ICON_W;
        back.h = ICON_H;
        back.fg = 0;
        back.bg = 15;
        back.id = CTRL_BACK_BUTTON;
        strcpy(back.text, "..");
        sys_win_add_control(state->form, &back);
        ctrl_set_image(state->form, back.id, "C:/ICONS/BACK.ICO");
    }

    /* Add icons for each app */
    for (int i = 0; i < state->app_count; i++) {
        int pos = i + icon_offset;
        int col = pos % cols;
        int row = pos / cols;
        int x = PADDING_X + col * (ICON_W + PADDING_X);
        int y = PADDING_Y + row * (ICON_H + PADDING_Y);

        gui_control_t icon = {0};
        icon.type = CTRL_ICON;
        icon.x = x;
        icon.y = y;
        icon.w = ICON_W;
        icon.h = ICON_H;
        icon.fg = 0;
        icon.bg = 15;
        icon.id = CTRL_APP_BASE + i;

        strncpy(icon.text, state->apps[i].name, sizeof(icon.text) - 1);
        sys_win_add_control(state->form, &icon);

        /* Set icon */
        ctrl_set_image(state->form, icon.id, state->apps[i].icon_path);
    }

    gui_control_t scrollbar = {0};
    scrollbar.type = CTRL_SCROLLBAR;
    scrollbar.x = startman_scrollbar_x(win_w);
    scrollbar.y = 1;
    scrollbar.w = SCROLLBAR_W;
    scrollbar.h = startman_scrollbar_h(win_h);
    scrollbar.fg = 8;
    scrollbar.bg = 7;
    scrollbar.id = CTRL_START_SCROLLBAR;
    scrollbar.font_size = 12;
    sys_win_add_control(state->form, &scrollbar);
    ctrl_set_visible(state->form, CTRL_START_SCROLLBAR, 0);

    rebuild_layout(state);
    sys_win_draw(state->form);
    prog_register_window(inst, state->form);
    return 0;
}

static void rebuild_layout(startman_state_t *state) {
    if (!state || !state->form) return;

    gui_form_t *f = (gui_form_t *)state->form;
    int win_w = f->win.w;
    int win_h = f->win.h;

    sys_win_invalidate_icons();

    int visible_rows = startman_visible_rows(win_h);
    int cols = startman_columns(win_w, 0);
    int total_rows = startman_total_rows(state, cols);
    int needs_scrollbar = total_rows > visible_rows;
    if (needs_scrollbar) {
        cols = startman_columns(win_w, 1);
        total_rows = startman_total_rows(state, cols);
        needs_scrollbar = total_rows > visible_rows;
    }

    int max_scroll = needs_scrollbar ? total_rows - visible_rows : 0;
    if (state->scroll_offset > max_scroll) state->scroll_offset = max_scroll;
    if (state->scroll_offset < 0) state->scroll_offset = 0;

    int icon_offset = state->is_main_window ? 0 : 1;

    if (f->controls) {
        for (int ctrl_idx = 0; ctrl_idx < f->ctrl_count; ctrl_idx++) {
            gui_control_t *ctrl = &f->controls[ctrl_idx];

            if (ctrl->id == CTRL_BACK_BUTTON) {
                int row = 0 - state->scroll_offset;
                ctrl->x = PADDING_X;
                ctrl->y = PADDING_Y + row * (ICON_H + PADDING_Y);
                ctrl_set_visible(state->form, ctrl->id, row >= 0 && row < visible_rows);
            } else if (ctrl->id >= CTRL_APP_BASE) {
                int pos = (ctrl->id - CTRL_APP_BASE) + icon_offset;
                int col = pos % cols;
                int row = (pos / cols) - state->scroll_offset;
                ctrl->x = PADDING_X + col * (ICON_W + PADDING_X);
                ctrl->y = PADDING_Y + row * (ICON_H + PADDING_Y);
                ctrl_set_visible(state->form, ctrl->id, row >= 0 && row < visible_rows);
            } else if (ctrl->id == CTRL_START_SCROLLBAR) {
                ctrl->x = startman_scrollbar_x(win_w);
                ctrl->y = 1;
                ctrl->w = SCROLLBAR_W;
                ctrl->h = startman_scrollbar_h(win_h);
                ctrl->scrollbar.max_length = max_scroll > 0 ? max_scroll : 1;
                ctrl->scrollbar.cursor_pos = state->scroll_offset;
                ctrl_set_visible(state->form, ctrl->id, needs_scrollbar);
            }
        }
    }
}

static int startman_event(prog_instance_t *inst, int win_idx, int event) {
    (void)win_idx;
    startman_state_t *state = inst->user_data;

    /* Handle window events */
    if (event == -1 || event == -2) {
        if (event == -2)
            save_main_geometry(state);
        return PROG_EVENT_REDRAW;
    }
    
    /* Handle resize event */
    if (event == -4) {
        rebuild_layout(state);
        save_main_geometry(state);
        return PROG_EVENT_REDRAW;
    }

    if (event == CTRL_START_SCROLLBAR && state) {
        gui_control_t *scrollbar = sys_win_get_control(state->form, CTRL_START_SCROLLBAR);
        if (scrollbar) {
            state->scroll_offset = scrollbar->scrollbar.cursor_pos;
            rebuild_layout(state);
            sys_win_draw(state->form);
            return PROG_EVENT_HANDLED;
        }
        return PROG_EVENT_HANDLED;
    }

    /* Handle back button - only for child windows */
    if (event == CTRL_BACK_BUTTON && state && !state->is_main_window) {
        /* Check if main Start Manager window is already open */
        int main_exists = 0;
        for (int i = 0; i < PROGMAN_INSTANCES_MAX; i++) {
            prog_instance_t *other = progman_get_instance(i);
            if (other && other->state == PROG_STATE_RUNNING && other->module &&
                strcmp(other->module->name, "Start Manager") == 0 &&
                other->user_data) {
                startman_state_t *other_state = other->user_data;
                if (other_state->is_main_window) {
                    main_exists = 1;
                    break;
                }
            }
        }

        /* Only launch new main window if one doesn't already exist */
        if (!main_exists) {
            g_pending_grp_path[0] = '\0';  /* Clear to open as main window */
            progman_launch("Start Manager");
        }
        return PROG_EVENT_CLOSE;
    }

    /* Handle icon double-clicks */
    if (event >= CTRL_APP_BASE && state) {
        int app_index = event - CTRL_APP_BASE;
        if (app_index >= 0 && app_index < state->app_count) {
            app_entry_t *app = &state->apps[app_index];
            if (app->type == ENTRY_GRP) {
                /* Open group in new window, pass display name if provided */
                open_group_window(app->path, state->grp_path, app->icon_path, app->name);
            } else if (app->type == ENTRY_MODULE) {
                /* Launch internal module (propagate icon override) */
                progman_launch_with_icon(app->path, app->icon_path);
            } else {
                int child_tid = 0;
                oslet_launch_program(app->path,
                                     "",
                                     OSLET_LAUNCH_FROM_GIX,
                                     app->icon_path,
                                     &child_tid);
            }
        }
        return PROG_EVENT_HANDLED;
    }

    return PROG_EVENT_NONE;
}

static void startman_cleanup(prog_instance_t *inst) {
    startman_state_t *state = inst->user_data;
    if (state) {
        if (state->form) {
            save_main_geometry(state);
            prog_unregister_window(inst, state->form);
            sys_win_destroy_form(state->form);
        }
        sys_free(state);
        inst->user_data = 0;
    }
}

/* Open a new group window by launching a new startman instance */
static void open_group_window(const char *grp_path, const char *parent_grp, const char *icon_path, const char *display_name) {
    (void)parent_grp;
    /* Store GRP path, icon, and optional display name for the new instance to pick up */
    strncpy(g_pending_grp_path, grp_path, sizeof(g_pending_grp_path) - 1);
    g_pending_grp_path[sizeof(g_pending_grp_path) - 1] = '\0';
    if (icon_path && icon_path[0]) {
        strncpy(g_pending_grp_icon, icon_path, sizeof(g_pending_grp_icon) - 1);
        g_pending_grp_icon[sizeof(g_pending_grp_icon) - 1] = '\0';
    } else {
        g_pending_grp_icon[0] = '\0';
    }
    if (display_name && display_name[0]) {
        strncpy(g_pending_grp_name, display_name, sizeof(g_pending_grp_name) - 1);
        g_pending_grp_name[sizeof(g_pending_grp_name) - 1] = '\0';
    } else {
        g_pending_grp_name[0] = '\0';
    }
    /* Calculate cascade position for this new subwindow */
    set_next_subwindow_position();

    /* Launch new startman instance */
    progman_launch("Start Manager");
}
