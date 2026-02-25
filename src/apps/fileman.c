#include "../syscall.h"
#include "../lib/stdio.h"
#include "../lib/string.h"

#define MENU_FILE_OPEN       101
#define MENU_FILE_DELETE     102
#define MENU_FILE_EXIT       103
#define MENU_EDIT_COPY       201
#define MENU_EDIT_PASTE      202
#define MENU_VIEW_REFRESH    301
#define MENU_HELP_ABOUT      401

#define ID_PATH_TEXTBOX      50
#define ID_BACK_BUTTON       51
#define ID_TREE_SCROLLBAR    60
#define ID_FILE_SCROLLBAR    61
#define ID_TREE_BASE         1000
#define ID_FILE_BASE         2000

#define MAX_TREE_ITEMS       32
#define MAX_FILE_ITEMS       64

#define TREE_X               11
#define TREE_Y               42
#define TREE_W               120
#define TREE_ITEM_H          18
#define TREE_SCROLLBAR_W     18
#define TREE_VISIBLE_ITEMS   10

#define FILES_X              (TREE_X + TREE_W + 12)
#define FILES_Y              42
#define FILES_W              288
#define FILE_ICON_W          48
#define FILE_ICON_H          58
#define FILE_COLS            5
#define FILE_PAD             8
#define FILE_SCROLLBAR_W     18
#define FILE_VISIBLE_ROWS    3

typedef struct {
    char name[64];
    char full_path[256];
    int is_directory;
    int indent_level;
} tree_item_t;

typedef struct {
    char name[64];
    char full_path[256];
    int is_directory;
    uint32_t size;
} file_item_t;

typedef struct {
    void *form;
    char current_path[256];
    tree_item_t tree_items[MAX_TREE_ITEMS];
    int tree_count;
    file_item_t file_items[MAX_FILE_ITEMS];
    int file_count;
    int tree_scroll_offset;
    int file_scroll_offset;
} fileman_state_t;

static fileman_state_t state;

static void ensure_dir_suffix(char *path, size_t size) {
    size_t len = strlen(path);
    if (len == 0 || size == 0) return;

    if (len == 2 && path[1] == ':') {
        if (len + 1 < size) {
            path[2] = '/';
            path[3] = '\0';
        }
        return;
    }

    if (path[len - 1] != '/' && len + 1 < size) {
        path[len] = '/';
        path[len + 1] = '\0';
    }
}

static void normalize_path(char *path, size_t size) {
    if (size == 0) return;
    path[size - 1] = '\0';
    ensure_dir_suffix(path, size);
}

static void build_path(char *dest, size_t dest_size, const char *base, const char *name) {
    if (!dest || dest_size == 0) return;

    dest[0] = '\0';
    if (base && base[0]) {
        snprintf(dest, dest_size, "%s", base);
    }

    size_t len = strlen(dest);
    if (len > 0 && dest[len - 1] != '/' && dest[len - 1] != ':') {
        if (len + 1 < dest_size) {
            dest[len] = '/';
            dest[len + 1] = '\0';
            len++;
        }
    }

    if (name && name[0] && len < dest_size - 1) {
        snprintf(dest + len, dest_size - len, "%s", name);
    }
}

static int path_exists(const char *path) {
    sys_dirent_t tmp[1];
    return sys_readdir(path, tmp, 1) >= 0;
}

static int set_current_path(const char *path, int show_error) {
    char new_path[256];
    strncpy(new_path, path, sizeof(new_path) - 1);
    new_path[sizeof(new_path) - 1] = '\0';
    normalize_path(new_path, sizeof(new_path));

    if (!path_exists(new_path)) {
        if (show_error) {
            sys_win_msgbox("Path not found", "OK", "File Manager");
            sys_win_redraw_all();
        }
        return 0;
    }

    strcpy(state.current_path, new_path);
    state.tree_scroll_offset = 0;
    state.file_scroll_offset = 0;
    return 1;
}

