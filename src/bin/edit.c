#include "../syscall.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "../drivers/keyboard.h"
#include <stdint.h>

#define MAX_LINES 200
#define MAX_LINE_LEN 80
#define SCREEN_HEIGHT 25
#define SCREEN_WIDTH 80
#define MENU_HEIGHT 1
#define STATUS_HEIGHT 1
#define EDIT_HEIGHT (SCREEN_HEIGHT - MENU_HEIGHT - STATUS_HEIGHT)
#define EDIT_START_Y MENU_HEIGHT

typedef struct {
    char lines[MAX_LINES][MAX_LINE_LEN];
    int line_count;
    int cursor_x;
    int cursor_y;
    int scroll_offset;
    int dirty;
    char filename[256];
    int dirty_lines[MAX_LINES];
    int full_redraw;
} editor_t;

static editor_t editor;

/* Forward declarations */
static void insert_newline(void);
static void adjust_scroll(void);
static void mark_line_dirty(int line);
static void mark_all_dirty(void);

static void mark_line_dirty(int line) {
    if (line >= 0 && line < MAX_LINES) {
        editor.dirty_lines[line] = 1;
    }
}

static void mark_all_dirty(void) {
    editor.full_redraw = 1;
    for (int i = 0; i < MAX_LINES; i++) {
        editor.dirty_lines[i] = 1;
    }
}

static void draw_menu(void) {
    /* Write directly to VGA memory at line 0 for SPEED */
    volatile uint16_t *vga = (uint16_t*)0xB8000;
    uint16_t color = ((7 << 4) | 0) << 8; /* Gray bg (7), black fg (0) */
    
    char menu[SCREEN_WIDTH + 1];
    int pos = 0;
    
    pos += snprintf(menu + pos, SCREEN_WIDTH - pos, "  %s - osLET Editor", 
        editor.filename[0] ? editor.filename : "UNTITLED.TXT");
    
    while (pos < SCREEN_WIDTH - 30) {
        menu[pos++] = ' ';
    }
    
    pos += snprintf(menu + pos, SCREEN_WIDTH - pos, "F2 Save   F3 Open   F10 Quit");
    
    while (pos < SCREEN_WIDTH) {
        menu[pos++] = ' ';
    }
    menu[SCREEN_WIDTH] = '\0';
    
    /* Write to VGA line 0 */
    for (int i = 0; i < SCREEN_WIDTH; i++) {
        vga[i] = color | (unsigned char)menu[i];
    }
}

static void draw_status(void) {
    /* Write directly to VGA memory at last line, also for SPEED*/
    volatile uint16_t *vga = (uint16_t*)0xB8000;
    volatile uint16_t *status_line = vga + (SCREEN_HEIGHT - 1) * SCREEN_WIDTH;
    uint16_t color = ((7 << 4) | 0) << 8; /* Gray bg (7), black fg (0) */
    
    char status[SCREEN_WIDTH + 1];
    int pos = 0;
    
    if (editor.dirty) {
        pos += snprintf(status + pos, SCREEN_WIDTH - pos, " *");
    } else {
        pos += snprintf(status + pos, SCREEN_WIDTH - pos, "  ");
    }
    
    while (pos < SCREEN_WIDTH - 25) {
        status[pos++] = ' ';
    }
    
    pos += snprintf(status + pos, SCREEN_WIDTH - pos, "Line: %d Column: %d", 
        editor.cursor_y + 1, editor.cursor_x + 1);
    
    while (pos < SCREEN_WIDTH) {
        status[pos++] = ' ';
    }
    status[SCREEN_WIDTH] = '\0';
    
    /* Write to VGA last line */
    for (int i = 0; i < SCREEN_WIDTH; i++) {
        status_line[i] = color | (unsigned char)status[i];
    }
}

