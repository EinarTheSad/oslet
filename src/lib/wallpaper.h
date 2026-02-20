#pragma once
#include "../syscall.h"

/* Helper functions for userland wallpaper handling */

/* wallpaper modes used by both desktop and control preview.  These values
   match the integers stored in SYSTEM.INI DESKTOP.MODE so they can be
   written/read directly.
     0 = centered (default)
     1 = stretch to fill screen
     2 = tile/repeat across the desktop
*/
#define WP_MODE_CENTER   0
#define WP_MODE_STRETCH  1
#define WP_MODE_TILE     2

int wallpaper_cache(const char *path, gfx_cached_bmp_t *out, int *out_x, int *out_y, int mode);
void wallpaper_free(gfx_cached_bmp_t *out);
void wallpaper_draw(gfx_cached_bmp_t *out, int x, int y);
int wallpaper_draw_partial(gfx_cached_bmp_t *out, int dest_x, int dest_y,
                               int src_x, int src_y, int src_w, int src_h);
