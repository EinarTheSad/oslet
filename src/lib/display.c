#include "display.h"
#include "../syscall.h"

int disp_width = 640;
int disp_height = 480;

int oslet_display_init(void) {
    sys_display_info_t info;

    if (sys_gfx_get_display_info(&info) != 0 ||
        info.width < 640 || info.width > 2048 ||
        info.height < 400 || info.height > 1536) {
        return -1;
    }

    disp_width = (int)info.width;
    disp_height = (int)info.height;
    return 0;
}