static void scan_directory(void) {
    sys_dirent_t entries[128];
    int count = sys_readdir(state.current_path, entries, 128);
    
    state.tree_count = 0;
    state.file_count = 0;
    
    if (count < 0) return;
    
    /* Build hierarchical tree showing path breakdown */
    
    /* Always start with drive root */
    if (state.tree_count < MAX_TREE_ITEMS) {
        tree_item_t *item = &state.tree_items[state.tree_count++];
        strcpy(item->name, "C:/");
        strcpy(item->full_path, "C:/");
        item->is_directory = 1;
        item->indent_level = 0;
    }
    
    /* Parse current path and add each level */
    if (strcmp(state.current_path, "C:/") != 0) {
        char temp_path[256];
        strcpy(temp_path, state.current_path);
        
        /* Remove trailing slash if present */
        int len = strlen(temp_path);
        if (len > 3 && temp_path[len - 1] == '/')
            temp_path[len - 1] = '\0';
        
        /* Skip the "C:/" prefix */
        char *path_part = temp_path + 3;
        char build_path_str[256] = "C:/";
        int level = 1;
        
        /* Tokenize by '/' */
        char *token = path_part;
        while (*token && state.tree_count < MAX_TREE_ITEMS) {
            char *slash = strchr(token, '/');
            size_t token_len = slash ? (size_t)(slash - token) : strlen(token);
            
            if (token_len > 0) {
                tree_item_t *item = &state.tree_items[state.tree_count++];
                strncpy(item->name, token, token_len);
                item->name[token_len] = '\0';
                
                /* Build full path up to this level */
                strcat(build_path_str, item->name);
                strcat(build_path_str, "/");
                strcpy(item->full_path, build_path_str);
                item->is_directory = 1;
                item->indent_level = level++;
            }
            
            if (!slash) break;
            token = slash + 1;
        }
    }
    
    /* Add subdirectories of current directory */
    int current_level = state.tree_count > 0 ? state.tree_items[state.tree_count - 1].indent_level + 1 : 1;
    if (strcmp(state.current_path, "C:/") == 0) current_level = 1;
    
    for (int i = 0; i < count && state.tree_count < MAX_TREE_ITEMS; i++) {
        if (entries[i].is_directory && strcmp(entries[i].name, ".") != 0 && strcmp(entries[i].name, "..") != 0) {
            tree_item_t *item = &state.tree_items[state.tree_count++];
            strncpy(item->name, entries[i].name, sizeof(item->name) - 1);
            item->name[sizeof(item->name) - 1] = '\0';
            build_path(item->full_path, sizeof(item->full_path), state.current_path, entries[i].name);
            item->is_directory = 1;
            item->indent_level = current_level;
        }
    }
    
    /* Add all files and folders to file pane */
    for (int i = 0; i < count && state.file_count < MAX_FILE_ITEMS; i++) {
        if (strcmp(entries[i].name, ".") == 0 || strcmp(entries[i].name, "..") == 0)
            continue;
            
        file_item_t *item = &state.file_items[state.file_count++];
        strncpy(item->name, entries[i].name, sizeof(item->name) - 1);
        item->name[sizeof(item->name) - 1] = '\0';
        build_path(item->full_path, sizeof(item->full_path), state.current_path, entries[i].name);
        item->is_directory = entries[i].is_directory;
        item->size = entries[i].size;
    }
}

static void clear_tree_controls(void) {
    for (int i = 0; i < TREE_VISIBLE_ITEMS; i++) {
        gui_control_t *ctrl = sys_win_get_control(state.form, ID_TREE_BASE + i);
        if (ctrl) ctrl_set_visible(state.form, ID_TREE_BASE + i, 0);
    }
}

static void clear_file_controls(void) {
    int visible_file_count = FILE_VISIBLE_ROWS * FILE_COLS;
    for (int i = 0; i < visible_file_count; i++) {
        gui_control_t *ctrl = sys_win_get_control(state.form, ID_FILE_BASE + i);
        if (ctrl) {
            ctrl_set_visible(state.form, ID_FILE_BASE + i, 0);
        }
    }
}

