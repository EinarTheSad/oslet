#pragma once

#include <stdint.h>

#define OSLET_KIND_AGIX    "AGIX"
#define OSLET_KIND_GIX     "GIX"
#define OSLET_KIND_DESKTOP "DESKTOP"

#define OSLET_APP_FLAG_NONE 0

#define OSLET_STR2(x) #x
#define OSLET_STR(x) OSLET_STR2(x)

#define OSLET_APP(name, kind, icon_path, flags) \
    __attribute__((section(".oslet_app"), used)) \
    static const char __oslet_app_name[] = "OSLET:NAME=" name; \
    __attribute__((section(".oslet_app"), used)) \
    static const char __oslet_app_kind[] = "OSLET:KIND=" kind; \
    __attribute__((section(".oslet_app"), used)) \
    static const char __oslet_app_icon[] = "OSLET:ICON=" icon_path; \
    __attribute__((section(".oslet_app"), used)) \
    static const char __oslet_app_flags[] = "OSLET:FLAGS=" OSLET_STR(flags)

typedef enum {
    OSLET_APP_UNKNOWN = 0,
    OSLET_APP_AGIX,
    OSLET_APP_GIX,
    OSLET_APP_DESKTOP_BOOTSTRAP
} oslet_app_kind_t;

typedef enum {
    OSLET_LAUNCH_FROM_AGIX = 0,
    OSLET_LAUNCH_FROM_GIX,
    OSLET_LAUNCH_FROM_TERMINAL
} oslet_launch_context_t;

#define OSLET_LAUNCH_OK 0
#define OSLET_LAUNCH_ERR -1
#define OSLET_LAUNCH_WRONG_MODE -2
#define OSLET_LAUNCH_ALREADY_RUNNING -3

typedef struct {
    oslet_app_kind_t kind;
    char name[64];
    char icon_path[64];
    uint16_t flags;
} oslet_app_info_t;

void oslet_app_info_init(oslet_app_info_t *info);
int oslet_app_read_info(const char *path, oslet_app_info_t *info);
const char *oslet_app_default_icon(oslet_app_kind_t kind);
int oslet_launch_program(const char *path,
                         const char *args,
                         oslet_launch_context_t context,
                         const char *icon_override,
                         int *out_tid);
int oslet_launch_program_wait(const char *path,
                              const char *args,
                              oslet_launch_context_t context,
                              const char *icon_override);
