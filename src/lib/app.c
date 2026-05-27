#include "app.h"
#include "stdio.h"
#include "string.h"
#include "../syscall.h"

#define INFO_BUF_SIZE 640
#define INFO_READ_SIZE 512

static int starts_with_icase(const char *s, const char *prefix) {
    while (*prefix) {
        char a = *s++;
        char b = *prefix++;
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) return 0;
    }
    return 1;
}

static int filename_is(const char *path, const char *name) {
    const char *base = path;
    if (!path || !name) return 0;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\' || *p == ':') base = p + 1;
    }
    return strcasecmp(base, name) == 0;
}

static int task_is_running(const char *name) {
    sys_taskinfo_t tasks[32];
    int count = sys_get_tasks(tasks, 32);

    for (int i = 0; i < count; i++) {
        if (strcasecmp(tasks[i].name, name) == 0) return 1;
    }
    return 0;
}

static void copy_filename_title(char *out, int out_len, const char *path) {
    const char *base = path;
    int len;

    if (!out || out_len <= 0) return;
    out[0] = '\0';
    if (!path) return;

    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\' || *p == ':') base = p + 1;
    }

    len = strlen(base);
    if (len > 4 && base[len - 4] == '.') len -= 4;
    if (len >= out_len) len = out_len - 1;

    for (int i = 0; i < len; i++) {
        char c = base[i];
        if (c == '_') c = ' ';
        out[i] = c;
    }
    out[len] = '\0';
}

void oslet_app_info_init(oslet_app_info_t *info) {
    if (!info) return;
    info->kind = OSLET_APP_UNKNOWN;
    info->name[0] = '\0';
    info->icon_path[0] = '\0';
    info->flags = 0;
}

static int find_value(const char *buf, int len, const char *key, char *out, int out_len) {
    int key_len = strlen(key);
    if (!buf || !key || !out || out_len <= 0 || key_len <= 0) return 0;

    for (int i = 0; i <= len - key_len; i++) {
        int match = 1;
        for (int j = 0; j < key_len; j++) {
            if (buf[i + j] != key[j]) {
                match = 0;
                break;
            }
        }
        if (!match) continue;

        int pos = i + key_len;
        int out_pos = 0;
        while (pos < len && out_pos < out_len - 1) {
            char c = buf[pos++];
            if (c == '\0' || c == '\r' || c == '\n') break;
            out[out_pos++] = c;
        }
        out[out_pos] = '\0';
        if (out_pos > 0)
            return 1;
    }
    return 0;
}

static void apply_kind(oslet_app_info_t *info, const char *kind) {
    if (!info || !kind || !kind[0]) return;

    if (strcasecmp(kind, OSLET_KIND_GIX) == 0) {
        info->kind = OSLET_APP_GIX;
    } else if (strcasecmp(kind, OSLET_KIND_AGIX) == 0) {
        info->kind = OSLET_APP_AGIX;
    } else if (strcasecmp(kind, OSLET_KIND_DESKTOP) == 0 ||
               strcasecmp(kind, "DESKTOP_BOOTSTRAP") == 0) {
        info->kind = OSLET_APP_DESKTOP_BOOTSTRAP;
    }
}

static uint16_t parse_flags(const char *s) {
    uint16_t v = 0;
    if (!s) return 0;
    while (*s >= '0' && *s <= '9') {
        v = (uint16_t)(v * 10 + (*s - '0'));
        s++;
    }
    return v;
}

int oslet_app_read_info(const char *path, oslet_app_info_t *info) {
    int fd;
    int n;
    int carry = 0;
    char buf[INFO_BUF_SIZE];
    char tmp[80];
    int saw_metadata = 0;

    if (!path || !info) return -1;
    oslet_app_info_init(info);

    if (filename_is(path, "DESKTOP.ELF")) {
        info->kind = OSLET_APP_DESKTOP_BOOTSTRAP;
        strcpy(info->name, "Desktop");
        strcpy(info->icon_path, "C:/ICONS/OSLET.ICO");
    }

    fd = sys_open(path, "r");
    if (fd < 0) {
        if (!info->name[0]) copy_filename_title(info->name, sizeof(info->name), path);
        if (!info->icon_path[0]) strcpy(info->icon_path, oslet_app_default_icon(info->kind));
        return -1;
    }

    while ((n = sys_read(fd, buf + carry, INFO_READ_SIZE)) > 0) {
        int total = carry + n;

        if (!info->name[0] && find_value(buf, total, "OSLET:NAME=", tmp, sizeof(tmp))) {
            strcpy(info->name, tmp);
            saw_metadata = 1;
        }

        if (find_value(buf, total, "OSLET:KIND=", tmp, sizeof(tmp))) {
            apply_kind(info, tmp);
            saw_metadata = 1;
        }

        if (!info->icon_path[0] && find_value(buf, total, "OSLET:ICON=", tmp, sizeof(tmp))) {
            strcpy(info->icon_path, tmp);
            saw_metadata = 1;
        }

        if (find_value(buf, total, "OSLET:FLAGS=", tmp, sizeof(tmp))) {
            info->flags = parse_flags(tmp);
            saw_metadata = 1;
        }

        if (info->kind == OSLET_APP_UNKNOWN && find_value(buf, total, "MODE=", tmp, sizeof(tmp))) {
            if (starts_with_icase(tmp, "GIX")) {
                info->kind = OSLET_APP_GIX;
            } else if (starts_with_icase(tmp, "AGIX")) {
                info->kind = OSLET_APP_AGIX;
            }
        }

        carry = total < 128 ? total : 128;
        for (int i = 0; i < carry; i++) {
            buf[i] = buf[total - carry + i];
        }

        if (saw_metadata && info->name[0] && info->icon_path[0] && info->kind != OSLET_APP_UNKNOWN)
            break;
    }

    sys_close(fd);

    if (!info->name[0]) copy_filename_title(info->name, sizeof(info->name), path);
    if (info->kind == OSLET_APP_UNKNOWN) info->kind = OSLET_APP_AGIX;
    if (!info->icon_path[0]) strcpy(info->icon_path, oslet_app_default_icon(info->kind));

    return 0;
}

