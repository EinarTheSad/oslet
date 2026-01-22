#include "progmod.h"
#include "progman.h"
#include "../syscall.h"
#include "../lib/string.h"
#include "../lib/ini.h"

#define CTRL_APP_BASE 100
#define CTRL_BACK_BUTTON 99

#define ICON_W 48
#define ICON_H 58
#define PADDING_X 12
#define PADDING_Y 8
#define COLS 5

#define WIN_WIDTH 330
#define WIN_HEIGHT 200
#define WIN_X 65
#define WIN_Y 80

#define INI_PATH "C:/OSLET/START/STARTMAN.INI"
#define MAIN_GRP_PATH "C:/OSLET/START/MAIN.GRP"
#define MAX_APPS 32

/* Entry types */
#define ENTRY_ELF 0
#define ENTRY_GRP 1

/* App entry from filesystem scan */
typedef struct {
    char name[32];       /* Display name (without extension) */
    char path[64];       /* Full path to ELF or GRP */
    char elf_name[32];   /* Original filename (uppercase) for INI lookup */
    char icon_path[64];  /* Icon path from INI or default */
    int type;            /* ENTRY_ELF or ENTRY_GRP */
} app_entry_t;

typedef struct {
    void *form;
    int app_count;
    app_entry_t apps[MAX_APPS];
    char grp_path[64];
    int is_main_window;  /* 1 if this is the main Start Manager window */
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

static int parse_ini(const char *elf_name, char *icon_path, int max_len) {
    int fd = sys_open(INI_PATH, "r");
    if (fd < 0)
        return 0;

    char buf[512];
    int bytes = sys_read(fd, buf, sizeof(buf) - 1);
    sys_close(fd);

    if (bytes <= 0)
        return 0;

    buf[bytes] = '\0';

    ini_parser_t ini;
    ini_init(&ini, buf);

    const char *val = ini_get(&ini, "ICONS", elf_name);
    if (val) {
        strncpy(icon_path, val, max_len - 1);
        icon_path[max_len - 1] = '\0';
        return 1;
    }
    return 0;
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

static void elf_to_upper(char *dst, const char *src, int max_len) {
    strncpy(dst, src, max_len - 1);
    dst[max_len - 1] = '\0';
    str_toupper(dst);
}

/* Simple bubble sort for app entries (alphabetical by display name) */
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

/* Extract filename from path for display */
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

/* Parse GRP file and load entries
 * GRP format: one path per line (ELF or GRP files)
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

        /* Extract path until end of line */
        char path[64];
        int i = 0;
        while (*line && *line != '\r' && *line != '\n' && i < 63) {
            path[i++] = *line++;
        }
        path[i] = '\0';

        /* Trim trailing whitespace */
        while (i > 0 && (path[i-1] == ' ' || path[i-1] == '\t'))
            path[--i] = '\0';

        /* Skip to next line */
        while (*line && *line != '\n')
            line++;
        if (*line == '\n')
            line++;

        if (i == 0)
            continue;

        /* Determine entry type and add to list */
        app_entry_t *app = &state->apps[app_count];
        strcpy(app->path, path);

        /* Extract filename for INI lookup */
        char filename[32];
        extract_filename(filename, path, sizeof(filename));
        elf_to_upper(app->elf_name, filename, sizeof(app->elf_name));

        /* Extract display name (remove extension, apply title case) */
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

        /* Set type and icon */
        if (is_grp_file(filename)) {
            app->type = ENTRY_GRP;
            strcpy(app->icon_path, "C:/ICONS/GROUP.ICO");
        } else {
            app->type = ENTRY_ELF;
            if (!parse_ini(app->elf_name, app->icon_path, sizeof(app->icon_path))) {
                strcpy(app->icon_path, "C:/ICONS/EXE.ICO");
            }
        }

        app_count++;
    }

    state->app_count = app_count;
    sort_apps(state->apps, app_count);
    return app_count;
}

/* Open a new group window */
static void open_group_window(const char *grp_path, const char *parent_grp);

