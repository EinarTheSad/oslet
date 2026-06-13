#include "mouse.h"
#include "graphics.h"
#include "../mem/heap.h"
#include "../irq/io.h"

#define CURSOR_MAX_W 32
#define CURSOR_MAX_H 32
#define CURSOR_CLEAN_PAD 2
#define CURSOR_SAVE_W (CURSOR_MAX_W + CURSOR_CLEAN_PAD)
#define CURSOR_SAVE_H (CURSOR_MAX_H + CURSOR_CLEAN_PAD)
#define CURSOR_DEFAULT_PATH "C:/ICONS/default.cur"
#define CURSOR_MOVE_PATH "C:/ICONS/move.cur"
#define CURSOR_BUSY_PATH "C:/ICONS/hrglass.cur"
#define CURSOR_PATH_MAX 128

static int mouse_x = 320;
static int mouse_y = 240;
static uint8_t mouse_buttons = 0;
static uint8_t mouse_cycle = 0;
static int8_t mouse_byte[3];
static uint8_t cursor_buffer[CURSOR_SAVE_W * CURSOR_SAVE_H];
static uint8_t *cursor_bitmap = NULL;
static int cursor_w = 11;
static int cursor_h = 19;
static int cursor_is_busy_cache = 0;
static char cursor_path[CURSOR_PATH_MAX] = CURSOR_DEFAULT_PATH;
static uint8_t *busy_bitmap = NULL;
static int busy_w = 0;
static int busy_h = 0;
static int busy_missing = 0;
static int busy_depth = 0;
static uint8_t *busy_saved_bitmap = NULL;
static int busy_saved_w = 11;
static int busy_saved_h = 19;
static char busy_saved_path[CURSOR_PATH_MAX] = CURSOR_DEFAULT_PATH;
static int default_cursor_checked = 0;
static int default_cursor_missing = 0;
static int saved_x = -1, saved_y = -1;
static int saved_w = 0, saved_h = 0;
int buffer_valid = 0;

extern void pic_send_eoi(int irq);

static void mouse_wait(uint8_t type) {
    uint32_t timeout = 100000;
    if (type == 0) {
        while (timeout--) {
            if ((inb(0x64) & 1) == 1) return;
        }
    } else {
        while (timeout--) {
            if ((inb(0x64) & 2) == 0) return;
        }
    }
}

static void mouse_write(uint8_t data) {
    mouse_wait(1);
    outb(0x64, 0xD4);
    mouse_wait(1);
    outb(0x60, data);
}

static uint8_t mouse_read(void) {
    mouse_wait(0);
    return inb(0x60);
}

void mouse_init(void) {
    mouse_wait(1);
    outb(0x64, 0xA8);
    
    mouse_wait(1);
    outb(0x64, 0x20);
    uint8_t status = mouse_read() | 2;
    mouse_wait(1);
    outb(0x64, 0x60);
    mouse_wait(1);
    outb(0x60, status);
    
    mouse_write(0xF6);
    mouse_read();
    
    mouse_write(0xF4);
    mouse_read();
}

void mouse_handler(void) {
    uint8_t status = inb(0x64);
    if (!(status & 0x20)) {
        pic_send_eoi(12);
        return;
    }
    
    int8_t data = inb(0x60);
    
    switch (mouse_cycle) {
        case 0:
            mouse_byte[0] = data;
            if (data & 0x08) mouse_cycle++;
            break;
        case 1:
            mouse_byte[1] = data;
            mouse_cycle++;
            break;
        case 2:
            mouse_byte[2] = data;
            mouse_cycle = 0;
            
            mouse_buttons = mouse_byte[0] & 0x07;
            
            int dx = mouse_byte[1];
            int dy = mouse_byte[2];
            
            if (mouse_byte[0] & 0x10) dx |= 0xFFFFFF00;
            if (mouse_byte[0] & 0x20) dy |= 0xFFFFFF00;
            
            mouse_x += dx;
            mouse_y -= dy;
            
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x > 639) mouse_x = 639;
            if (mouse_y > 479) mouse_y = 479;
            break;
    }
    
    pic_send_eoi(12);
}