const char *oslet_app_default_icon(oslet_app_kind_t kind) {
    switch (kind) {
        case OSLET_APP_DESKTOP_BOOTSTRAP:
            return "C:/ICONS/OSLET.ICO";
        case OSLET_APP_AGIX:
            return "C:/ICONS/TERMINAL.ICO";
        case OSLET_APP_GIX:
            return "C:/ICONS/EXE.ICO";
        default:
            return "C:/ICONS/EXE.ICO";
    }
}

int oslet_launch_program(const char *path,
                         const char *args,
                         oslet_launch_context_t context,
                         const char *icon_override,
                         int *out_tid) {
    oslet_app_info_t info;
    const char *icon;
    int tid;

    if (out_tid) *out_tid = 0;
    if (!path || !path[0]) return OSLET_LAUNCH_ERR;

    oslet_app_read_info(path, &info);
    icon = (icon_override && icon_override[0]) ? icon_override : info.icon_path;

    if (context != OSLET_LAUNCH_FROM_AGIX && info.kind == OSLET_APP_DESKTOP_BOOTSTRAP) {
        if (task_is_running("DESKTOP.ELF")) {
            sys_win_msgbox("The desktop is already running.", "OK", "Desktop");
            return OSLET_LAUNCH_ALREADY_RUNNING;
        }
    }

    if (context != OSLET_LAUNCH_FROM_AGIX && filename_is(path, "SHELL.ELF")) {
        sys_request_textmode();
        return OSLET_LAUNCH_OK;
    }

    if (context == OSLET_LAUNCH_FROM_AGIX && info.kind == OSLET_APP_GIX) {
        return OSLET_LAUNCH_WRONG_MODE;
    }

    if (context == OSLET_LAUNCH_FROM_GIX && info.kind == OSLET_APP_AGIX) {
        char term_args[256];
        term_args[0] = '\0';
        snprintf(term_args, sizeof(term_args), "%s%s%s",
                 path,
                 (args && args[0]) ? " " : "",
                 (args && args[0]) ? args : "");
        tid = sys_spawn_async_args("C:/OSLET/GIX/TERMINAL.ELF", term_args);
    } else {
        tid = sys_spawn_async_args(path, args ? args : "");
    }

    if (tid <= 0) return OSLET_LAUNCH_ERR;

    if (icon && icon[0]) sys_proc_set_icon(tid, icon);
    if (out_tid) *out_tid = tid;
    return OSLET_LAUNCH_OK;
}

int oslet_launch_program_wait(const char *path,
                              const char *args,
                              oslet_launch_context_t context,
                              const char *icon_override) {
    oslet_app_info_t info;
    int ret;
    (void)icon_override;

    if (!path || !path[0]) return OSLET_LAUNCH_ERR;

    oslet_app_read_info(path, &info);
    if (context != OSLET_LAUNCH_FROM_AGIX && info.kind == OSLET_APP_DESKTOP_BOOTSTRAP) {
        if (task_is_running("DESKTOP.ELF")) {
            sys_win_msgbox("The desktop is already running.", "OK", "Desktop");
            return OSLET_LAUNCH_ALREADY_RUNNING;
        }
    }

    if (context != OSLET_LAUNCH_FROM_AGIX && filename_is(path, "SHELL.ELF")) {
        sys_request_textmode();
        return OSLET_LAUNCH_OK;
    }

    if (context == OSLET_LAUNCH_FROM_AGIX && info.kind == OSLET_APP_GIX) {
        return OSLET_LAUNCH_WRONG_MODE;
    }

    if (context == OSLET_LAUNCH_FROM_GIX && info.kind == OSLET_APP_AGIX) {
        int tid = 0;
        return oslet_launch_program(path, args, context, icon_override, &tid);
    }

    ret = sys_spawn_args(path, args ? args : "");
    return ret == 0 ? OSLET_LAUNCH_OK : OSLET_LAUNCH_ERR;
}