static int startman_init(prog_instance_t *inst) {
    startman_state_t *state = sys_malloc(sizeof(startman_state_t));
    if (!state)
        return -1;

    inst->user_data = state;
    state->app_count = 0;
    state->grp_path[0] = '\0';

    /* Check if we have a pending GRP path to open */
    if (g_pending_grp_path[0]) {
        /* This is a subgroup window */
        strcpy(state->grp_path, g_pending_grp_path);
        g_pending_grp_path[0] = '\0';  /* Clear for next use */
        state->is_main_window = 0;
    } else {
        /* Main window - load from MAIN.GRP */
        strcpy(state->grp_path, MAIN_GRP_PATH);
        state->is_main_window = 1;
    }

    load_grp(state, state->grp_path);

    /* Build window title */
    char title[32];
    if (state->is_main_window) {
        strcpy(title, "Start Manager");
    } else {
        char grp_name[32];
        extract_filename(grp_name, state->grp_path, sizeof(grp_name));
        /* Remove .GRP extension */
        int len = strlen(grp_name);
        if (len > 4)
            grp_name[len - 4] = '\0';

        title_case(title, grp_name, sizeof(title));
    }

    /* Calculate window position and size */
    int win_x = WIN_X;
    int win_y = WIN_Y;
    int win_w = WIN_WIDTH;
    int win_h = WIN_HEIGHT;

    int icon_offset = 0;

    if (!state->is_main_window) {
        /* Subgroup windows: offset position and calculate dynamic size */
        win_x += 40;
        win_y += 40;

        /* +1 for ".." back button */
        int total_icons = state->app_count + 1;
        icon_offset = 1;

        /* Calculate rows needed */
        int rows = (total_icons + COLS - 1) / COLS;
        if (rows < 1) rows = 1;

        /* Dynamic height based on content */
        win_h = PADDING_Y + rows * (ICON_H + PADDING_Y) + 30;  /* +30 for title bar */
        if (win_h < 100) win_h = 100;
        if (win_h > WIN_HEIGHT) win_h = WIN_HEIGHT;

        /* Dynamic width if few icons */
        int cols_needed = (total_icons < COLS) ? total_icons : COLS;
        if (cols_needed < 1) cols_needed = 1;
        win_w = PADDING_X + cols_needed * (ICON_W + PADDING_X) + PADDING_X;
        if (win_w < 120) win_w = 120;
        if (win_w > WIN_WIDTH) win_w = WIN_WIDTH;
    }

    state->form = sys_win_create_form(title, win_x, win_y, win_w, win_h);
    if (!state->form) {
        sys_free(state);
        return -1;
    }
    sys_win_set_icon(state->form, "C:/ICONS/GROUP.ICO");

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
        back.checked = 0;
        strcpy(back.text, "..");
        sys_win_add_control(state->form, &back);
        ctrl_set_image(state->form, back.id, "C:/ICONS/BACK.ICO");
    }

    /* Add icons for each app */
    for (int i = 0; i < state->app_count; i++) {
        int pos = i + icon_offset;
        int col = pos % COLS;
        int row = pos / COLS;
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
        icon.checked = 0;

        strncpy(icon.text, state->apps[i].name, sizeof(icon.text) - 1);
        sys_win_add_control(state->form, &icon);

        /* Set icon */
        ctrl_set_image(state->form, icon.id, state->apps[i].icon_path);
    }

    sys_win_draw(state->form);
    prog_register_window(inst, state->form);
    return 0;
}

static int startman_event(prog_instance_t *inst, int win_idx, int event) {
    (void)win_idx;
    startman_state_t *state = inst->user_data;

    /* Handle window events */
    if (event == -1 || event == -2)
        return PROG_EVENT_REDRAW;

    /* Handle back button - open main Start Manager window and close this one */
    if (event == CTRL_BACK_BUTTON && state) {
        g_pending_grp_path[0] = '\0';  /* Clear to open as main window */
        progman_launch("Start Manager");
        return PROG_EVENT_CLOSE;
    }

    /* Handle icon double-clicks */
    if (event >= CTRL_APP_BASE && state) {
        int app_index = event - CTRL_APP_BASE;
        if (app_index >= 0 && app_index < state->app_count) {
            app_entry_t *app = &state->apps[app_index];
            if (app->type == ENTRY_GRP) {
                /* Open group in new window */
                open_group_window(app->path, state->grp_path);
            } else {
                /* Launch ELF */
                sys_spawn_async(app->path);
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
            prog_unregister_window(inst, state->form);
            sys_win_destroy_form(state->form);
        }
        sys_free(state);
        inst->user_data = 0;
    }
}

/* Open a new group window by launching a new startman instance */
static void open_group_window(const char *grp_path, const char *parent_grp) {
    (void)parent_grp;
    /* Store GRP path for the new instance to pick up */
    strcpy(g_pending_grp_path, grp_path);
    /* Launch new startman instance */
    progman_launch("Start Manager");
}
