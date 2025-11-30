/* osLET Editor */
/* Claude by Anthropic, 2025 */
/* Revised by EinarTheSad */

#include "../syscall.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "../drivers/keyboard.h"
#include <stdint.h>

#define MAX_LINES 200
#define MAX_LINE_LEN 79
#define SCREEN_HEIGHT 25
#define SCREEN_WIDTH 80
#define EDIT_HEIGHT 22
#define EDIT_START_Y 2
#define EDIT_START_X 1
#define EDIT_WIDTH 78
#define UNDO_STACK_SIZE 50

/* VGA colors */
#define COL_MENU      ((0 << 4) | 7)   /* Black bg, light gray fg */
#define COL_MENU_HOT  ((0 << 4) | 15)  /* Black bg, white (hotkey) */
#define COL_MENU_SEL  ((7 << 4) | 0)   /* Gray bg, black fg (selected) */
#define COL_EDIT      ((1 << 4) | 7)   /* Blue bg, light grey fg */
#define COL_BORDER    ((1 << 4) | 11)  /* Blue bg, cyan fg */
#define COL_TITLE     ((7 << 4) | 0)   /* Light grey bg, black fg */
#define COL_STATUS    ((3 << 4) | 0)   /* Cyan bg, black fg */
#define COL_DROPDOWN  ((7 << 4) | 0)   /* Gray bg, black fg */
#define COL_DROP_HOT  ((7 << 4) | 1)   /* Gray bg, blue (hotkey) */
#define COL_DROP_SEL  ((0 << 4) | 7)   /* Black bg, white (selected) */

/* Box drawing characters (CP437) */
#define CHAR_HLINE    196
#define CHAR_VLINE    179
#define CHAR_TL       218
#define CHAR_TR       191
#define CHAR_BL       192
#define CHAR_BR       217

typedef struct {
    char lines[MAX_LINES][MAX_LINE_LEN + 1];
    int line_count;
    int cursor_x;
    int cursor_y;
    int scroll_offset;
    int dirty;
    char filename[64];
} editor_t;

/* Undo/redo snapshot */
typedef struct {
    char lines[MAX_LINES][MAX_LINE_LEN + 1];
    int line_count;
    int cursor_x;
    int cursor_y;
} undo_state_t;

/* Menu structure */
typedef struct {
    const char *title;
    char hotkey;
    int x_pos;
} menu_item_t;

typedef struct {
    const char *text;
    char hotkey;
    int key_code;
    const char *shortcut;
} dropdown_item_t;

static editor_t ed;
static volatile uint16_t *vga = (uint16_t*)0xB8000;

/* Undo/redo stacks */
static undo_state_t undo_stack[UNDO_STACK_SIZE];
static undo_state_t redo_stack[UNDO_STACK_SIZE];
static int undo_top = -1;
static int redo_top = -1;
static int typing_batch = 0; /* 1 if currently in typing session */

/* Menus */
static menu_item_t menus[] = {
    {"File",   'F', 1},
    {"Edit",   'E', 7},
    {"Help",   'H', 13},
};
#define NUM_MENUS 3

static dropdown_item_t file_menu[] = {
    {"New",       'N', KEY_ALT_N, ""},
    {"Open...",   'O', KEY_F3,    "F3"},
    {"Save",      'S', KEY_F2,    "F2"},
    {"Save As...", 'A', 0,        ""},
    {"Exit",      'x', KEY_ALT_X, ""},
};
#define FILE_MENU_SIZE 5

static dropdown_item_t edit_menu[] = {
    {"Undo",       'U', KEY_ALT_Z, "Alt+Z"},
    {"Redo",       'R', KEY_ALT_Y, "Alt+Y"},
    {"Clear All",  'C', 0,         ""},
    {"Delete Line",'D', KEY_ALT_D, "Alt+D"},
};
#define EDIT_MENU_SIZE 4

static dropdown_item_t help_menu[] = {
    {"About...", 'A', 0, ""},
};
#define HELP_MENU_SIZE 1

/* Forward declarations */
static void draw_all(void);
static void draw_menu_bar(void);
static void draw_border(void);
static void draw_edit_area(void);
static void draw_status(void);
static void update_cursor(void);
static void adjust_scroll(void);
static void insert_char(char c);
static void delete_char(void);
static void backspace_char(void);
static void insert_newline(void);
static void move_cursor(int dx, int dy);
static int load_file(const char *filename);
static int save_file(void);
static void prompt_filename(const char *prompt, char *buf, int maxlen);
static void show_dropdown(int menu_idx);
static void show_about(void);
static void new_file(void);
static int ask_yes_no(const char *question);
static void show_message(const char *msg);
static void save_undo_state(void);
static void perform_undo(void);
static void perform_redo(void);

static void vga_putc(int x, int y, char c, uint8_t attr) {
    if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT)
        vga[y * SCREEN_WIDTH + x] = ((uint16_t)attr << 8) | (uint8_t)c;
}

static void vga_puts(int x, int y, const char *s, uint8_t attr) {
    while (*s && x < SCREEN_WIDTH) {
        vga_putc(x++, y, *s++, attr);
    }
}

static void vga_fill(int x, int y, int w, int h, char c, uint8_t attr) {
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            vga_putc(x + i, y + j, c, attr);
        }
    }
}