static void refresh_display(void) {
    scan_directory();
    clear_tree_controls();
    clear_file_controls();
    
    /* Update tree scrollbar */
    if (state.tree_count > TREE_VISIBLE_ITEMS) {
        ctrl_set_visible(state.form, ID_TREE_SCROLLBAR, 1);
        gui_control_t *scrollbar = sys_win_get_control(state.form, ID_TREE_SCROLLBAR);
        if (scrollbar) {
            scrollbar->max_length = state.tree_count - TREE_VISIBLE_ITEMS;
            if (state.tree_scroll_offset > scrollbar->max_length)
                state.tree_scroll_offset = scrollbar->max_length;
            scrollbar->cursor_pos = state.tree_scroll_offset;
        }
    } else {
        ctrl_set_visible(state.form, ID_TREE_SCROLLBAR, 0);
        state.tree_scroll_offset = 0;
    }
    
    /* Update path textbox with uppercase */
    char upper_path[256];
    strcpy(upper_path, state.current_path);
    str_toupper(upper_path);
    ctrl_set_text(state.form, ID_PATH_TEXTBOX, upper_path);
    
    /* Update tree items with scroll offset and indentation */
    for (int i = 0; i < TREE_VISIBLE_ITEMS && (i + state.tree_scroll_offset) < state.tree_count; i++) {
        int idx = i + state.tree_scroll_offset;
        gui_control_t *ctrl = sys_win_get_control(state.form, ID_TREE_BASE + i);
        if (ctrl) {
            char display_text[96];
            char indent[32] = "";

            /* Build indentation string for non-root entries */
            if (!(idx == 0 && strcmp(state.tree_items[idx].full_path, "C:/") == 0)) {
                for (int j = 0; j < state.tree_items[idx].indent_level && j < 8; j++) {
                    strcat(indent, "  ");
                }
            }

            if (idx == 0 && strcmp(state.tree_items[idx].full_path, "C:/") == 0) {
                /* main catalogue has no indent */
                snprintf(display_text, sizeof(display_text), "\x8D %s", state.tree_items[idx].name);
            } else {
                snprintf(display_text, sizeof(display_text), "%s\x8D %s", indent, state.tree_items[idx].name);
            }

            ctrl_set_visible(state.form, ID_TREE_BASE + i, 1);
            ctrl_set_text(state.form, ID_TREE_BASE + i, display_text);
        }
    }
    
    /* Update file scrollbar */
    int file_rows = (state.file_count + FILE_COLS - 1) / FILE_COLS;
    if (file_rows > FILE_VISIBLE_ROWS) {
        ctrl_set_visible(state.form, ID_FILE_SCROLLBAR, 1);
        gui_control_t *scrollbar = sys_win_get_control(state.form, ID_FILE_SCROLLBAR);
        if (scrollbar) {
            scrollbar->max_length = file_rows - FILE_VISIBLE_ROWS;
            if (state.file_scroll_offset > scrollbar->max_length)
                state.file_scroll_offset = scrollbar->max_length;
            scrollbar->cursor_pos = state.file_scroll_offset;
        }
    } else {
        ctrl_set_visible(state.form, ID_FILE_SCROLLBAR, 0);
        state.file_scroll_offset = 0;
    }
    
    /* Update file items with scroll offset */
    int visible_file_count = FILE_VISIBLE_ROWS * FILE_COLS;
    for (int i = 0; i < visible_file_count; i++) {
        int idx = (state.file_scroll_offset * FILE_COLS) + i;
        gui_control_t *ctrl = sys_win_get_control(state.form, ID_FILE_BASE + i);
        
        if (idx < state.file_count && ctrl) {
            ctrl_set_text(state.form, ID_FILE_BASE + i, state.file_items[idx].name);
            ctrl_set_visible(state.form, ID_FILE_BASE + i, 1);
            
            /* Set appropriate icon */
            if (state.file_items[idx].is_directory) {
                ctrl_set_image(state.form, ID_FILE_BASE + i, "C:/ICONS/DIRECTRY.ICO");
            } else {
                /* Different icons based on extension */
                const char *dot = strrchr(state.file_items[idx].name, '.');
                if (dot) {
                    if (strcasecmp(dot, ".elf") == 0) {
                        ctrl_set_image(state.form, ID_FILE_BASE + i, "C:/ICONS/EXE.ICO");
                    } else if (strcasecmp(dot, ".txt") == 0) {
                        ctrl_set_image(state.form, ID_FILE_BASE + i, "C:/ICONS/TEXT.ICO");
                    } else if (strcasecmp(dot, ".bmp") == 0 || (strcasecmp(dot, ".ico") == 0)) {
                        ctrl_set_image(state.form, ID_FILE_BASE + i, "C:/ICONS/BITMAP.ICO");
                    } else if (strcasecmp(dot, ".wav") == 0) {
                        ctrl_set_image(state.form, ID_FILE_BASE + i, "C:/ICONS/SOUND.ICO");
                    } else {
                        ctrl_set_image(state.form, ID_FILE_BASE + i, "C:/ICONS/FILE.ICO");
                    }
                } else {
                    ctrl_set_image(state.form, ID_FILE_BASE + i, "C:/ICONS/FILE.ICO");
                }
            }
        } else if (ctrl) {
            ctrl_set_visible(state.form, ID_FILE_BASE + i, 0);
        }
    }
    
    sys_win_draw(state.form);
}

