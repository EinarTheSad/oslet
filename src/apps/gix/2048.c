#include "../../syscall.h"
#include "../../lib/stdio.h"
#include "../../lib/string.h"
#include "../../drivers/keyboard.h"

static void *Form1 = 0;
static int board[4][4] = {0};
static int score = 0;
static int game_over = 0;

#define ID_SCORE_LABEL 18
#define ID_SCORE_VALUE 19

#define MENU_FILE_NEW    1
#define MENU_FILE_EXIT  2
#define MENU_HELP_ABOUT 3

static int tile_colors[12] = {
    7,
    14,
    13,
    12,
    11,
    10,
    9,
    8,
    6,
    5,
    4,
    3
};

static void set_tile_color(int row, int col) {
    int id = 1 + row * 4 + col;
    int val = board[row][col];
    int color_idx = 0;
    
    if (val > 2) {
        int v = val;
        while (v > 4 && color_idx < 11) {
            v >>= 1;
            color_idx++;
        }
        if (v == 4) color_idx++;
    }
    
    sys_ctrl_set_prop(Form1, id, PROP_BG, tile_colors[color_idx]);
}

static void update_tile(int row, int col) {
    int id = 1 + row * 4 + col;
    int val = board[row][col];
    
    if (val == 0) {
        ctrl_set_text(Form1, id, "");
    } else {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", val);
        ctrl_set_text(Form1, id, buf);
    }
    set_tile_color(row, col);
}

static void update_all_tiles() {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            update_tile(r, c);
        }
    }
}

static void update_score() {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", score);
    ctrl_set_text(Form1, ID_SCORE_VALUE, buf);
}

static unsigned int rng_state = 12345;

static unsigned int simple_rand() {
    rng_state = rng_state * 1103515245 + 12345;
    return (rng_state >> 16) & 0x7FFF;
}

static void spawn_tile() {
    int empty[16];
    int count = 0;
    
    rng_state = sys_uptime();
    
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (board[r][c] == 0) {
                empty[count++] = r * 4 + c;
            }
        }
    }
    
    if (count > 0) {
        int idx = empty[simple_rand() % count];
        int row = idx / 4;
        int col = idx % 4;
        board[row][col] = (simple_rand() % 10 < 9) ? 2 : 4;
    }
}

static int slide_row(int *row) {
    int new[4] = {0, 0, 0, 0};
    int j = 0;
    int moved = 0;
    int gained = 0;
    
    for (int i = 0; i < 4; i++) {
        if (row[i] != 0) {
            if (j > 0 && new[j-1] == row[i]) {
                new[j-1] *= 2;
                gained += new[j-1];
                moved = 1;
                j--;
            } else {
                if (new[j] != row[i] || j == 0) {
                    if (j != i) moved = 1;
                    new[j] = row[i];
                }
            }
            j++;
        }
    }
    
    for (int i = 0; i < 4; i++) {
        if (row[i] != new[i]) moved = 1;
        row[i] = new[i];
    }
    
    score += gained;
    return moved;
}

static int move_left() {
    int moved = 0;
    for (int r = 0; r < 4; r++) {
        if (slide_row(board[r])) moved = 1;
    }
    return moved;
}

static int move_right() {
    int moved = 0;
    for (int r = 0; r < 4; r++) {
        int rev[4] = {board[r][3], board[r][2], board[r][1], board[r][0]};
        if (slide_row(rev)) {
            moved = 1;
            for (int c = 0; c < 4; c++) {
                board[r][c] = rev[3-c];
            }
        }
    }
    return moved;
}

static int move_up() {
    int moved = 0;
    for (int c = 0; c < 4; c++) {
        int col[4] = {board[0][c], board[1][c], board[2][c], board[3][c]};
        if (slide_row(col)) {
            moved = 1;
            for (int r = 0; r < 4; r++) {
                board[r][c] = col[r];
            }
        }
    }
    return moved;
}

static int move_down() {
    int moved = 0;
    for (int c = 0; c < 4; c++) {
        int col[4] = {board[3][c], board[2][c], board[1][c], board[0][c]};
        if (slide_row(col)) {
            moved = 1;
            for (int r = 0; r < 4; r++) {
                board[r][c] = col[3-r];
            }
        }
    }
    return moved;
}

static int can_move() {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (board[r][c] == 0) return 1;
            if (c < 3 && board[r][c] == board[r][c+1]) return 1;
            if (r < 3 && board[r][c] == board[r+1][c]) return 1;
        }
    }
    return 0;
}

static void check_game_over() {
    if (!can_move()) {
        game_over = 1;
        sys_win_msgbox("Game over!", "OK", "2048");
    }
}

static void init_game() {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            board[r][c] = 0;
        }
    }
    score = 0;
    game_over = 0;
    spawn_tile();
    spawn_tile();
    update_all_tiles();
    update_score();
}