static void draw_menu_bar(void) {
    /* Clear menu line with black bg */
    vga_fill(0, 0, SCREEN_WIDTH, 1, ' ', COL_MENU);
    
    /* Draw menu items */
    for (int i = 0; i < NUM_MENUS; i++) {
        int x = menus[i].x_pos;
        const char *t = menus[i].title;
        
        /* First letter is hotkey - show brighter */
        vga_putc(x, 0, t[0], COL_MENU_HOT);
        vga_puts(x + 1, 0, t + 1, COL_MENU);
    }
}

static void draw_border(void) {
    /* Top border (row 1) */
    vga_putc(0, 1, CHAR_TL, COL_BORDER);
    for (int x = 1; x < SCREEN_WIDTH - 1; x++)
        vga_putc(x, 1, CHAR_HLINE, COL_BORDER);
    vga_putc(SCREEN_WIDTH - 1, 1, CHAR_TR, COL_BORDER);
    
    /* Side borders and edit area background */
    for (int y = EDIT_START_Y; y < EDIT_START_Y + EDIT_HEIGHT; y++) {
        vga_putc(0, y, CHAR_VLINE, COL_BORDER);
        for (int x = 1; x < SCREEN_WIDTH - 1; x++)
            vga_putc(x, y, ' ', COL_EDIT);
        vga_putc(SCREEN_WIDTH - 1, y, CHAR_VLINE, COL_BORDER);
    }
    
    /* Bottom border (row 24) */
    vga_putc(0, SCREEN_HEIGHT - 1, CHAR_BL, COL_BORDER);
    for (int x = 1; x < SCREEN_WIDTH - 1; x++)
        vga_putc(x, SCREEN_HEIGHT - 1, CHAR_HLINE, COL_BORDER);
    vga_putc(SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1, CHAR_BR, COL_BORDER);
    
    /* Filename in title bar (centered on row 1) */
    char title[40];
    if (ed.filename[0])
        snprintf(title, sizeof(title), " %s ", ed.filename);
    else
        snprintf(title, sizeof(title), " Untitled ");
    
    int title_x = (SCREEN_WIDTH - strlen(title)) / 2;
    vga_puts(title_x, 1, title, COL_TITLE);
}

static void draw_edit_area(void) {
    for (int i = 0; i < EDIT_HEIGHT; i++) {
        int line_idx = i + ed.scroll_offset;
        int screen_y = EDIT_START_Y + i;
        
        /* Clear line first */
        for (int x = 1; x < SCREEN_WIDTH - 1; x++)
            vga_putc(x, screen_y, ' ', COL_EDIT);
        
        /* Draw line content if exists */
        if (line_idx < ed.line_count) {
            char *line = ed.lines[line_idx];
            int len = strlen(line);
            for (int x = 0; x < len && x < EDIT_WIDTH; x++) {
                vga_putc(EDIT_START_X + x, screen_y, line[x], COL_EDIT);
            }
        }
    }
}

static void draw_status(void) {
    /* Status bar at bottom - inside border area */
    char status[SCREEN_WIDTH];
    snprintf(status, sizeof(status), " %c | Line:%d Column:%d |",
             ed.dirty ? '*' : ' ', ed.cursor_y + 1, ed.cursor_x + 1);
    
    /* Pad to width */
    int len = strlen(status);
    while (len < SCREEN_WIDTH - 2) status[len++] = ' ';
    status[SCREEN_WIDTH - 2] = '\0';
    
    /* Write over bottom border line */
    vga_puts(1, SCREEN_HEIGHT - 1, status, COL_STATUS);
}

static void draw_all(void) {
    draw_menu_bar();
    draw_border();
    draw_edit_area();
    draw_status();
    update_cursor();
}

static void update_cursor(void) {
    int screen_x = EDIT_START_X + ed.cursor_x;
    int screen_y = EDIT_START_Y + (ed.cursor_y - ed.scroll_offset);
    sys_setcur(screen_x, screen_y);
}

static void adjust_scroll(void) {
    if (ed.cursor_y >= ed.scroll_offset + EDIT_HEIGHT) {
        ed.scroll_offset = ed.cursor_y - EDIT_HEIGHT + 1;
        draw_edit_area();
    }
    if (ed.cursor_y < ed.scroll_offset) {
        ed.scroll_offset = ed.cursor_y;
        draw_edit_area();
    }
}

static void insert_char(char c) {
    int y = ed.cursor_y;
    int x = ed.cursor_x;
    
    int len = strlen(ed.lines[y]);
    
    /* If at end of line and line is full, wrap to next line */
    if (len >= MAX_LINE_LEN - 1) {
        /* Auto-wrap: create new line and put char there */
        if (ed.line_count < MAX_LINES) {
            insert_newline();
            y = ed.cursor_y;
            x = 0;
            len = 0;
        } else {
            return; /* Can't add more */
        }
    }
    
    /* Shift right */
    for (int i = len; i > x; i--)
        ed.lines[y][i] = ed.lines[y][i - 1];
    
    ed.lines[y][x] = c;
    ed.lines[y][len + 1] = '\0';
    ed.cursor_x++;
    
    /* If cursor went past line width, wrap */
    if (ed.cursor_x >= MAX_LINE_LEN) {
        if (ed.line_count < MAX_LINES) {
            insert_newline();
        } else {
            ed.cursor_x = MAX_LINE_LEN - 1;
        }
    }
    
    ed.dirty = 1;
}