/* Event handling */
static int handle_event(void *f, int event, void *userdata) {
    (void)f;
    (void)userdata;
    
    if (event == -3 || event == MENU_FILE_EXIT) {
        return 1;
    }
    
    if (event == 0) {
        return 0;
    }
    
    /* Handle path textbox - navigate when Enter is pressed */
    if (event == ID_PATH_TEXTBOX) {
        gui_control_t *textbox = sys_win_get_control(state.form, ID_PATH_TEXTBOX);
        if (textbox && textbox->text[0]) {
            char prev_path[256];
            strcpy(prev_path, state.current_path);
            if (set_current_path(textbox->text, 1)) {
                refresh_display();
            } else {
                strcpy(state.current_path, prev_path);
            }
        }
        return 0;
    }

    /* Back button pressed: go up one directory level */
    if (event == ID_BACK_BUTTON) {
        /* walk up from current path */
        if (strcmp(state.current_path, "C:/") != 0) {
            char new_path[256];
            strcpy(new_path, state.current_path);

            int len = strlen(new_path);
            if (len > 0 && new_path[len - 1] == '/')
                new_path[--len] = '\0';
            char *p = strrchr(new_path, '/');
            if (p && p > new_path + 2) {
                *p = '\0';
                normalize_path(new_path, sizeof(new_path));
            } else {
                strcpy(new_path, "C:/");
            }

            if (set_current_path(new_path, 0)) {
                refresh_display();
            }
        }
        return 0;
    }
    
    /* Handle tree scrollbar */
    if (event == ID_TREE_SCROLLBAR) {
        gui_control_t *scrollbar = sys_win_get_control(state.form, ID_TREE_SCROLLBAR);
        if (scrollbar) {
            state.tree_scroll_offset = scrollbar->cursor_pos;
            refresh_display();
        }
        return 0;
    }
    
    /* Handle file scrollbar */
    if (event == ID_FILE_SCROLLBAR) {
        gui_control_t *scrollbar = sys_win_get_control(state.form, ID_FILE_SCROLLBAR);
        if (scrollbar) {
            state.file_scroll_offset = scrollbar->cursor_pos;
            refresh_display();
        }
        return 0;
    }
    
    /* Handle tree (folder) clicks */
    if (event >= ID_TREE_BASE && event < ID_TREE_BASE + TREE_VISIBLE_ITEMS) {
        int visible_idx = event - ID_TREE_BASE;
        int actual_idx = visible_idx + state.tree_scroll_offset;
        if (actual_idx >= 0 && actual_idx < state.tree_count) {
            if (set_current_path(state.tree_items[actual_idx].full_path, 0)) {
                refresh_display();
            }
        }
        return 0;
    }
    
    /* Handle file/folder icon clicks */
    if (event >= ID_FILE_BASE && event < ID_FILE_BASE + (FILE_VISIBLE_ROWS * FILE_COLS)) {
        int visible_idx = event - ID_FILE_BASE;
        int actual_idx = (state.file_scroll_offset * FILE_COLS) + visible_idx;
        if (actual_idx >= 0 && actual_idx < state.file_count) {
            if (state.file_items[actual_idx].is_directory) {
                if (set_current_path(state.file_items[actual_idx].full_path, 0)) {
                    refresh_display();
                }
            } else {
                const char *dot = strrchr(state.file_items[actual_idx].name, '.');
                if (dot && strcasecmp(dot, ".elf") == 0) {
                    sys_spawn_async(state.file_items[actual_idx].full_path);
                } else {
                    /* TODO: handle files by using default programs,
                       for instance open images with Image Viewer
                    */
                    char msg[128];
                    snprintf(msg, sizeof(msg), "No functionality to open %s yet", state.file_items[actual_idx].name);
                    sys_win_msgbox(msg, "OK", "File Manager");
                    sys_win_redraw_all();
                }
            }
        }
        return 0;
    }
    
    /* Handle menu selections */
    if (event == MENU_FILE_DELETE) {
        sys_win_msgbox("Not implemented", "OK", "File Manager");
        sys_win_redraw_all();
        return 0;
    }
    
    if (event == MENU_VIEW_REFRESH) {
        refresh_display();
        return 0;
    }
    
    if (event == MENU_HELP_ABOUT) {
        sys_win_msgbox("osLET File Manager\n\nThis is a work in progress!", "OK", "About");
        sys_win_redraw_all();
        return 0;
    }
    
    if (event == MENU_FILE_OPEN) {
        sys_win_msgbox("Not implemented", "OK", "File Manager");
        sys_win_redraw_all();
        return 0;
    }
    
    if (event == MENU_EDIT_COPY || event == MENU_EDIT_PASTE) {
        sys_win_msgbox("Not implemented", "OK", "File Manager");
        sys_win_redraw_all();
        return 0;
    }
    
    return 0;
}

