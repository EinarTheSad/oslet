#include "bitmap.h"
#include "../mem/heap.h"
#include "../drivers/graphics.h"

bitmap_t* bitmap_load_from_file(const char *path) {
    if (!path || !path[0]) return NULL;

    bitmap_t *bmp = (bitmap_t*)kmalloc(sizeof(bitmap_t));
    if (!bmp) return NULL;

    int width, height;
    bmp->data = gfx_load_bmp_to_buffer(path, &width, &height);

    if (!bmp->data) {
        kfree(bmp);
        return NULL;
    }

    bmp->width = width;
    bmp->height = height;
    bmp->bits_per_pixel = 4;  // VGA 4-bit mode

    return bmp;
}

void bitmap_draw(bitmap_t *bmp, int x, int y) {
    if (!bmp || !bmp->data) return;

    gfx_draw_cached_bmp(bmp->data, bmp->width, bmp->height, x, y);
}

void bitmap_free(bitmap_t *bmp) {
    if (!bmp) return;

    if (bmp->data) {
        kfree(bmp->data);
        bmp->data = NULL;
    }

    kfree(bmp);
}