static void delete_char(void) {
    save_undo_state();
    
    int y = ed.cursor_y;
    int x = ed.cursor_x;
    int len = strlen(ed.lines[y]);
    
    if (x >= len) {
        /* Join with next line */
        if (y < ed.line_count - 1) {
            int next_len = strlen(ed.lines[y + 1]);
            if (len + next_len < MAX_LINE_LEN) {
                strcat(ed.lines[y], ed.lines[y + 1]);
                for (int i = y + 1; i < ed.line_count - 1; i++)
                    strcpy(ed.lines[i], ed.lines[i + 1]);
                ed.line_count--;
                ed.dirty = 1;
            }
        }
        return;
    }
    
    /* Shift left */
    for (int i = x; i < len; i++)
        ed.lines[y][i] = ed.lines[y][i + 1];
    ed.dirty = 1;
}

static void backspace_char(void) {
    save_undo_state();
    
    if (ed.cursor_x > 0) {
        ed.cursor_x--;
        delete_char();
    } else if (ed.cursor_y > 0) {
        int prev_len = strlen(ed.lines[ed.cursor_y - 1]);
        int curr_len = strlen(ed.lines[ed.cursor_y]);
        
        if (prev_len + curr_len < MAX_LINE_LEN) {
            strcat(ed.lines[ed.cursor_y - 1], ed.lines[ed.cursor_y]);
            for (int i = ed.cursor_y; i < ed.line_count - 1; i++)
                strcpy(ed.lines[i], ed.lines[i + 1]);
            ed.line_count--;
            ed.cursor_y--;
            ed.cursor_x = prev_len;
            ed.dirty = 1;
        }
    }
}

static void insert_newline(void) {
    save_undo_state();
    
    if (ed.line_count >= MAX_LINES) return;
    
    int y = ed.cursor_y;
    int x = ed.cursor_x;
    
    /* Save remainder of current line */
    char rest[MAX_LINE_LEN + 1];
    strcpy(rest, &ed.lines[y][x]);
    ed.lines[y][x] = '\0';
    
    /* Shift lines down */
    for (int i = ed.line_count; i > y + 1; i--)
        strcpy(ed.lines[i], ed.lines[i - 1]);
    
    ed.line_count++;
    strcpy(ed.lines[y + 1], rest);
    
    ed.cursor_y++;
    ed.cursor_x = 0;
    ed.dirty = 1;
}

static void move_cursor(int dx, int dy) {
    if (dy != 0) {
        ed.cursor_y += dy;
        if (ed.cursor_y < 0) ed.cursor_y = 0;
        if (ed.cursor_y >= ed.line_count) ed.cursor_y = ed.line_count - 1;
        
        int len = strlen(ed.lines[ed.cursor_y]);
        if (ed.cursor_x > len) ed.cursor_x = len;
    }
    
    if (dx != 0) {
        ed.cursor_x += dx;
        int len = strlen(ed.lines[ed.cursor_y]);
        if (ed.cursor_x < 0) ed.cursor_x = 0;
        if (ed.cursor_x > len) ed.cursor_x = len;
    }
}

static int load_file(const char *filename) {
    int fd = sys_open(filename, "r");
    if (fd < 0) return -1;
    
    memset(&ed, 0, sizeof(ed));
    ed.line_count = 1;
    
    char buffer[512];
    int bytes_read;
    int current_line = 0;
    
    while ((bytes_read = sys_read(fd, buffer, sizeof(buffer))) > 0) {
        for (int i = 0; i < bytes_read; i++) {
            if (buffer[i] == '\n') {
                current_line++;
                if (current_line >= MAX_LINES) break;
            } else if (buffer[i] != '\r') {
                int len = strlen(ed.lines[current_line]);
                if (len < MAX_LINE_LEN) {
                    ed.lines[current_line][len] = buffer[i];
                    ed.lines[current_line][len + 1] = '\0';
                }
            }
        }
        if (current_line >= MAX_LINES) break;
    }
    
    ed.line_count = current_line + 1;
    if (ed.line_count <= 0) ed.line_count = 1;
    
    sys_close(fd);
    
    /* Copy filename, truncate if needed */
    int j = 0;
    for (int i = 0; filename[i] && j < 63; i++) {
        ed.filename[j++] = filename[i];
    }
    ed.filename[j] = '\0';
    
    ed.dirty = 0;
    undo_top = -1;
    redo_top = -1;
    return 0;
}

static int save_file(void) {
    if (!ed.filename[0]) return -1;
    
    int fd = sys_open(ed.filename, "w");
    if (fd < 0) return -1;
    
    for (int i = 0; i < ed.line_count; i++) {
        int len = strlen(ed.lines[i]);
        sys_write_file(fd, ed.lines[i], len);
        sys_write_file(fd, "\n", 1);
    }
    
    sys_close(fd);
    ed.dirty = 0;
    return 0;
}

