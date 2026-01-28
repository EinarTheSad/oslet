#pragma once
#include "../syscall.h"

/* Helper functions for userland wallpaper handling */
int wallpaper_cache(const char *path, gfx_cached_bmp_t *out, int *out_x, int *out_y);
void wallpaper_free(gfx_cached_bmp_t *out);
void wallpaper_draw(gfx_cached_bmp_t *out, int x, int y, int transparent);
int wallpaper_draw_partial(gfx_cached_bmp_t *out, int dest_x, int dest_y,
                               int src_x, int src_y, int src_w, int src_h, int transparent);
