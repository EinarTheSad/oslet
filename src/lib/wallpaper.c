#include "wallpaper.h"
#include "rect.h"
#include "../win/wm_config.h"

int wallpaper_cache(const char *path, gfx_cached_bmp_t *out, int *out_x, int *out_y) {
    if (!path || !path[0] || !out) return -1;
    if (sys_gfx_cache_bmp(path, out) != 0) {
        if (out) { out->data = 0; out->width = 0; out->height = 0; }
        return -1;
    }

    /* center */
    int wx = (WM_SCREEN_WIDTH - out->width) / 2;
    int wy = (WM_SCREEN_HEIGHT - out->height) / 2;
    if (wx < 0) wx = 0;
    if (wy < 0) wy = 0;

    if (out_x) *out_x = wx;
    if (out_y) *out_y = wy;
    return 0;
}

void wallpaper_free(gfx_cached_bmp_t *out) {
    if (!out) return;
    sys_gfx_free_cached(out);
    out->data = 0;
    out->width = 0;
    out->height = 0;
}

void wallpaper_draw(gfx_cached_bmp_t *out, int x, int y, int transparent) {
    if (!out || !out->data) return;
    sys_gfx_draw_cached(out, x, y, transparent);
}

int wallpaper_draw_partial(gfx_cached_bmp_t *out, int dest_x, int dest_y,
                               int src_x, int src_y, int src_w, int src_h, int transparent) {
    if (!out || !out->data) return -1;
    return sys_gfx_draw_cached_partial(out, dest_x, dest_y, src_x, src_y, src_w, src_h, transparent);
}