static void prompt_filename(const char *prompt, char *buf, int maxlen) {
    /* Draw dialog box */
    int box_w = 50;
    int box_h = 5;
    int box_x = (SCREEN_WIDTH - box_w) / 2;
    int box_y = (SCREEN_HEIGHT - box_h) / 2;
    
    /* Draw box */
    vga_fill(box_x, box_y, box_w, box_h, ' ', COL_DROPDOWN);
    
    /* Border */
    vga_putc(box_x, box_y, CHAR_TL, COL_DROPDOWN);
    vga_putc(box_x + box_w - 1, box_y, CHAR_TR, COL_DROPDOWN);
    vga_putc(box_x, box_y + box_h - 1, CHAR_BL, COL_DROPDOWN);
    vga_putc(box_x + box_w - 1, box_y + box_h - 1, CHAR_BR, COL_DROPDOWN);
    for (int x = 1; x < box_w - 1; x++) {
        vga_putc(box_x + x, box_y, CHAR_HLINE, COL_DROPDOWN);
        vga_putc(box_x + x, box_y + box_h - 1, CHAR_HLINE, COL_DROPDOWN);
    }
    for (int y = 1; y < box_h - 1; y++) {
        vga_putc(box_x, box_y + y, CHAR_VLINE, COL_DROPDOWN);
        vga_putc(box_x + box_w - 1, box_y + y, CHAR_VLINE, COL_DROPDOWN);
    }
    
    /* Prompt */
    vga_puts(box_x + 2, box_y + 1, prompt, COL_DROPDOWN);
    
    /* Input field */
    vga_fill(box_x + 2, box_y + 2, box_w - 4, 1, ' ', COL_EDIT);
    sys_setcur(box_x + 2, box_y + 2);
    
    /* Read input */
    int pos = 0;
    buf[0] = '\0';
    
    while (1) {
        int ch = sys_getchar();
        
        if (ch == '\n' || ch == '\r') break;
        if (ch == KEY_ESC) { buf[0] = '\0'; break; }
        
        if (ch == '\b' && pos > 0) {
            pos--;
            buf[pos] = '\0';
            vga_putc(box_x + 2 + pos, box_y + 2, ' ', COL_EDIT);
            sys_setcur(box_x + 2 + pos, box_y + 2);
        } else if (ch >= 32 && ch < 127 && pos < maxlen - 1) {
            buf[pos] = ch;
            buf[pos + 1] = '\0';
            vga_putc(box_x + 2 + pos, box_y + 2, ch, COL_EDIT);
            pos++;
            sys_setcur(box_x + 2 + pos, box_y + 2);
        }
    }
    
    draw_all();
}

static void show_about(void) {
    int box_w = 40;
    int box_h = 7;
    int box_x = (SCREEN_WIDTH - box_w) / 2;
    int box_y = (SCREEN_HEIGHT - box_h) / 2;
    
    vga_fill(box_x, box_y, box_w, box_h, ' ', COL_DROPDOWN);
    
    /* Border */
    vga_putc(box_x, box_y, CHAR_TL, COL_DROPDOWN);
    vga_putc(box_x + box_w - 1, box_y, CHAR_TR, COL_DROPDOWN);
    vga_putc(box_x, box_y + box_h - 1, CHAR_BL, COL_DROPDOWN);
    vga_putc(box_x + box_w - 1, box_y + box_h - 1, CHAR_BR, COL_DROPDOWN);
    for (int x = 1; x < box_w - 1; x++) {
        vga_putc(box_x + x, box_y, CHAR_HLINE, COL_DROPDOWN);
        vga_putc(box_x + x, box_y + box_h - 1, CHAR_HLINE, COL_DROPDOWN);
    }
    for (int y = 1; y < box_h - 1; y++) {
        vga_putc(box_x, box_y + y, CHAR_VLINE, COL_DROPDOWN);
        vga_putc(box_x + box_w - 1, box_y + y, CHAR_VLINE, COL_DROPDOWN);
    }
    
    char *line1 = "osLET Text Editor v0.3";
    char *line2 = "EinarTheSad, 2025";
    vga_puts(box_w - strlen(line1)/2, box_y + 2, line1, COL_DROPDOWN);
    vga_puts(box_w - strlen(line2)/2, box_y + 4, line2, COL_DROPDOWN);
    
    sys_getchar();
    draw_all();
}