static void redraw_screen(void) {
    volatile uint16_t *vga = (uint16_t*)0xB8000;
    uint16_t normal_color = ((0 << 4) | 7) << 8;
    
    /* Full clear only if needed */
    if (editor.full_redraw) {
        for (int y = 0; y < SCREEN_HEIGHT; y++) {
            for (int x = 0; x < SCREEN_WIDTH; x++) {
                vga[y * SCREEN_WIDTH + x] = normal_color | ' ';
            }
        }
        draw_menu();
        editor.full_redraw = 0;
    }
    
    /* Draw only dirty lines */
    for (int i = 0; i < EDIT_HEIGHT && (i + editor.scroll_offset) < editor.line_count; i++) {
        int line_idx = i + editor.scroll_offset;
        
        if (editor.dirty_lines[line_idx]) {
            int screen_line = EDIT_START_Y + i;
            char *line = editor.lines[line_idx];
            int len = strlen(line);
            
            for (int x = 0; x < SCREEN_WIDTH; x++) {
                char ch = (x < len) ? line[x] : ' ';
                vga[screen_line * SCREEN_WIDTH + x] = normal_color | (unsigned char)ch;
            }
            
            editor.dirty_lines[line_idx] = 0;
        }
    }
    
    draw_status();
    
    /* Position cursor */
    int screen_y = EDIT_START_Y + (editor.cursor_y - editor.scroll_offset);
    if (screen_y < EDIT_START_Y) screen_y = EDIT_START_Y;
    if (screen_y >= SCREEN_HEIGHT - STATUS_HEIGHT) screen_y = SCREEN_HEIGHT - STATUS_HEIGHT - 1;
    
    sys_setcur(editor.cursor_x, screen_y);
}

static void adjust_scroll(void) {
    if (editor.cursor_y >= editor.scroll_offset + EDIT_HEIGHT) {
        editor.scroll_offset = editor.cursor_y - EDIT_HEIGHT + 1;
    }
    
    if (editor.cursor_y < editor.scroll_offset) {
        editor.scroll_offset = editor.cursor_y;
    }
}

static void insert_char(char c) {
    int y = editor.cursor_y;
    int x = editor.cursor_x;
    
    if (x >= MAX_LINE_LEN - 1) {
        insert_newline();
        y = editor.cursor_y;
        x = 0;
    }
    
    int len = strlen(editor.lines[y]);
    
    for (int i = len; i > x; i--) {
        editor.lines[y][i] = editor.lines[y][i - 1];
    }
    
    editor.lines[y][x] = c;
    editor.lines[y][len + 1] = '\0';
    editor.cursor_x++;
    
    if (editor.cursor_x >= MAX_LINE_LEN - 1) {
        insert_newline();
        adjust_scroll();
    }
    
    editor.dirty = 1;
    mark_line_dirty(y);
}

static void delete_char(void) {
    int y = editor.cursor_y;
    int x = editor.cursor_x;
    
    int len = strlen(editor.lines[y]);
    if (x >= len) {
        if (y < editor.line_count - 1) {
            int next_len = strlen(editor.lines[y + 1]);
            if (len + next_len < MAX_LINE_LEN - 1) {
                strcat(editor.lines[y], editor.lines[y + 1]);
                
                for (int i = y + 1; i < editor.line_count - 1; i++) {
                    strcpy(editor.lines[i], editor.lines[i + 1]);
                }
                editor.line_count--;
                editor.lines[editor.line_count][0] = '\0';
                editor.dirty = 1;
                mark_line_dirty(y);
                mark_all_dirty(); /* Lines shifted */
            }
        }
        return;
    }
    
    for (int i = x; i < len; i++) {
        editor.lines[y][i] = editor.lines[y][i + 1];
    }
    
    editor.dirty = 1;
    mark_line_dirty(y);
}

static void backspace_char(void) {
    if (editor.cursor_x > 0) {
        editor.cursor_x--;
        delete_char();
    } else if (editor.cursor_y > 0) {
        int prev_y = editor.cursor_y - 1;
        int prev_len = strlen(editor.lines[prev_y]);
        int curr_len = strlen(editor.lines[editor.cursor_y]);
        
        if (prev_len + curr_len < MAX_LINE_LEN - 1) {
            strcat(editor.lines[prev_y], editor.lines[editor.cursor_y]);
            
            for (int i = editor.cursor_y; i < editor.line_count - 1; i++) {
                strcpy(editor.lines[i], editor.lines[i + 1]);
            }
            editor.line_count--;
            editor.lines[editor.line_count][0] = '\0';
            
            editor.cursor_y--;
            editor.cursor_x = prev_len;
            editor.dirty = 1;
            mark_all_dirty(); /* Lines shifted */
        }
    }
}