int mouse_get_x(void) { return mouse_x; }
int mouse_get_y(void) { return mouse_y; }
uint8_t mouse_get_buttons(void) { return mouse_buttons; }

static const uint8_t cursor[19][11] = {
    {2,0,0,0,0,0,0,0,0,0,0},
	{2,2,0,0,0,0,0,0,0,0,0},
	{2,1,2,0,0,0,0,0,0,0,0},
	{2,1,1,2,0,0,0,0,0,0,0},
	{2,1,1,1,2,0,0,0,0,0,0},
	{2,1,1,1,1,2,0,0,0,0,0},
	{2,1,1,1,1,1,2,0,0,0,0},
	{2,1,1,1,1,1,1,2,0,0,0},
	{2,1,1,1,1,1,1,1,2,0,0},
	{2,1,1,1,1,1,1,1,1,2,0},
	{2,1,1,1,1,1,2,2,2,2,2},
	{2,1,1,2,1,1,2,0,0,0,0},
	{2,1,2,0,2,1,1,2,0,0,0},
	{2,2,0,0,2,1,1,2,0,0,0},
	{2,0,0,0,0,2,1,1,2,0,0},
	{0,0,0,0,0,2,1,1,2,0,0},
	{0,0,0,0,0,0,2,1,1,2,0},
	{0,0,0,0,0,0,2,1,1,2,0},
	{0,0,0,0,0,0,0,2,2,0,0}
};