/* Main entry point */
__attribute__((section(".entry"), used))
void _start(void) {
    /* Initialize state */
    memset(&state, 0, sizeof(state));
    
    /* Get current directory */
    sys_getcwd(state.current_path, sizeof(state.current_path));
    if (!state.current_path[0]) {
        strcpy(state.current_path, "C:/");
    }
    normalize_path(state.current_path, sizeof(state.current_path));
    
    /* Create main window */
    state.form = sys_win_create_form("File Manager", 80, 60, 450, 280);
    if (!state.form) {
        sys_exit();
    }
    
    sys_win_set_icon(state.form, "C:/ICONS/CABINET.ICO");
    
    /* Enable menubar */
    sys_win_menubar_enable(state.form);
    
    /* Add File menu */
    int file_menu = sys_win_menubar_add_menu(state.form, "File");
    sys_win_menubar_add_item(state.form, file_menu, "Open", MENU_FILE_OPEN);
    sys_win_menubar_add_item(state.form, file_menu, "Delete", MENU_FILE_DELETE);
    sys_win_menubar_add_item(state.form, file_menu, "Exit", MENU_FILE_EXIT);
    
    /* Add Edit menu */
    int edit_menu = sys_win_menubar_add_menu(state.form, "Edit");
    sys_win_menubar_add_item(state.form, edit_menu, "Copy", MENU_EDIT_COPY);
    sys_win_menubar_add_item(state.form, edit_menu, "Paste", MENU_EDIT_PASTE);
    
    /* Add View menu */
    int view_menu = sys_win_menubar_add_menu(state.form, "View");
    sys_win_menubar_add_item(state.form, view_menu, "Refresh", MENU_VIEW_REFRESH);
    
    /* Add Help menu */
    int help_menu = sys_win_menubar_add_menu(state.form, "Help");
    sys_win_menubar_add_item(state.form, help_menu, "About", MENU_HELP_ABOUT);
    
    /* Add path textbox */
    gui_control_t path_textbox = {
        CTRL_TEXTBOX, 8, 8, 408, 20, 0, 15, "", ID_PATH_TEXTBOX, 0, 12, 1, 8, NULL, NULL, 0, 0, 0, 0, 0, 255, 0, 0, -1, -1, 0, 0, 0, 0
    };
    char upper_path[256];
    strcpy(upper_path, state.current_path);
    str_toupper(upper_path);
    strncpy(path_textbox.text, upper_path, sizeof(path_textbox.text) - 1);
    sys_win_add_control(state.form, &path_textbox);

    /* Add back button adjacent to path field */
    gui_control_t back_button = {
        CTRL_BUTTON, 420, 8, 22, 20, 0, -1, "..", ID_BACK_BUTTON, 0, 12, 0, 0, NULL, NULL, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0, 0, 0
    };
    sys_win_add_control(state.form, &back_button);
    
    /* Add frame for tree view */
    gui_control_t tree_frame = {
        CTRL_FRAME, TREE_X - 3, TREE_Y - 12, TREE_W + 8, 204, 8, 7, "", 0, 0, 12, 1, 8, NULL, NULL, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0, 0, 0
    };
    sys_win_add_control(state.form, &tree_frame);
    
    /* Add tree scrollbar (initially hidden) */
    gui_control_t tree_scrollbar = {
        CTRL_SCROLLBAR, TREE_X + TREE_W + 3 - TREE_SCROLLBAR_W, TREE_Y - 1, TREE_SCROLLBAR_W, 192, 8, 7, "", ID_TREE_SCROLLBAR, 0, 12, 0, 0, NULL, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0, 0, 0
    };
    sys_win_add_control(state.form, &tree_scrollbar);
    ctrl_set_visible(state.form, ID_TREE_SCROLLBAR, 0);
    
    /* Add file scrollbar (initially hidden) */
    gui_control_t file_scrollbar = {
        CTRL_SCROLLBAR, FILES_X + FILES_W - 2, FILES_Y - 1, FILE_SCROLLBAR_W, FILE_VISIBLE_ROWS * (FILE_ICON_H + FILE_PAD), 8, 7, "", ID_FILE_SCROLLBAR, 0, 12, 0, 0, NULL, NULL, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0, 0, 0
    };
    sys_win_add_control(state.form, &file_scrollbar);
    ctrl_set_visible(state.form, ID_FILE_SCROLLBAR, 0);
    
    /* Pre-create tree item controls (labels in left pane) */
    for (int i = 0; i < TREE_VISIBLE_ITEMS; i++) {
        int y = TREE_Y + i * TREE_ITEM_H;
        
        /* Use label for clickable tree item */
        gui_control_t tree_label = {
            CTRL_LABEL, TREE_X, y, TREE_W, TREE_ITEM_H - 2, 0, -1, "", ID_TREE_BASE + i, 0, 12, 0, 0, NULL, NULL, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0, 0, 0
        };
        sys_win_add_control(state.form, &tree_label);
        ctrl_set_visible(state.form, ID_TREE_BASE + i, 0);
    }
    
    /* Pre-create file item controls (larger icons in right pane) */
    int visible_file_count = FILE_VISIBLE_ROWS * FILE_COLS;
    for (int i = 0; i < visible_file_count; i++) {
        int col = i % FILE_COLS;
        int row = i / FILE_COLS;
        int x = FILES_X + col * (FILE_ICON_W + FILE_PAD);
        int y = FILES_Y + row * (FILE_ICON_H + FILE_PAD);
        
        gui_control_t file_icon = {
            CTRL_ICON, x, y, FILE_ICON_W, FILE_ICON_H, 0, 15, "", ID_FILE_BASE + i, 0, 9, 0, 0, NULL, NULL, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0, 0, 0
        };
        sys_win_add_control(state.form, &file_icon);
        ctrl_set_visible(state.form, ID_FILE_BASE + i, 0);
    }
    
    /* Initial display */
    refresh_display();
    
    /* Run event loop */
    sys_win_run_event_loop(state.form, handle_event, NULL);
    
    sys_win_destroy_form(state.form);
    sys_exit();
}