static void insert_newline(void) {
    if (editor.line_count >= MAX_LINES) return;
    
    int y = editor.cursor_y;
    int x = editor.cursor_x;
    
    char rest[MAX_LINE_LEN];
    strcpy(rest, &editor.lines[y][x]);
    editor.lines[y][x] = '\0';
    
    for (int i = editor.line_count; i > y + 1; i--) {
        strcpy(editor.lines[i], editor.lines[i - 1]);
    }
    
    editor.line_count++;
    strcpy(editor.lines[y + 1], rest);
    
    editor.cursor_y++;
    editor.cursor_x = 0;
    editor.dirty = 1;
    mark_line_dirty(y);
    mark_line_dirty(y + 1);
    mark_all_dirty(); /* Lines shifted */
}

static void move_cursor(int dx, int dy) {
    if (dy != 0) {
        editor.cursor_y += dy;
        if (editor.cursor_y < 0) editor.cursor_y = 0;
        if (editor.cursor_y >= editor.line_count) editor.cursor_y = editor.line_count - 1;
        
        int len = strlen(editor.lines[editor.cursor_y]);
        if (editor.cursor_x > len) editor.cursor_x = len;
    }
    
    if (dx != 0) {
        editor.cursor_x += dx;
        int len = strlen(editor.lines[editor.cursor_y]);
        if (editor.cursor_x < 0) editor.cursor_x = 0;
        if (editor.cursor_x > len) editor.cursor_x = len;
    }
}

static int load_file(const char *filename) {
    int fd = sys_open(filename, "r");
    if (fd < 0) return -1;
    
    editor.line_count = 0;
    int current_line = 0;
    editor.lines[0][0] = '\0';
    
    char buffer[1024];
    int bytes_read;
    
    while ((bytes_read = sys_read(fd, buffer, sizeof(buffer))) > 0) {
        for (int i = 0; i < bytes_read; i++) {
            if (buffer[i] == '\n') {
                /* End current line, start new one */
                current_line++;
                if (current_line >= MAX_LINES) break;
                editor.lines[current_line][0] = '\0';
            } else if (buffer[i] != '\r') {
                /* Add character to current line */
                int len = strlen(editor.lines[current_line]);
                if (len < MAX_LINE_LEN - 1) {
                    editor.lines[current_line][len] = buffer[i];
                    editor.lines[current_line][len + 1] = '\0';
                }
            }
        }
        if (current_line >= MAX_LINES) break;
    }
    
    /* Set line count */
    editor.line_count = current_line + 1;
    if (editor.line_count == 0) editor.line_count = 1;
    
    sys_close(fd);
    strcpy(editor.filename, filename);
    editor.dirty = 0;
    editor.cursor_x = 0;
    editor.cursor_y = 0;
    editor.scroll_offset = 0;
    mark_all_dirty();
    
    return 0;
}

static void prompt_open_file(void) {
    volatile uint16_t *vga = (uint16_t*)0xB8000;
    volatile uint16_t *status_line = vga + (SCREEN_HEIGHT - 1) * SCREEN_WIDTH;
    uint16_t color = ((7 << 4) | 0) << 8;
    
    /* Clear status line and write prompt */
    const char *prompt = "Open file: ";
    for (int i = 0; i < SCREEN_WIDTH; i++) {
        char ch = (i < 11) ? prompt[i] : ' ';
        status_line[i] = color | (unsigned char)ch;
    }
    
    /* Position cursor for input */
    sys_setcur(11, SCREEN_HEIGHT - 1);
    
    /* Read filename */
    char filename[256];
    sys_readline(filename, sizeof(filename));
    
    if (filename[0] != '\0') {
        if (load_file(filename) != 0) {
            /* Show error briefly */
            const char *error = "Error: Cannot open file!";
            for (int i = 0; i < SCREEN_WIDTH; i++) {
                char ch = (i < 25) ? error[i] : ' ';
                status_line[i] = (((12 << 4) | 15) << 8) | (unsigned char)ch; /* Red bg, white fg */
            }
            
            /* Wait a moment */
            for (volatile int i = 0; i < 10000000; i++);
        }
    }
    
    /* CRITICAL: Reset cursor to editor position before returning */
    int screen_y = EDIT_START_Y + (editor.cursor_y - editor.scroll_offset);
    sys_setcur(editor.cursor_x, screen_y);
}

