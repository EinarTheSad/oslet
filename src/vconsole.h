#pragma once
#include <stdint.h>
#include <stddef.h>

#define VC_COLS 80
#define VC_ROWS 25
#define VC_MAX_COLS 240
#define VC_MAX_ROWS 135
#define VC_SCROLLBACK 256
#define VC_KEYBUF_SIZE 64
#define VC_HISTORY_SIZE 10
#define VC_HISTORY_MAXLEN 256

struct task;

typedef struct vconsole {
    uint8_t chars[VC_MAX_ROWS * VC_MAX_COLS];
    uint8_t attrs[VC_MAX_ROWS * VC_MAX_COLS];
    uint8_t history_chars[VC_SCROLLBACK * VC_MAX_COLS];
    uint8_t history_attrs[VC_SCROLLBACK * VC_MAX_COLS];
    int cols, rows;
    int history_start, history_lines;
    uint32_t history_sequence;
    int cursor_x, cursor_y;
    uint8_t color;
    volatile uint8_t dirty;

    volatile uint8_t keybuf[VC_KEYBUF_SIZE];
    volatile int key_head, key_tail;

    char history[VC_HISTORY_SIZE][VC_HISTORY_MAXLEN];
    int history_count;

    uint32_t owner_tid;
    uint32_t refs;
    volatile uint8_t active;
} vconsole_t;

vconsole_t *vc_create(uint32_t owner_tid);
void vc_destroy(vconsole_t *vc);
int vc_is_registered(vconsole_t *vc);
void vc_assign_task(struct task *task, vconsole_t *vc);
void vc_begin_read(struct task *task, vconsole_t *vc);
void vc_end_read(struct task *task);
void vc_cleanup_task(struct task *task);

void vc_putchar(vconsole_t *vc, char c);
void vc_write(vconsole_t *vc, const char *s);

int vc_getchar(vconsole_t *vc);
size_t vc_getline(vconsole_t *vc, char *buf, size_t maxlen);

void vc_clear(vconsole_t *vc);
void vc_set_color(vconsole_t *vc, uint8_t bg, uint8_t fg);
void vc_set_cursor(vconsole_t *vc, int x, int y);
void vc_get_cursor(vconsole_t *vc, int *x, int *y);
int vc_resize(vconsole_t *vc, int cols, int rows);
void vc_copy_row(vconsole_t *vc, int row, uint8_t *chars, uint8_t *attrs);

void vc_send_key(vconsole_t *vc, uint8_t key);