static gui_control_t Form1_controls[] = {
    { .type = CTRL_BUTTON, .x = 8, .y = 8, .w = 40, .h = 40, .fg = 0, .bg = 7, .text = "", .id = 1, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_BUTTON, .x = 48, .y = 8, .w = 40, .h = 40, .fg = 0, .bg = 7, .text = "", .id = 2, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_BUTTON, .x = 88, .y = 8, .w = 40, .h = 40, .fg = 0, .bg = 7, .text = "", .id = 3, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_BUTTON, .x = 128, .y = 8, .w = 40, .h = 40, .fg = 0, .bg = 7, .text = "", .id = 4, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_BUTTON, .x = 8, .y = 48, .w = 40, .h = 40, .fg = 0, .bg = 7, .text = "", .id = 5, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_BUTTON, .x = 48, .y = 48, .w = 40, .h = 40, .fg = 0, .bg = 7, .text = "", .id = 6, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_BUTTON, .x = 88, .y = 48, .w = 40, .h = 40, .fg = 0, .bg = 7, .text = "", .id = 7, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_BUTTON, .x = 128, .y = 48, .w = 40, .h = 40, .fg = 0, .bg = 7, .text = "", .id = 8, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_BUTTON, .x = 8, .y = 88, .w = 40, .h = 40, .fg = 0, .bg = 7, .text = "", .id = 9, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_BUTTON, .x = 48, .y = 88, .w = 40, .h = 40, .fg = 0, .bg = 7, .text = "", .id = 10, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_BUTTON, .x = 88, .y = 88, .w = 40, .h = 40, .fg = 0, .bg = 7, .text = "", .id = 11, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_BUTTON, .x = 128, .y = 88, .w = 40, .h = 40, .fg = 0, .bg = 7, .text = "", .id = 12, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_BUTTON, .x = 8, .y = 128, .w = 40, .h = 40, .fg = 0, .bg = 7, .text = "", .id = 13, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_BUTTON, .x = 48, .y = 128, .w = 40, .h = 40, .fg = 0, .bg = 7, .text = "", .id = 14, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_BUTTON, .x = 88, .y = 128, .w = 40, .h = 40, .fg = 0, .bg = 7, .text = "", .id = 15, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_BUTTON, .x = 128, .y = 128, .w = 40, .h = 40, .fg = 0, .bg = 7, .text = "", .id = 16, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_LABEL, .x = 8, .y = 176, .w = 0, .h = 0, .fg = 0, .bg = -1, .text = "Score:", .id = ID_SCORE_LABEL, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_LABEL, .x = 56, .y = 176, .w = 0, .h = 0, .fg = 0, .bg = -1, .text = "0", .id = ID_SCORE_VALUE, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 }
};

static int Form1_handle_event(void *form, int event, void *userdata) {
    (void)userdata; (void)form;
    
    if (event > 0) {
        switch (event) {
            case MENU_FILE_NEW:
                init_game();
                sys_win_mark_dirty(Form1);
                return 0;
            case MENU_FILE_EXIT:
                return 1;
            case MENU_HELP_ABOUT:
                sys_win_msgbox("Version 1.0, EinarTheSad 2026. Original game by Gabriele Cirulli.", "OK", "About 2048");
                return 0;
        }
    }
    
    if (game_over) return 0;
    
    int key = sys_get_key_nonblock();
    if (key) {
        int moved = 0;
        switch (key) {
            case KEY_LEFT:
                moved = move_left();
                break;
            case KEY_RIGHT:
                moved = move_right();
                break;
            case KEY_UP:
                moved = move_up();
                break;
            case KEY_DOWN:
                moved = move_down();
                break;
        }
        
        if (moved) {
            spawn_tile();
            update_all_tiles();
            update_score();
            check_game_over();
            sys_win_mark_dirty(Form1);
        }
    }
    
    if (event == -2) {
        sys_win_draw(form);
        sys_win_force_full_redraw();
    }
    
    return 0;
}

__attribute__((section(".entry"), used))
void _start(void) {
    Form1 = sys_win_create_form("2048", 32, 24, 175, 240);
    sys_win_set_resizable(Form1, 0);
    sys_win_set_icon(Form1,"C:/ICONS/2048.ICO");
    if (!Form1) {
        sys_exit();
        return;
    }
    
    sys_win_menubar_enable(Form1);
    int file_menu = sys_win_menubar_add_menu(Form1, "File");
    sys_win_menubar_add_item(Form1, file_menu, "New game", MENU_FILE_NEW);
    sys_win_menubar_add_item(Form1, file_menu, "Exit", MENU_FILE_EXIT);
    int help_menu = sys_win_menubar_add_menu(Form1, "Help");
    sys_win_menubar_add_item(Form1, help_menu, "About", MENU_HELP_ABOUT);
    
    for (int i = 0; i < 18; i++) {
        sys_win_add_control(Form1, &Form1_controls[i]);
    }
    
    init_game();
    sys_win_draw(Form1);

    sys_win_run_event_loop(Form1, Form1_handle_event, NULL);

    sys_win_destroy_form(Form1);
    sys_exit();
}