static void prompt_filename(void) {
    volatile uint16_t *vga = (uint16_t*)0xB8000;
    volatile uint16_t *status_line = vga + (SCREEN_HEIGHT - 1) * SCREEN_WIDTH;
    uint16_t color = ((7 << 4) | 0) << 8;
    
    /* Clear status line and write prompt */
    const char *prompt = "Save as: ";
    for (int i = 0; i < SCREEN_WIDTH; i++) {
        char ch = (i < 9) ? prompt[i] : ' ';
        status_line[i] = color | (unsigned char)ch;
    }
    
    /* Position cursor for input */
    sys_setcur(9, SCREEN_HEIGHT - 1);
    
    /* Read filename using sys_readline */
    char filename[256];
    sys_readline(filename, sizeof(filename));
    
    if (filename[0] != '\0') {
        strcpy(editor.filename, filename);
    }
    
    /* CRITICAL: Reset cursor to editor position before returning */
    int screen_y = EDIT_START_Y + (editor.cursor_y - editor.scroll_offset);
    sys_setcur(editor.cursor_x, screen_y);
}

static int save_file(void) {
    if (!editor.filename[0]) return -1;
    
    int fd = sys_open(editor.filename, "w");
    if (fd < 0) return -1;
    
    for (int i = 0; i < editor.line_count; i++) {
        int len = strlen(editor.lines[i]);
        sys_write_file(fd, editor.lines[i], len);
        sys_write_file(fd, "\n", 1);
    }
    
    sys_close(fd);
    editor.dirty = 0;
    return 0;
}

__attribute__((section(".entry"), used))
void _start(void) {
    memset(&editor, 0, sizeof(editor));
    editor.line_count = 1;
    editor.lines[0][0] = '\0';
    editor.cursor_x = 0;
    editor.cursor_y = 0;
    editor.scroll_offset = 0;
    editor.full_redraw = 1;
    mark_all_dirty();
    
    sys_setcolor(0, 7);
    redraw_screen();
    
    sys_setcur(0, EDIT_START_Y);
    
    while (1) {
        int ch = sys_getchar();
        
        /* F2 = Save */
        if (ch == KEY_F2) {
            if (!editor.filename[0]) {
                prompt_filename();
            }
            if (editor.filename[0]) {
                save_file();
            }
            redraw_screen();
            continue;
        }
        
        /* F3 = Open */
        if (ch == KEY_F3) {
            prompt_open_file();
            redraw_screen();
            continue;
        }
        
        /* F10 = Exit */
        if (ch == KEY_F10) {
            if (editor.dirty) {
                volatile uint16_t *vga = (uint16_t*)0xB8000;
                volatile uint16_t *status_line = vga + (SCREEN_HEIGHT - 1) * SCREEN_WIDTH;
                uint16_t color = ((1 << 4) | 15) << 8; /* Blue bg, white fg */
                
                const char *msg = "Unsaved changes! Press F10 again to quit";
                for (int i = 0; i < SCREEN_WIDTH; i++) {
                    char ch_msg = (i < 42) ? msg[i] : ' ';
                    status_line[i] = color | (unsigned char)ch_msg;
                }
                
                int ch2 = sys_getchar();
                if (ch2 != KEY_F10) {
                    redraw_screen();
                    continue;
                }
            }
            break;
        }
        
        if (ch == KEY_UP) {
            move_cursor(0, -1);
            adjust_scroll();
            redraw_screen();
            continue;
        }
        if (ch == KEY_DOWN) {
            move_cursor(0, 1);
            adjust_scroll();
            redraw_screen();
            continue;
        }
        if (ch == KEY_LEFT) {
            move_cursor(-1, 0);
            redraw_screen();
            continue;
        }
        if (ch == KEY_RIGHT) {
            move_cursor(1, 0);
            redraw_screen();
            continue;
        }
        
        if (ch == KEY_DELETE) {
            delete_char();
            redraw_screen();
            continue;
        }
        
        if (ch == '\b') {
            backspace_char();
            adjust_scroll();
            redraw_screen();
            continue;
        }
        
        if (ch == '\n' || ch == '\r') {
            insert_newline();
            adjust_scroll();
            redraw_screen();
            continue;
        }
        
        if (ch >= 32 && ch < 127) {
            insert_char((char)ch);
            redraw_screen();
        }
    }
    
    sys_clear();
    sys_exit();
}