static int ask_yes_no(const char *question) {
    int box_w = 50;
    int box_h = 6;
    int box_x = (SCREEN_WIDTH - box_w) / 2;
    int box_y = (SCREEN_HEIGHT - box_h) / 2;
    
    vga_fill(box_x, box_y, box_w, box_h, ' ', COL_DROPDOWN);
    
    /* Border */
    vga_putc(box_x, box_y, CHAR_TL, COL_DROPDOWN);
    vga_putc(box_x + box_w - 1, box_y, CHAR_TR, COL_DROPDOWN);
    vga_putc(box_x, box_y + box_h - 1, CHAR_BL, COL_DROPDOWN);
    vga_putc(box_x + box_w - 1, box_y + box_h - 1, CHAR_BR, COL_DROPDOWN);
    for (int x = 1; x < box_w - 1; x++) {
        vga_putc(box_x + x, box_y, CHAR_HLINE, COL_DROPDOWN);
        vga_putc(box_x + x, box_y + box_h - 1, CHAR_HLINE, COL_DROPDOWN);
    }
    for (int y = 1; y < box_h - 1; y++) {
        vga_putc(box_x, box_y + y, CHAR_VLINE, COL_DROPDOWN);
        vga_putc(box_x + box_w - 1, box_y + y, CHAR_VLINE, COL_DROPDOWN);
    }
    
    vga_puts((box_x + 2)+((strlen(question)/2)-1), box_y + 2, question, COL_DROPDOWN);
    vga_puts(box_x + 17, box_y + 4, "  Y", (15 << 4) | 4);
    vga_puts(box_x + 20, box_y + 4, "es  ", (15 << 4) | 0);
    vga_puts(box_x + 25, box_y + 4, "  N", (15 << 4) | 4);
    vga_puts(box_x + 28, box_y + 4, "o  ", (15 << 4) | 0);

    
    while (1) {
        int ch = sys_getchar();
        if (ch == 'y' || ch == 'Y') {
            draw_all();
            return 1;
        }
        if (ch == 'n' || ch == 'N' || ch == KEY_ESC) {
            draw_all();
            return 0;
        }
    }
}

static void show_message(const char *msg) {
    int msg_len = strlen(msg);
    int box_w = (msg_len > 40) ? msg_len + 4 : 44;
    int box_h = 5;
    int box_x = (SCREEN_WIDTH - box_w) / 2;
    int box_y = (SCREEN_HEIGHT - box_h) / 2;
    
    vga_fill(box_x, box_y, box_w, box_h, ' ', COL_DROPDOWN);
    
    vga_putc(box_x, box_y, CHAR_TL, COL_DROPDOWN);
    vga_putc(box_x + box_w - 1, box_y, CHAR_TR, COL_DROPDOWN);
    vga_putc(box_x, box_y + box_h - 1, CHAR_BL, COL_DROPDOWN);
    vga_putc(box_x + box_w - 1, box_y + box_h - 1, CHAR_BR, COL_DROPDOWN);
    for (int x = 1; x < box_w - 1; x++) {
        vga_putc(box_x + x, box_y, CHAR_HLINE, COL_DROPDOWN);
        vga_putc(box_x + x, box_y + box_h - 1, CHAR_HLINE, COL_DROPDOWN);
    }
    for (int y = 1; y < box_h - 1; y++) {
        vga_putc(box_x, box_y + y, CHAR_VLINE, COL_DROPDOWN);
        vga_putc(box_x + box_w - 1, box_y + y, CHAR_VLINE, COL_DROPDOWN);
    }
    
    int text_x = box_x + (box_w - msg_len) / 2;
    vga_puts(text_x, box_y + 2, msg, COL_DROPDOWN);
    
    sys_getchar();
    draw_all();
}

static void save_undo_state(void) {
    /* Push current state to undo stack */
    if (undo_top < UNDO_STACK_SIZE - 1) {
        undo_top++;
    } else {
        /* Shift stack down */
        for (int i = 0; i < UNDO_STACK_SIZE - 1; i++) {
            undo_stack[i] = undo_stack[i + 1];
        }
    }
    
    /* Save state */
    undo_state_t *state = &undo_stack[undo_top];
    for (int i = 0; i < ed.line_count && i < MAX_LINES; i++) {
        strcpy(state->lines[i], ed.lines[i]);
    }
    state->line_count = ed.line_count;
    state->cursor_x = ed.cursor_x;
    state->cursor_y = ed.cursor_y;
    
    /* Clear redo stack */
    redo_top = -1;
}

static void perform_undo(void) {
    typing_batch = 0;
    
    if (undo_top < 0) {
        show_message("Nothing to undo!");
        return;
    }
    
    /* Save current state to redo stack */
    if (redo_top < UNDO_STACK_SIZE - 1) {
        redo_top++;
    } else {
        /* Shift stack down */
        for (int i = 0; i < UNDO_STACK_SIZE - 1; i++) {
            redo_stack[i] = redo_stack[i + 1];
        }
    }
    
    undo_state_t *redo_state = &redo_stack[redo_top];
    for (int i = 0; i < ed.line_count && i < MAX_LINES; i++) {
        strcpy(redo_state->lines[i], ed.lines[i]);
    }
    redo_state->line_count = ed.line_count;
    redo_state->cursor_x = ed.cursor_x;
    redo_state->cursor_y = ed.cursor_y;
    
    /* Restore from undo stack */
    undo_state_t *state = &undo_stack[undo_top];
    for (int i = 0; i < state->line_count && i < MAX_LINES; i++) {
        strcpy(ed.lines[i], state->lines[i]);
    }
    /* Clear remaining lines */
    for (int i = state->line_count; i < ed.line_count && i < MAX_LINES; i++) {
        ed.lines[i][0] = '\0';
    }
    ed.line_count = state->line_count;
    ed.cursor_x = state->cursor_x;
    ed.cursor_y = state->cursor_y;
    
    undo_top--;
    
    adjust_scroll();
    draw_all();
}

