#include "elf.h"
#include "app.h"

int elf_is_textmode(const char *path) {
    oslet_app_info_t info;

    if (!path) return 0;
    if (oslet_app_read_info(path, &info) != 0) return 0;

    return info.kind == OSLET_APP_GIX ? 1 : 0;
}
