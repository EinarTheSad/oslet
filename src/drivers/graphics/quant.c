#include "gpriv.h"

typedef struct {
    int16_t r, g, b;
} color_error_t;

static int clamp_channel(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return value;
}

static void add_error(color_error_t *e, int r, int g, int b, int weight) {
    e->r += r * weight / 16;
    e->g += g * weight / 16;
    e->b += b * weight / 16;
}

int gfx_dither_colors(uint8_t *data, uint32_t *colors, int width, int height, int masked) {
    if (!data || !colors || width <= 0 || height <= 0 ||
        width > 4096 || height > 4096) return -1;

    uint16_t (*distances)[256][16] = kmalloc(3 * sizeof(*distances));
    color_error_t *errors = kmalloc((size_t)width * 2 * sizeof(*errors));
    if (!distances || !errors) {
        if (distances) kfree(distances);
        if (errors) kfree(errors);
        return -1;
    }

    for (int channel = 0; channel < 3; channel++) {
        for (int value = 0; value < 256; value++) {
            for (int i = 0; i < 16; i++) {
                int difference = value - gfx_palette[i][channel];
                distances[channel][value][i] = (uint16_t)(difference * difference);
            }
        }
    }
    memset_s(errors, 0, (size_t)width * 2 * sizeof(*errors));

    int row_bytes = (width + 1) / 2;
    color_error_t *current = errors, *next = errors + width;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            size_t pos = (size_t)y * width + x;
            uint32_t rgb = colors[pos];
            uint8_t index = 5;
            if (!masked || !(rgb & GFX_BMP_TRANSPARENT)) {
                int r = clamp_channel((int)((rgb >> 16) & 255) + current[x].r);
                int g = clamp_channel((int)((rgb >> 8) & 255) + current[x].g);
                int b = clamp_channel((int)(rgb & 255) + current[x].b);
                const uint16_t *red = distances[0][r];
                const uint16_t *green = distances[1][g];
                const uint16_t *blue = distances[2][b];
                int best = 0x7FFFFFFF;
                for (int i = 0; i < 16; i++) {
                    if (masked && i == 5) continue;
                    int distance = red[i] + green[i] + blue[i];
                    if (distance < best) {
                        best = distance;
                        index = (uint8_t)i;
                    }
                }
                r -= gfx_palette[index][0];
                g -= gfx_palette[index][1];
                b -= gfx_palette[index][2];
                if (x + 1 < width) add_error(&current[x + 1], r, g, b, 7);
                if (x > 0) add_error(&next[x - 1], r, g, b, 3);
                add_error(&next[x], r, g, b, 5);
                if (x + 1 < width) add_error(&next[x + 1], r, g, b, 1);
            }
            colors[pos] = (rgb & (GFX_BMP_TRANSPARENT | 0x00FFFFFFu)) |
                          ((uint32_t)index << 24);
            size_t offset = (size_t)y * row_bytes + x / 2;
            if (x & 1) data[offset] |= index;
            else data[offset] = index << 4;
        }
        color_error_t *tmp = current;
        current = next;
        next = tmp;
        memset_s(next, 0, (size_t)width * sizeof(*next));
    }
    kfree(errors);
    kfree(distances);
    return 0;
}