static void perform_redo(void) {
    typing_batch = 0;
    
    if (redo_top < 0) {
        show_message("Nothing to redo!");
        return;
    }
    
    /* Save current state to undo stack */
    if (undo_top < UNDO_STACK_SIZE - 1) {
        undo_top++;
    } else {
        /* Shift stack down */
        for (int i = 0; i < UNDO_STACK_SIZE - 1; i++) {
            undo_stack[i] = undo_stack[i + 1];
        }
    }
    
    undo_state_t *undo_state = &undo_stack[undo_top];
    for (int i = 0; i < ed.line_count && i < MAX_LINES; i++) {
        strcpy(undo_state->lines[i], ed.lines[i]);
    }
    undo_state->line_count = ed.line_count;
    undo_state->cursor_x = ed.cursor_x;
    undo_state->cursor_y = ed.cursor_y;
    
    /* Restore from redo stack */
    undo_state_t *state = &redo_stack[redo_top];
    for (int i = 0; i < state->line_count && i < MAX_LINES; i++) {
        strcpy(ed.lines[i], state->lines[i]);
    }
    /* Clear remaining lines */
    for (int i = state->line_count; i < ed.line_count && i < MAX_LINES; i++) {
        ed.lines[i][0] = '\0';
    }
    ed.line_count = state->line_count;
    ed.cursor_x = state->cursor_x;
    ed.cursor_y = state->cursor_y;
    
    redo_top--;
    
    adjust_scroll();
    draw_all();
}

static void new_file(void) {
    if (ed.dirty) {
        if (ask_yes_no("Save changes before creating new file?")) {
            if (!ed.filename[0]) {
                char fname[64];
                prompt_filename("Save as:", fname, sizeof(fname));
                if (fname[0]) {
                    strcpy(ed.filename, fname);
                    save_file();
                }
            } else {
                save_file();
            }
        }
    }
    memset(&ed, 0, sizeof(ed));
    ed.line_count = 1;
    undo_top = -1;
    redo_top = -1;
    draw_all();
}

static void show_dropdown(int menu_idx) {
    dropdown_item_t *items;
    int item_count;
    int menu_x = menus[menu_idx].x_pos;
    int menu_w;
    
    switch (menu_idx) {
        case 0: items = file_menu;   item_count = FILE_MENU_SIZE;   menu_w = 20; break;
        case 1: items = edit_menu;   item_count = EDIT_MENU_SIZE;   menu_w = 20; break;
        case 2: items = help_menu;   item_count = HELP_MENU_SIZE;   menu_w = 16; break;
        default: return;
    }
    
    /* Highlight menu title */
    vga_putc(menu_x - 1, 0, ' ', COL_MENU_SEL);
    const char *t = menus[menu_idx].title;
    int tx = menu_x;
    while (*t) vga_putc(tx++, 0, *t++, COL_MENU_SEL);
    vga_putc(tx, 0, ' ', COL_MENU_SEL);
    
    /* Draw dropdown */
    int menu_y = 1;
    int selected = 0;
    
    while (1) {
        /* Draw menu items */
        for (int i = 0; i < item_count; i++) {
            uint8_t attr = (i == selected) ? COL_DROP_SEL : COL_DROPDOWN;
            uint8_t hot_attr = (i == selected) ? COL_DROP_SEL : COL_DROP_HOT;
            
            vga_fill(menu_x - 1, menu_y + i, menu_w, 1, ' ', attr);
            
            /* Draw item text with hotkey */
            const char *text = items[i].text;
            int x = menu_x;
            int found_hot = 0;
            while (*text) {
                if (!found_hot && *text == items[i].hotkey) {
                    vga_putc(x, menu_y + i, *text, hot_attr);
                    found_hot = 1;
                } else {
                    vga_putc(x, menu_y + i, *text, attr);
                }
                x++;
                text++;
            }
            
            /* Draw shortcut */
            if (items[i].shortcut[0]) {
                int sx = menu_x + menu_w - strlen(items[i].shortcut) - 2;
                vga_puts(sx, menu_y + i, items[i].shortcut, attr);
            }
        }
        
        int ch = sys_getchar();
        
        if (ch == KEY_UP) {
            selected--;
            if (selected < 0) selected = item_count - 1;
        } else if (ch == KEY_DOWN) {
            selected++;
            if (selected >= item_count) selected = 0;
        } else if (ch == KEY_LEFT) {
            draw_menu_bar();
            draw_border();
            draw_edit_area();
            show_dropdown((menu_idx + NUM_MENUS - 1) % NUM_MENUS);
            return;
        } else if (ch == KEY_RIGHT) {
            draw_menu_bar();
            draw_border();
            draw_edit_area();
            show_dropdown((menu_idx + 1) % NUM_MENUS);
            return;
        } else if (ch == KEY_ESC) {
            break;
        } else if (ch == '\n' || ch == '\r') {
            /* Execute selected item */
            int key = items[selected].key_code;
            draw_all();
            
            /* Handle action */
            if (menu_idx == 0) { /* File menu */
                switch (selected) {
                    case 0: new_file(); break;
                    case 1: { /* Open */
                        if (ed.dirty) {
                            draw_all();
                            if (ask_yes_no("Save changes before opening?")) {
                                if (!ed.filename[0]) {
                                    char fname[64];
                                    prompt_filename("Save as:", fname, sizeof(fname));
                                    if (fname[0]) {
                                        strcpy(ed.filename, fname);
                                        save_file();
                                    }
                                } else {
                                    save_file();
                                }
                            }
                        }
                        char fname[64];
                        prompt_filename("File to open:", fname, sizeof(fname));
                        if (fname[0]) {
                            if (load_file(fname) != 0) {
                                show_message("Error: Cannot open file!");
                            }
                        }
                        draw_all();
                        break;
                    }
                    case 2: /* Save */
                        if (!ed.filename[0]) {
                            char fname[64];
                            prompt_filename("Save as:", fname, sizeof(fname));
                            if (fname[0]) {
                                strcpy(ed.filename, fname);
                                save_file();
                            }
                        } else {
                            save_file();
                        }
                        draw_all();
                        break;
                    case 3: { /* Save As */
                        char fname[64];
                        prompt_filename("Save as:", fname, sizeof(fname));
                        if (fname[0]) {
                            strcpy(ed.filename, fname);
                            save_file();
                        }
                        draw_all();
                        break;
                    }
                    case 4: /* Exit */
                        if (ed.dirty) {
                            draw_all();
                            if (ask_yes_no("Save changes before exit?")) {
                                if (!ed.filename[0]) {
                                    char fname[64];
                                    prompt_filename("Save as:", fname, sizeof(fname));
                                    if (fname[0]) {
                                        strcpy(ed.filename, fname);
                                        save_file();
                                    }
                                } else {
                                    save_file();
                                }
                            }
                        }
                        sys_clear();
                        sys_exit();
                        break;
                }
            } else if (menu_idx == 2 && selected == 0) { /* Help menu */
                show_about();
            } else if (menu_idx == 1) { /* Edit menu */
                switch (selected) {
                    case 0: /* Undo */
                        perform_undo();
                        break;
                    case 1: /* Redo */
                        perform_redo();
                        break;
                    case 2: /* Clear All */
                        typing_batch = 0;
                        save_undo_state();
                        memset(&ed, 0, sizeof(ed));
                        ed.line_count = 1;
                        draw_all();
                        break;
                    case 3: /* Delete Line */
                        typing_batch = 0;
                        save_undo_state();
                        if (ed.line_count > 1) {
                            for (int i = ed.cursor_y; i < ed.line_count - 1; i++)
                                strcpy(ed.lines[i], ed.lines[i + 1]);
                            ed.line_count--;
                            if (ed.cursor_y >= ed.line_count)
                                ed.cursor_y = ed.line_count - 1;
                            ed.cursor_x = 0;
                            ed.dirty = 1;
                        } else {
                            ed.lines[0][0] = '\0';
                            ed.cursor_x = 0;
                            ed.dirty = 1;
                        }
                        draw_all();
                        break;
                }
            }
            return;
        } else {
            /* Check for hotkey */
            char upper_ch = (ch >= 'a' && ch <= 'z') ? (ch - 32) : ch;
            for (int i = 0; i < item_count; i++) {
                if (upper_ch == items[i].hotkey) {
                    selected = i;
                    /* Trigger immediately */
                    ch = '\n';
                    break;
                }
            }
        }
    }
    
    draw_all();
}

