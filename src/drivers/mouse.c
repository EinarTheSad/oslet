#include "mouse.h"
#include "../irq/io.h"

static int mouse_x = 320;
static int mouse_y = 240;
static uint8_t mouse_buttons = 0;
static uint8_t mouse_cycle = 0;
static int8_t mouse_byte[3];
static uint8_t cursor_buffer[12 * 20];
static int saved_x = -1, saved_y = -1;
int buffer_valid = 0;

extern void pic_send_eoi(int irq);
extern void gfx_putpixel(int x, int y, uint8_t color);

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

void mouse_draw_cursor(int x, int y) {
    for (int row = 0; row < 19; row++) {
        for (int col = 0; col < 11; col++) {
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
    extern uint8_t gfx_getpixel(int x, int y);
    
    for (int row = 0; row < 20; row++) {
        for (int col = 0; col < 12; col++) {
            cursor_buffer[row * 12 + col] = gfx_getpixel(x + col, y + row);
        }
    }
    saved_x = x;
    saved_y = y;
    buffer_valid = 1;
}

void mouse_restore(void) {  
    if (!buffer_valid) return;
    
    for (int row = 0; row < 20; row++) {
        for (int col = 0; col < 12; col++) {
            gfx_putpixel(saved_x + col, saved_y + row, cursor_buffer[row * 12 + col]);
        }
    }
}