static int mouse_streq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b)
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static void mouse_copy_path(char *dest, const char *src) {
    int i = 0;

    if (!dest)
        return;

    if (!src) {
        dest[0] = '\0';
        return;
    }

    while (src[i] && i < CURSOR_PATH_MAX - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static void mouse_free_current_bitmap(void) {
    if (cursor_bitmap && !cursor_is_busy_cache)
        kfree(cursor_bitmap);

    cursor_bitmap = NULL;
    cursor_is_busy_cache = 0;
}

static void mouse_clear_drawn_cursor(void) {
    if (buffer_valid)
        mouse_restore();
    buffer_valid = 0;
}

static void mouse_use_builtin_cursor(void) {
    mouse_clear_drawn_cursor();
    mouse_free_current_bitmap();
    cursor_w = 11;
    cursor_h = 19;
    mouse_copy_path(cursor_path, CURSOR_DEFAULT_PATH);
}

static uint8_t mouse_cursor_pixel(int x, int y) {
    int row_bytes = (cursor_w + 1) / 2;
    uint8_t byte = cursor_bitmap[y * row_bytes + x / 2];

    if (x & 1)
        return byte & 0x0F;
    return (byte >> 4) & 0x0F;
}

static void mouse_check_default_cursor(void) {
    if (!cursor_bitmap && !default_cursor_checked) {
        default_cursor_checked = 1;
        mouse_set_cursor_file(CURSOR_DEFAULT_PATH);
    }
}

static int mouse_load_cursor_bitmap(const char *path, uint8_t **out_data, int *out_w, int *out_h) {
    int w = 0;
    int h = 0;
    uint8_t *data;

    if (!path || !out_data || !out_w || !out_h)
        return -1;

    data = gfx_load_bmp_to_buffer(path, &w, &h);
    if (!data || w <= 0 || h <= 0 || w > CURSOR_MAX_W || h > CURSOR_MAX_H) {
        if (data)
            kfree(data);
        return -1;
    }

    *out_data = data;
    *out_w = w;
    *out_h = h;
    return 0;
}

static void mouse_show_current_cursor(int keep_drawn) {
    int x;
    int y;

    if (!gfx_is_active())
        return;

    x = mouse_get_x();
    y = mouse_get_y();
    mouse_save(x, y);
    mouse_draw_cursor(x, y);
    gfx_swap_buffers();

    if (!keep_drawn) {
        mouse_restore();
        buffer_valid = 0;
    }
}

static int mouse_set_saved_cursor_file(const char *path) {
    uint8_t *data = NULL;
    int w = 0;
    int h = 0;
    int is_default;

    if (!path) {
        if (busy_saved_bitmap)
            kfree(busy_saved_bitmap);
        busy_saved_bitmap = NULL;
        busy_saved_w = 11;
        busy_saved_h = 19;
        mouse_copy_path(busy_saved_path, CURSOR_DEFAULT_PATH);
        return -1;
    }

    is_default = mouse_streq(path, CURSOR_DEFAULT_PATH);
    if (is_default && default_cursor_missing) {
        if (busy_saved_bitmap)
            kfree(busy_saved_bitmap);
        busy_saved_bitmap = NULL;
        busy_saved_w = 11;
        busy_saved_h = 19;
        mouse_copy_path(busy_saved_path, CURSOR_DEFAULT_PATH);
        return -1;
    }

    if (mouse_load_cursor_bitmap(path, &data, &w, &h) != 0) {
        if (is_default) {
            default_cursor_checked = 1;
            default_cursor_missing = 1;
            if (busy_saved_bitmap)
                kfree(busy_saved_bitmap);
            busy_saved_bitmap = NULL;
            busy_saved_w = 11;
            busy_saved_h = 19;
            mouse_copy_path(busy_saved_path, CURSOR_DEFAULT_PATH);
        }
        return -1;
    }

    if (busy_saved_bitmap)
        kfree(busy_saved_bitmap);

    busy_saved_bitmap = data;
    busy_saved_w = w;
    busy_saved_h = h;
    mouse_copy_path(busy_saved_path, path);

    if (is_default) {
        default_cursor_checked = 1;
        default_cursor_missing = 0;
    }

    return 0;
}

int mouse_set_cursor_file(const char *path) {
    int w = 0;
    int h = 0;
    uint8_t *data = NULL;
    int is_default;

    if (busy_depth > 0) {
        if (path && mouse_streq(path, CURSOR_BUSY_PATH))
            return 0;
        return mouse_set_saved_cursor_file(path);
    }

    if (!path) {
        mouse_use_builtin_cursor();
        return -1;
    }

    is_default = mouse_streq(path, CURSOR_DEFAULT_PATH);
    if (is_default && default_cursor_missing) {
        mouse_use_builtin_cursor();
        return -1;
    }

    if (mouse_load_cursor_bitmap(path, &data, &w, &h) != 0) {
        if (is_default) {
            default_cursor_checked = 1;
            default_cursor_missing = 1;
            mouse_use_builtin_cursor();
        }

        return -1;
    }

    mouse_clear_drawn_cursor();
    mouse_free_current_bitmap();

    cursor_bitmap = data;
    cursor_w = w;
    cursor_h = h;
    cursor_is_busy_cache = 0;
    mouse_copy_path(cursor_path, path);
    if (is_default) {
        default_cursor_checked = 1;
        default_cursor_missing = 0;
    }
    return 0;
}

void mouse_set_cursor_mode(int mode) {
    if (mode == 1) {
        mouse_set_cursor_file(CURSOR_MOVE_PATH);
        return;
    }

    mouse_set_cursor_file(CURSOR_DEFAULT_PATH);
}

int mouse_busy_begin(void) {
    if (busy_depth > 0) {
        busy_depth++;
        return 0;
    }

    mouse_check_default_cursor();
    mouse_clear_drawn_cursor();

    busy_depth = 1;
    busy_saved_bitmap = cursor_bitmap;
    busy_saved_w = cursor_w;
    busy_saved_h = cursor_h;
    mouse_copy_path(busy_saved_path, cursor_path);

    cursor_bitmap = NULL;
    cursor_is_busy_cache = 0;

    if (!busy_bitmap && !busy_missing) {
        if (mouse_load_cursor_bitmap(CURSOR_BUSY_PATH, &busy_bitmap, &busy_w, &busy_h) != 0)
            busy_missing = 1;
    }

    if (!busy_bitmap) {
        cursor_bitmap = busy_saved_bitmap;
        cursor_w = busy_saved_w;
        cursor_h = busy_saved_h;
        busy_saved_bitmap = NULL;
        busy_depth = 0;
        return -1;
    }

    cursor_bitmap = busy_bitmap;
    cursor_w = busy_w;
    cursor_h = busy_h;
    cursor_is_busy_cache = 1;
    mouse_copy_path(cursor_path, CURSOR_BUSY_PATH);
    mouse_show_current_cursor(0);
    return 0;
}

void mouse_busy_end(void) {
    if (busy_depth <= 0)
        return;

    busy_depth--;
    if (busy_depth > 0)
        return;

    mouse_clear_drawn_cursor();

    cursor_bitmap = busy_saved_bitmap;
    cursor_w = busy_saved_w;
    cursor_h = busy_saved_h;
    cursor_is_busy_cache = 0;
    mouse_copy_path(cursor_path, busy_saved_path);

    busy_saved_bitmap = NULL;
    busy_saved_w = 11;
    busy_saved_h = 19;
    mouse_copy_path(busy_saved_path, CURSOR_DEFAULT_PATH);

    mouse_show_current_cursor(1);
}

void mouse_draw_cursor(int x, int y) {
    mouse_check_default_cursor();

    if (cursor_bitmap) {
        for (int row = 0; row < cursor_h; row++) {
            for (int col = 0; col < cursor_w; col++) {
                uint8_t px = mouse_cursor_pixel(col, row);
                if (px != 5)
                    gfx_putpixel(x + col, y + row, px);
            }
        }
        return;
    }

    for (int row = 0; row < cursor_h; row++) {
        for (int col = 0; col < cursor_w; col++) {
            uint8_t px = cursor[row][col];

            if (px == 0)
                continue;

            if (px == 1)
                gfx_putpixel(x + col, y + row, 15);

            if (px == 2)
                gfx_putpixel(x + col, y + row, 0);
        }
    }
}

void mouse_save(int x, int y) {
    mouse_check_default_cursor();

    saved_w = cursor_w + CURSOR_CLEAN_PAD;
    saved_h = cursor_h + CURSOR_CLEAN_PAD;
    if (saved_w > CURSOR_SAVE_W) saved_w = CURSOR_SAVE_W;
    if (saved_h > CURSOR_SAVE_H) saved_h = CURSOR_SAVE_H;

    for (int row = 0; row < saved_h; row++) {
        for (int col = 0; col < saved_w; col++) {
            int sx = x + col;
            int sy = y + row;
            if (sx >= 0 && sx < GFX_WIDTH && sy >= 0 && sy < GFX_HEIGHT)
                cursor_buffer[row * CURSOR_SAVE_W + col] = gfx_getpixel(sx, sy);
            else
                cursor_buffer[row * CURSOR_SAVE_W + col] = 0;
        }
    }
    saved_x = x;
    saved_y = y;
    buffer_valid = 1;
}

void mouse_restore(void) {
    if (!buffer_valid) return;

    for (int row = 0; row < saved_h; row++) {
        for (int col = 0; col < saved_w; col++) {
            int sx = saved_x + col;
            int sy = saved_y + row;
            if (sx >= 0 && sx < GFX_WIDTH && sy >= 0 && sy < GFX_HEIGHT)
                gfx_putpixel(sx, sy, cursor_buffer[row * CURSOR_SAVE_W + col]);
        }
    }
}

void mouse_invalidate_buffer(void) {
    buffer_valid = 0;
}
