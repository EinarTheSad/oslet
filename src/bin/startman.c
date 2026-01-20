#include "progmod.h"
#include "progman.h"
#include "../syscall.h"
#include "../lib/string.h"

/* Control ID base for app icons */
#define CTRL_APP_BASE 100

#define ICON_W 48
#define ICON_H 58
#define PADDING_X 12
#define PADDING_Y 8
#define COLS 5

#define WIN_WIDTH 350
#define WIN_HEIGHT 225
#define WIN_X 65
#define WIN_Y 80

#define INI_PATH "C:/OSLET/START/STARTMAN.INI"
#define MAX_APPS 32

/* App entry from filesystem scan */
typedef struct {
    char name[32];       /* Display name (without .ELF) */
    char path[64];       /* Full path to ELF */
    char elf_name[32];   /* Original ELF filename (uppercase) for INI lookup */
    char icon_path[64];  /* Icon path from INI or default */
} app_entry_t;

typedef struct {
    void *form;
    int app_count;
    app_entry_t apps[MAX_APPS];
} startman_state_t;

static int startman_init(prog_instance_t *inst);
static int startman_event(prog_instance_t *inst, int win_idx, int event);
static void startman_cleanup(prog_instance_t *inst);

const progmod_t startman_module = {
    .name = "Start Manager",
    .icon_path = "C:/ICONS/STARTMAN.ICO",
    .init = startman_init,
    .update = 0,
    .handle_event = startman_event,
    .cleanup = startman_cleanup,
    .flags = PROG_FLAG_SINGLETON
};

static char to_upper(char c) {
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 'A';
    return c;
}

static char to_lower(char c) {
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 'a';
    return c;
}

static void str_to_upper(char *s) {
    while (*s) {
        *s = to_upper(*s);
        s++;
    }
}

static void title_case(char *dst, const char *src, int max_len) {
    int i = 0;
    int capitalize_next = 1;  /* Capitalize first letter */

    while (*src && i < max_len - 1) {
        if (*src == '_') {
            dst[i++] = ' ';
            capitalize_next = 1;
        } else {
            if (capitalize_next) {
                dst[i++] = to_upper(*src);
                capitalize_next = 0;
            } else {
                dst[i++] = to_lower(*src);
            }
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

    /* Create uppercase version of elf_name for comparison */
    char upper_elf[32];
    strncpy(upper_elf, elf_name, sizeof(upper_elf) - 1);
    upper_elf[sizeof(upper_elf) - 1] = '\0';
    str_to_upper(upper_elf);

    /* Search for line starting with elf_name= */
    char *line = buf;
    while (*line) {
        /* Skip whitespace */
        while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n')
            line++;

        if (*line == '\0')
            break;

        if (*line == '[') {
            /* Skip section header */
            while (*line && *line != '\n')
                line++;
            continue;
        }

        /* Check if line starts with our ELF name (case insensitive) */
        char *p = line;
        char *e = upper_elf;

        /* Convert line chars to uppercase for comparison */
        int match = 1;
        while (*e && *p && *p != '=' && *p != '\n') {
            if (to_upper(*p) != *e) {
                match = 0;
                break;
            }
            p++;
            e++;
        }

        if (match && *e == '\0' && *p == '=') {
            /* Found matching entry */
            p++;  /* Skip '=' */

            /* Copy path until end of line */
            int i = 0;
            while (*p && *p != '\r' && *p != '\n' && i < max_len - 1) {
                icon_path[i++] = *p++;
            }
            icon_path[i] = '\0';
            return 1;
        }

        /* Skip to next line */
        while (*line && *line != '\n')
            line++;
        if (*line == '\n')
            line++;
    }

    return 0;
}

static int is_elf_file(const char *name) {
    int len = strlen(name);
    if (len < 5) return 0;  /* Need at least "x.elf" */

    const char *ext = name + len - 4;
    return (ext[0] == '.' &&
            (ext[1] == 'E' || ext[1] == 'e') &&
            (ext[2] == 'L' || ext[2] == 'l') &&
            (ext[3] == 'F' || ext[3] == 'f'));
}

static void parse_name(char *dst, const char *src, int max_len) {
    int len = strlen(src);
    if (len > 4) len -= 4;  /* Remove .ELF */
    if (len >= max_len) len = max_len - 1;

    for (int i = 0; i < len; i++) {
        dst[i] = src[i];
    }
    dst[len] = '\0';
}

static void elf_to_upper(char *dst, const char *src, int max_len) {
    int i = 0;
    while (*src && i < max_len - 1) {
        dst[i++] = to_upper(*src++);
    }
    dst[i] = '\0';
}

static int scan_apps(startman_state_t *state) {
    sys_dirent_t entries[32];
    int count = sys_readdir("C:/OSLET/START", entries, 32);

    if (count < 0) {
        /* Directory doesn't exist or error - try to create it */
        sys_mkdir("C:/OSLET");
        sys_mkdir("C:/OSLET/START");
        return 0;
    }

    int app_count = 0;
    for (int i = 0; i < count && app_count < MAX_APPS; i++) {
        if (entries[i].is_directory) continue;
        if (!is_elf_file(entries[i].name)) continue;

        app_entry_t *app = &state->apps[app_count];

        /* Store uppercase ELF filename for INI lookup */
        elf_to_upper(app->elf_name, entries[i].name, sizeof(app->elf_name));

        /* Extract name without extension, then apply Title Case */
        char raw_name[32];
        parse_name(raw_name, entries[i].name, sizeof(raw_name));
        title_case(app->name, raw_name, sizeof(app->name));

        /* Build full path */
        strcpy(app->path, "C:/OSLET/START/");
        strcat(app->path, entries[i].name);

        /* Try to get icon from INI, otherwise use default */
        if (!parse_ini(app->elf_name, app->icon_path, sizeof(app->icon_path))) {
            strcpy(app->icon_path, "C:/ICONS/EXE.ICO");
        }

        app_count++;
    }

    state->app_count = app_count;
    return app_count;
}

static int startman_init(prog_instance_t *inst) {
    startman_state_t *state = sys_malloc(sizeof(startman_state_t));
    if (!state)
        return -1;

    inst->user_data = state;
    state->app_count = 0;
    scan_apps(state);

    state->form = sys_win_create_form("Start Manager", WIN_X, WIN_Y, WIN_WIDTH, WIN_HEIGHT);
    if (!state->form) {
        sys_free(state);
        return -1;
    }
    sys_win_set_icon(state->form, "C:/ICONS/EXE.ICO");

    /* Add icons for each app */
    for (int i = 0; i < state->app_count; i++) {
        int col = i % COLS;
        int row = i / COLS;
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

        /* Set icon from INI or default */
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

    /* Handle icon double-clicks */
    if (event >= CTRL_APP_BASE && state) {
        int app_index = event - CTRL_APP_BASE;
        if (app_index >= 0 && app_index < state->app_count) {
            sys_spawn_async(state->apps[app_index].path);
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
