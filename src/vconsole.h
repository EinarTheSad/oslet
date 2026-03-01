#pragma once
#include <stdint.h>
#include <stddef.h>

#define VC_COLS 80
#define VC_ROWS 25
#define VC_KEYBUF_SIZE 64
#define VC_HISTORY_SIZE 10
#define VC_HISTORY_MAXLEN 256

typedef struct vconsole {
    uint8_t chars[VC_ROWS * VC_COLS];
    uint8_t attrs[VC_ROWS * VC_COLS];
    int cursor_x, cursor_y;
    uint8_t color;
    volatile uint8_t dirty;

    volatile uint8_t keybuf[VC_KEYBUF_SIZE];
    volatile int key_head, key_tail;

    char history[VC_HISTORY_SIZE][VC_HISTORY_MAXLEN];
    int history_count;

    uint32_t owner_tid;
    uint8_t active;
} vconsole_t;

vconsole_t *vc_create(uint32_t owner_tid);
void vc_destroy(vconsole_t *vc);

void vc_putchar(vconsole_t *vc, char c);
void vc_write(vconsole_t *vc, const char *s);

int vc_getchar(vconsole_t *vc);
size_t vc_getline(vconsole_t *vc, char *buf, size_t maxlen);

void vc_clear(vconsole_t *vc);
void vc_set_color(vconsole_t *vc, uint8_t bg, uint8_t fg);
void vc_set_cursor(vconsole_t *vc, int x, int y);
void vc_get_cursor(vconsole_t *vc, int *x, int *y);

void vc_send_key(vconsole_t *vc, uint8_t key);