__attribute__((section(".entry"), used))
void _start(void) {
    memset(&ed, 0, sizeof(ed));
    ed.line_count = 1;
    undo_top = -1;
    redo_top = -1;
    typing_batch = 0;
    
    draw_all();
    
    while (1) {
        int ch = sys_getchar();
        
        /* Alt + key combinations for menus */
        if (ch == KEY_ALT_F) { show_dropdown(0); continue; }
        if (ch == KEY_ALT_E) { show_dropdown(1); continue; }
        if (ch == KEY_ALT_H) { show_dropdown(2); continue; }
        
        /* File shortcuts */
        if (ch == KEY_ALT_N) { new_file(); continue; }
        
        if (ch == KEY_ALT_O) { /* Open */
            if (ed.dirty) {
                if (ask_yes_no("Save changes before opening?")) {
                    if (!ed.filename[0]) {
                        char fname[64];
                        prompt_filename("Save as:", fname, sizeof(fname));
                        if (fname[0]) {
                            strcpy(ed.filename, fname);
                            save_file();
                        }
                    } else {
                        save_file();
                    }
                }
            }
            char fname[64];
            prompt_filename("File to open:", fname, sizeof(fname));
            if (fname[0]) {
                if (load_file(fname) != 0) {
                    show_message("Error: Cannot open file!");
                }
            }
            draw_all();
            continue;
        }
        
        /* Edit shortcuts */
        if (ch == KEY_ALT_Z) { perform_undo(); continue; }
        if (ch == KEY_ALT_Y) { perform_redo(); continue; }
        
        if (ch == KEY_ALT_D) { /* Delete Line */
            typing_batch = 0;
            save_undo_state();
            if (ed.line_count > 1) {
                for (int i = ed.cursor_y; i < ed.line_count - 1; i++)
                    strcpy(ed.lines[i], ed.lines[i + 1]);
                ed.line_count--;
                if (ed.cursor_y >= ed.line_count)
                    ed.cursor_y = ed.line_count - 1;
                ed.cursor_x = 0;
                ed.dirty = 1;
            } else {
                ed.lines[0][0] = '\0';
                ed.cursor_x = 0;
                ed.dirty = 1;
            }
            draw_all();
            continue;
        }
        
        /* Direct shortcuts */
        if (ch == KEY_ALT_X) {
            if (ed.dirty) {
                if (ask_yes_no("Save changes before exit?")) {
                    if (!ed.filename[0]) {
                        char fname[64];
                        prompt_filename("Save as:", fname, sizeof(fname));
                        if (fname[0]) {
                            strcpy(ed.filename, fname);
                            save_file();
                        }
                    } else {
                        save_file();
                    }
                }
            }
            sys_clear();
            sys_exit();
        }
        
        if (ch == KEY_F2) {
            if (!ed.filename[0]) {
                char fname[64];
                prompt_filename("Save as:", fname, sizeof(fname));
                if (fname[0]) {
                    strcpy(ed.filename, fname);
                    if (save_file() == 0) {
                        show_message("File saved successfully!");
                    } else {
                        show_message("Error: Cannot save file!");
                    }
                }
            } else {
                if (save_file() == 0) {
                    show_message("File saved successfully!");
                } else {
                    show_message("Error: Cannot save file!");
                }
            }
            draw_all();
            continue;
        }
        
        if (ch == KEY_F3) {
            if (ed.dirty) {
                if (ask_yes_no("Save changes before opening?")) {
                    if (!ed.filename[0]) {
                        char fname[64];
                        prompt_filename("Save as:", fname, sizeof(fname));
                        if (fname[0]) {
                            strcpy(ed.filename, fname);
                            save_file();
                        }
                    } else {
                        save_file();
                    }
                }
            }
            char fname[64];
            prompt_filename("File to open:", fname, sizeof(fname));
            if (fname[0]) {
                if (load_file(fname) != 0) {
                    show_message("Error: Cannot open file!");
                }
            }
            draw_all();
            continue;
        }
        
        if (ch == KEY_F10 || ch == KEY_ESC) {
            /* Activate menu bar */
            show_dropdown(0);
            continue;
        }
        
        /* Navigation */
        if (ch == KEY_UP)    { typing_batch = 0; move_cursor(0, -1); adjust_scroll(); draw_status(); update_cursor(); continue; }
        if (ch == KEY_DOWN)  { typing_batch = 0; move_cursor(0, 1);  adjust_scroll(); draw_status(); update_cursor(); continue; }
        if (ch == KEY_LEFT)  { typing_batch = 0; move_cursor(-1, 0); draw_status(); update_cursor(); continue; }
        if (ch == KEY_RIGHT) { typing_batch = 0; move_cursor(1, 0);  draw_status(); update_cursor(); continue; }
        
        if (ch == KEY_HOME) {
            typing_batch = 0;
            ed.cursor_x = 0;
            draw_status();
            update_cursor();
            continue;
        }
        
        if (ch == KEY_END) {
            typing_batch = 0;
            ed.cursor_x = strlen(ed.lines[ed.cursor_y]);
            draw_status();
            update_cursor();
            continue;
        }
        
        if (ch == KEY_PGUP) {
            typing_batch = 0;
            for (int i = 0; i < EDIT_HEIGHT; i++)
                move_cursor(0, -1);
            adjust_scroll();
            draw_edit_area();
            draw_status();
            update_cursor();
            continue;
        }
        
        if (ch == KEY_PGDN) {
            typing_batch = 0;
            for (int i = 0; i < EDIT_HEIGHT; i++)
                move_cursor(0, 1);
            adjust_scroll();
            draw_edit_area();
            draw_status();
            update_cursor();
            continue;
        }
        
        /* Editing */
        if (ch == '\t') { /* Tab = 4 spaces */
            typing_batch = 0;
            save_undo_state();
            for (int i = 0; i < 4; i++) {
                insert_char(' ');
            }
            draw_edit_area();
            draw_status();
            update_cursor();
            continue;
        }
        
        if (ch == KEY_DELETE) {
            typing_batch = 0;
            delete_char();
            draw_edit_area();
            draw_status();
            update_cursor();
            continue;
        }
        
        if (ch == '\b') {
            typing_batch = 0;
            backspace_char();
            adjust_scroll();
            draw_edit_area();
            draw_status();
            update_cursor();
            continue;
        }
        
        if (ch == '\n' || ch == '\r') {
            typing_batch = 0;
            insert_newline();
            adjust_scroll();
            draw_edit_area();
            draw_status();
            update_cursor();
            continue;
        }
        
        /* Printable characters */
        if (ch >= 32 && ch < 127) {
            if (!typing_batch) {
                save_undo_state();
                typing_batch = 1;
            }
            insert_char((char)ch);
            /* Redraw only current line */
            int screen_y = EDIT_START_Y + (ed.cursor_y - ed.scroll_offset);
            char *line = ed.lines[ed.cursor_y];
            int len = strlen(line);
            for (int x = 0; x < EDIT_WIDTH; x++) {
                char c = (x < len) ? line[x] : ' ';
                vga_putc(EDIT_START_X + x, screen_y, c, COL_EDIT);
            }
            draw_status();
            update_cursor();
        }
    }
}