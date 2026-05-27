#include "../../syscall.h"
#include "../../lib/stdio.h"
#include "../../lib/string.h"
#include "../../lib/elf.h"
#include "../../lib/app.h"
#include "../../drivers/keyboard.h"

OSLET_APP("File Manager", OSLET_KIND_GIX, "C:/ICONS/CABINET.ICO", OSLET_APP_FLAG_NONE);

#define MENU_FILE_NEW_FOLDER 101
#define MENU_FILE_NEW_FILE   102
#define MENU_FILE_EXIT       104
#define MENU_EDIT_CUT        201
#define MENU_EDIT_COPY       202
#define MENU_EDIT_PASTE      203
#define MENU_EDIT_RENAME     204
#define MENU_EDIT_DELETE     205
#define MENU_GO_BACK         301
#define MENU_GO_HOME         302
#define MENU_VIEW_REFRESH    401
#define MENU_HELP_ABOUT      501

#define ID_PATH_TEXTBOX      50
#define ID_BACK_BUTTON       51
#define ID_TOOLBAR_FRAME     52
#define ID_TREEVIEW          53
#define ID_FILE_SCROLLBAR    61

#define ID_TB_BACK           69
#define ID_TB_CUT            70
#define ID_TB_COPY           71
#define ID_TB_PASTE          72
#define ID_TB_RENAME         73
#define ID_TB_DELETE         74
#define ID_TB_NEW_FOLDER     75
#define ID_TB_NEW_FILE       76
#define ID_FILE_BASE         2000

#define MAX_TREE_ITEMS       256
#define MAX_FILE_ITEMS       64
#define MAX_DIR_SCAN_ITEMS   128

#define TOOLBAR_Y            30
#define TOOLBAR_H            35
#define CONTENT_Y            66
#define TREE_X               11
#define TREE_W               120
#define TREE_ITEM_H          18
#define FILE_ICON_W          48
#define FILE_ICON_H          58
#define FILE_PAD             8
#define FILE_SCROLLBAR_W     18

#define MAX_FILE_COLS        9
#define MAX_FILE_ROWS        6

typedef struct {
    char name[64];
    char full_path[256];
    uint8_t level;
    uint8_t flags;
} tree_node_t;

typedef struct {
    char name[64];
    char full_path[256];
    int is_directory;
    uint32_t size;
} file_item_t;

typedef struct {
    void *form;
    char current_path[256];
    tree_node_t tree_items[MAX_TREE_ITEMS];
    sys_tree_item_t tree_view_items[MAX_TREE_ITEMS];
    sys_dirent_t tree_scan_entries[MAX_DIR_SCAN_ITEMS];
    sys_dirent_t tree_probe_entries[MAX_DIR_SCAN_ITEMS];
    sys_dirent_t file_scan_entries[MAX_DIR_SCAN_ITEMS];
    int tree_count;
    int selected_tree_index;
    file_item_t file_items[MAX_FILE_ITEMS];
    int file_count;
    int file_scroll_offset;
    int selected_file_index;
    char clipboard_path[256];
    int clipboard_is_cut;
    int file_cols;
    int file_rows;
    int files_x;
    int files_w;
} fileman_state_t;

static fileman_state_t state;

static char* show_prompt_dialog(const char *title, const char *label, const char *default_value) {
    static char result[256];
    result[0] = '\0';
    
    void *dlg = sys_win_create_form(title, 193, 199, 300, 82);
    if (!dlg) return NULL;
    
    gui_control_t label_ctrl = {0};
    label_ctrl.type = CTRL_LABEL;
    label_ctrl.x = 9; label_ctrl.y = 6;
    label_ctrl.id = 1; label_ctrl.font_size = 12; label_ctrl.bg = -1;
    strncpy(label_ctrl.text, label, sizeof(label_ctrl.text) - 1);
    sys_win_add_control(dlg, &label_ctrl);

    gui_control_t textbox = {0};
    textbox.type = CTRL_TEXTBOX;
    textbox.x = 80; textbox.y = 5; textbox.w = 210; textbox.h = 20;
    textbox.id = 2; textbox.font_size = 12; textbox.textbox.max_length = 255;
    if (default_value && default_value[0])
        strncpy(textbox.text, default_value, sizeof(textbox.text) - 1);
    sys_win_add_control(dlg, &textbox);

    gui_control_t ok_btn = {0};
    ok_btn.type = CTRL_BUTTON;
    ok_btn.x = 76; ok_btn.y = 33; ok_btn.w = 70; ok_btn.h = 23;
    ok_btn.id = 3; ok_btn.font_size = 12; ok_btn.bg = -1;
    strncpy(ok_btn.text, "OK", sizeof(ok_btn.text) - 1);
    sys_win_add_control(dlg, &ok_btn);

    gui_control_t cancel_btn = {0};
    cancel_btn.type = CTRL_BUTTON;
    cancel_btn.x = 150; cancel_btn.y = 33; cancel_btn.w = 70; cancel_btn.h = 23;
    cancel_btn.id = 4; cancel_btn.font_size = 12; cancel_btn.bg = -1;
    strncpy(cancel_btn.text, "Cancel", sizeof(cancel_btn.text) - 1);
    sys_win_add_control(dlg, &cancel_btn);
    
    sys_win_draw(dlg);
    sys_win_redraw_all();
    
    int dlg_running = 1;
    int confirmed = 0;
    
    while (dlg_running) {
        int ev = sys_win_pump_events(dlg);
        if (ev == -3 || ev == 4) {
            dlg_running = 0;
        } else if (ev == 3) {
            gui_control_t *tb = sys_win_get_control(dlg, 2);
            if (tb && tb->text[0]) {
                strncpy(result, tb->text, sizeof(result) - 1);
                result[sizeof(result) - 1] = '\0';
                confirmed = 1;
            }
            dlg_running = 0;
        } else if (ev == -1 || ev == -2) {
            sys_win_draw(dlg);
            sys_win_redraw_all();
        }
        sys_yield();
    }
    
    sys_win_destroy_form(dlg);
    sys_win_redraw_all();
    
    return confirmed ? result : NULL;
}

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

static int file_or_dir_exists(const char *path) {
    if (path_exists(path))
        return 1;

    int fd = sys_open(path, "r");
    if (fd >= 0) {
        sys_close(fd);
        return 1;
    }
    return 0;
}

static int tree_name_matches(const char *name, const char *token, size_t token_len) {
    size_t name_len = strlen(name);
    if (name_len != token_len) return 0;
    for (size_t i = 0; i < token_len; i++) {
        char a = name[i];
        char b = token[i];
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) return 0;
    }
    return 1;
}

static int tree_find_path(const char *path) {
    for (int i = 0; i < state.tree_count; i++) {
        if (strcasecmp(state.tree_items[i].full_path, path) == 0)
            return i;
    }
    return -1;
}

static int tree_find_child_by_name(int parent_idx, const char *name, size_t name_len) {
    if (parent_idx < 0 || parent_idx >= state.tree_count) return -1;
    int child_level = state.tree_items[parent_idx].level + 1;

    for (int i = parent_idx + 1; i < state.tree_count; i++) {
        if (state.tree_items[i].level <= state.tree_items[parent_idx].level)
            break;
        if (state.tree_items[i].level == child_level &&
            tree_name_matches(state.tree_items[i].name, name, name_len))
            return i;
    }
    return -1;
}

static int tree_item_visible(int idx) {
    if (idx < 0 || idx >= state.tree_count) return 0;

    int level = state.tree_items[idx].level;
    for (int i = idx - 1; i >= 0 && level > 0; i--) {
        if (state.tree_items[i].level < level) {
            if (!(state.tree_items[i].flags & TREE_ITEM_EXPANDED))
                return 0;
            level = state.tree_items[i].level;
        }
    }
    return 1;
}

static int tree_nearest_visible_ancestor(int idx) {
    if (idx < 0 || idx >= state.tree_count) return 0;
    if (tree_item_visible(idx)) return idx;

    int level = state.tree_items[idx].level;
    for (int i = idx - 1; i >= 0 && level > 0; i--) {
        if (state.tree_items[i].level < level) {
            if (tree_item_visible(i))
                return i;
            level = state.tree_items[i].level;
        }
    }
    return 0;
}

static int tree_visible_count(void) {
    int visible = 0;
    for (int i = 0; i < state.tree_count; i++) {
        if (tree_item_visible(i))
            visible++;
    }
    return visible;
}

static int tree_model_valid(void) {
    if (state.tree_count <= 0 || state.tree_count > MAX_TREE_ITEMS)
        return 0;
    if (state.tree_items[0].level != 0 || strcmp(state.tree_items[0].full_path, "C:/") != 0)
        return 0;

    for (int i = 1; i < state.tree_count; i++) {
        if (state.tree_items[i].level == 0)
            return 0;
        if (state.tree_items[i].level > state.tree_items[i - 1].level + 1)
            return 0;
    }

    return tree_visible_count() > 0;
}

static void tree_remove_descendants(int idx) {
    if (idx < 0 || idx >= state.tree_count) return;

    int start = idx + 1;
    int end = start;
    while (end < state.tree_count && state.tree_items[end].level > state.tree_items[idx].level)
        end++;

    int remove_count = end - start;
    if (remove_count <= 0) return;

    for (int i = start; i + remove_count < state.tree_count; i++)
        state.tree_items[i] = state.tree_items[i + remove_count];
    state.tree_count -= remove_count;
}

static void tree_sort_dirents(sys_dirent_t *entries, int count) {
    for (int i = 1; i < count; i++) {
        sys_dirent_t tmp = entries[i];
        int j = i - 1;
        while (j >= 0 && strcasecmp(entries[j].name, tmp.name) > 0) {
            entries[j + 1] = entries[j];
            j--;
        }
        entries[j + 1] = tmp;
    }
}

static int directory_has_subdirs(const char *path) {
    int count = sys_readdir(path, state.tree_probe_entries, MAX_DIR_SCAN_ITEMS);
    if (count < 0) return 0;

    for (int i = 0; i < count; i++) {
        if (!state.tree_probe_entries[i].is_directory) continue;
        if (strcmp(state.tree_probe_entries[i].name, ".") == 0 || strcmp(state.tree_probe_entries[i].name, "..") == 0) continue;
        return 1;
    }
    return 0;
}

static void tree_load_children(int idx) {
    if (idx < 0 || idx >= state.tree_count) return;
    if (state.tree_items[idx].flags & TREE_ITEM_LOADED) return;

    sys_dirent_t *entries = state.tree_scan_entries;
    int count = sys_readdir(state.tree_items[idx].full_path, entries, MAX_DIR_SCAN_ITEMS);
    if (count < 0) {
        state.tree_items[idx].flags |= TREE_ITEM_LOADED;
        state.tree_items[idx].flags &= ~TREE_ITEM_HAS_CHILDREN;
        return;
    }

    int dir_count = 0;
    for (int i = 0; i < count && dir_count < MAX_DIR_SCAN_ITEMS; i++) {
        if (!entries[i].is_directory) continue;
        if (strcmp(entries[i].name, ".") == 0 || strcmp(entries[i].name, "..") == 0) continue;
        if (dir_count != i)
            entries[dir_count] = entries[i];
        dir_count++;
    }
    tree_sort_dirents(entries, dir_count);

    if (dir_count == 0) {
        state.tree_items[idx].flags |= TREE_ITEM_LOADED;
        state.tree_items[idx].flags &= ~(TREE_ITEM_HAS_CHILDREN | TREE_ITEM_EXPANDED);
        return;
    }

    int insert_at = idx + 1;
    while (insert_at < state.tree_count && state.tree_items[insert_at].level > state.tree_items[idx].level)
        insert_at++;

    for (int i = 0; i < dir_count && state.tree_count < MAX_TREE_ITEMS; i++) {
        for (int j = state.tree_count; j > insert_at; j--)
            state.tree_items[j] = state.tree_items[j - 1];

        tree_node_t *child = &state.tree_items[insert_at];
        strncpy(child->name, entries[i].name, sizeof(child->name) - 1);
        child->name[sizeof(child->name) - 1] = '\0';
        build_path(child->full_path, sizeof(child->full_path), state.tree_items[idx].full_path, entries[i].name);
        normalize_path(child->full_path, sizeof(child->full_path));
        child->level = state.tree_items[idx].level + 1;
        child->flags = TREE_ITEM_FOLDER;
        if (directory_has_subdirs(child->full_path))
            child->flags |= TREE_ITEM_HAS_CHILDREN;
        else
            child->flags |= TREE_ITEM_LOADED;
        state.tree_count++;
        insert_at++;
    }

    state.tree_items[idx].flags |= TREE_ITEM_LOADED | TREE_ITEM_HAS_CHILDREN;
}

static void tree_reload_children_for_path(const char *path) {
    int idx = tree_find_path(path);
    if (idx < 0) return;

    uint8_t was_expanded = state.tree_items[idx].flags & TREE_ITEM_EXPANDED;
    tree_remove_descendants(idx);
    state.tree_items[idx].flags &= ~(TREE_ITEM_LOADED | TREE_ITEM_HAS_CHILDREN);
    tree_load_children(idx);
    if (was_expanded && (state.tree_items[idx].flags & TREE_ITEM_HAS_CHILDREN))
        state.tree_items[idx].flags |= TREE_ITEM_EXPANDED;
}

static void tree_init_model(void) {
    state.tree_count = 1;
    state.selected_tree_index = 0;
    strcpy(state.tree_items[0].name, "C:/");
    strcpy(state.tree_items[0].full_path, "C:/");
    state.tree_items[0].level = 0;
    state.tree_items[0].flags = TREE_ITEM_FOLDER | TREE_ITEM_HAS_CHILDREN | TREE_ITEM_EXPANDED;
    tree_load_children(0);
}

static void tree_build_path_fallback(const char *path) {
    char normalized[256];
    char current_path[256];

    state.tree_count = 1;
    state.selected_tree_index = 0;
    strcpy(state.tree_items[0].name, "C:/");
    strcpy(state.tree_items[0].full_path, "C:/");
    state.tree_items[0].level = 0;
    state.tree_items[0].flags = TREE_ITEM_FOLDER | TREE_ITEM_HAS_CHILDREN | TREE_ITEM_EXPANDED | TREE_ITEM_LOADED;

    if (!path || !path[0])
        return;

    strncpy(normalized, path, sizeof(normalized) - 1);
    normalized[sizeof(normalized) - 1] = '\0';
    normalize_path(normalized, sizeof(normalized));
    if (strcmp(normalized, "C:/") == 0)
        return;

    strcpy(current_path, "C:/");

    char temp[256];
    strcpy(temp, normalized);
    int len = strlen(temp);
    if (len > 3 && temp[len - 1] == '/')
        temp[len - 1] = '\0';

    char *token = temp + 3;
    uint8_t level = 1;
    while (*token && state.tree_count < MAX_TREE_ITEMS) {
        char *slash = strchr(token, '/');
        size_t token_len = slash ? (size_t)(slash - token) : strlen(token);

        if (token_len > 0) {
            tree_node_t *node = &state.tree_items[state.tree_count];
            int copy_len = token_len < sizeof(node->name) - 1 ? (int)token_len : (int)sizeof(node->name) - 1;
            strncpy(node->name, token, copy_len);
            node->name[copy_len] = '\0';
            char next_path[256];
            build_path(next_path, sizeof(next_path), current_path, node->name);
            normalize_path(next_path, sizeof(next_path));
            strncpy(current_path, next_path, sizeof(current_path) - 1);
            current_path[sizeof(current_path) - 1] = '\0';
            strncpy(node->full_path, current_path, sizeof(node->full_path) - 1);
            node->full_path[sizeof(node->full_path) - 1] = '\0';
            node->level = level++;
            node->flags = TREE_ITEM_FOLDER | TREE_ITEM_LOADED;
            if (slash)
                node->flags |= TREE_ITEM_HAS_CHILDREN | TREE_ITEM_EXPANDED;
            else if (directory_has_subdirs(node->full_path))
                node->flags |= TREE_ITEM_HAS_CHILDREN;

            state.selected_tree_index = state.tree_count;
            state.tree_count++;
        }

        if (!slash)
            break;
        token = slash + 1;
    }
}

static int tree_ensure_path(const char *path) {
    if (state.tree_count <= 0)
        tree_init_model();

    char normalized[256];
    strncpy(normalized, path, sizeof(normalized) - 1);
    normalized[sizeof(normalized) - 1] = '\0';
    normalize_path(normalized, sizeof(normalized));

    int current = 0;
    int found_path = 1;
    state.tree_items[current].flags |= TREE_ITEM_EXPANDED;
    tree_load_children(current);

    if (strcmp(normalized, "C:/") != 0) {
        char temp[256];
        strcpy(temp, normalized);
        int len = strlen(temp);
        if (len > 3 && temp[len - 1] == '/')
            temp[len - 1] = '\0';

        char *token = temp + 3;
        while (*token) {
            char *slash = strchr(token, '/');
            size_t token_len = slash ? (size_t)(slash - token) : strlen(token);
            if (token_len > 0) {
                int child = tree_find_child_by_name(current, token, token_len);
                if (child < 0) {
                    tree_reload_children_for_path(state.tree_items[current].full_path);
                    child = tree_find_child_by_name(current, token, token_len);
                }
                if (child < 0) {
                    found_path = 0;
                    break;
                }
                current = child;
                if (slash) {
                    state.tree_items[current].flags |= TREE_ITEM_EXPANDED;
                    tree_load_children(current);
                }
            }
            if (!slash) break;
            token = slash + 1;
        }
    }

    state.selected_tree_index = current;
    return found_path;
}

static void tree_rebuild_for_path(const char *path) {
    state.tree_count = 0;
    state.selected_tree_index = 0;
    tree_init_model();
    if (!tree_ensure_path(path))
        tree_build_path_fallback(path);

    if (state.form) {
        ctrl_tree_set_icons(state.form, ID_TREEVIEW, "C:/ICONS/FLD.ICO", "C:/ICONS/FLO.ICO");
    }
}

static void sync_tree_control(void) {
    if (!state.form) return;

    if (!tree_model_valid())
        tree_rebuild_for_path(state.current_path);
    if (!tree_model_valid())
        tree_build_path_fallback(state.current_path);
    if (!tree_model_valid())
        tree_build_path_fallback("C:/");

    if (state.selected_tree_index < 0 || state.selected_tree_index >= state.tree_count)
        state.selected_tree_index = 0;
    if (!tree_item_visible(state.selected_tree_index))
        state.selected_tree_index = tree_nearest_visible_ancestor(state.selected_tree_index);

    for (int i = 0; i < state.tree_count; i++) {
        strncpy(state.tree_view_items[i].text, state.tree_items[i].name, sizeof(state.tree_view_items[i].text) - 1);
        state.tree_view_items[i].text[sizeof(state.tree_view_items[i].text) - 1] = '\0';
        state.tree_view_items[i].level = state.tree_items[i].level;
        state.tree_view_items[i].flags = state.tree_items[i].flags;
        state.tree_view_items[i].app_id = (uint16_t)i;
    }

    ctrl_tree_set_items(state.form, ID_TREEVIEW, state.tree_view_items, state.tree_count);
    ctrl_tree_set_selected(state.form, ID_TREEVIEW, state.selected_tree_index);
}

static int build_unique_paste_path(char *dest, size_t dest_size, const char *folder, const char *filename) {
    build_path(dest, dest_size, folder, filename);
    if (!file_or_dir_exists(dest))
        return 1;

    char base[128];
    char ext[64];
    size_t base_len = 0;
    const char *dot = strrchr(filename, '.');

    if (dot && dot != filename) {
        base_len = dot - filename;
        if (base_len >= sizeof(base))
            base_len = sizeof(base) - 1;
        for (size_t i = 0; i < base_len; i++)
            base[i] = filename[i];
        base[base_len] = '\0';
        strncpy(ext, dot, sizeof(ext) - 1);
        ext[sizeof(ext) - 1] = '\0';
    } else {
        strncpy(base, filename, sizeof(base) - 1);
        base[sizeof(base) - 1] = '\0';
        ext[0] = '\0';
    }

    for (int n = 1; n < 1000; n++) {
        char candidate[192];
        snprintf(candidate, sizeof(candidate), "%s (%d)%s", base, n, ext);
        build_path(dest, dest_size, folder, candidate);
        if (!file_or_dir_exists(dest))
            return 1;
    }
    dest[0] = '\0';
    return 0;
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
    state.file_scroll_offset = 0;
    state.selected_file_index = -1;
    if (!tree_ensure_path(state.current_path))
        tree_rebuild_for_path(state.current_path);
    return 1;
}

static void scan_directory(void) {
    sys_dirent_t *entries = state.file_scan_entries;
    int count = sys_readdir(state.current_path, entries, MAX_DIR_SCAN_ITEMS);
    
    state.file_count = 0;
    
    if (count < 0) return;
    
    /* In subfolders, add ".." as the first file item */
    if (strcmp(state.current_path, "C:/") != 0) {
        file_item_t *back = &state.file_items[state.file_count++];
        strcpy(back->name, "..");
        /* Compute parent path */
        char parent[256];
        strcpy(parent, state.current_path);
        int plen = strlen(parent);
        if (plen > 0 && parent[plen - 1] == '/') parent[--plen] = '\0';
        char *sl = strrchr(parent, '/');
        if (sl && sl > parent + 2) { *sl = '\0'; normalize_path(parent, sizeof(parent)); }
        else strcpy(parent, "C:/");
        strcpy(back->full_path, parent);
        back->is_directory = 1;
        back->size = 0;
    }

    /* Collect directories and files separately, then sort */
    int dir_start = state.file_count;
    for (int i = 0; i < count; i++) {
        if (strcmp(entries[i].name, ".") == 0 || strcmp(entries[i].name, "..") == 0) continue;
        if (!entries[i].is_directory) continue;
        if (state.file_count >= MAX_FILE_ITEMS) break;
        file_item_t *item = &state.file_items[state.file_count++];
        strncpy(item->name, entries[i].name, sizeof(item->name) - 1);
        item->name[sizeof(item->name) - 1] = '\0';
        build_path(item->full_path, sizeof(item->full_path), state.current_path, entries[i].name);
        item->is_directory = 1;
        item->size = entries[i].size;
    }
    int dir_end = state.file_count;

    int file_start = state.file_count;
    for (int i = 0; i < count; i++) {
        if (strcmp(entries[i].name, ".") == 0 || strcmp(entries[i].name, "..") == 0) continue;
        if (entries[i].is_directory) continue;
        if (state.file_count >= MAX_FILE_ITEMS) break;
        file_item_t *item = &state.file_items[state.file_count++];
        strncpy(item->name, entries[i].name, sizeof(item->name) - 1);
        item->name[sizeof(item->name) - 1] = '\0';
        build_path(item->full_path, sizeof(item->full_path), state.current_path, entries[i].name);
        item->is_directory = 0;
        item->size = entries[i].size;
    }
    int file_end = state.file_count;

    /* Insertion sort directories alphabetically (case-insensitive) */
    for (int i = dir_start + 1; i < dir_end; i++) {
        file_item_t tmp = state.file_items[i];
        int j = i - 1;
        while (j >= dir_start && strcasecmp(state.file_items[j].name, tmp.name) > 0) {
            state.file_items[j + 1] = state.file_items[j];
            j--;
        }
        state.file_items[j + 1] = tmp;
    }

    /* Insertion sort files alphabetically (case-insensitive) */
    for (int i = file_start + 1; i < file_end; i++) {
        file_item_t tmp = state.file_items[i];
        int j = i - 1;
        while (j >= file_start && strcasecmp(state.file_items[j].name, tmp.name) > 0) {
            state.file_items[j + 1] = state.file_items[j];
            j--;
        }
        state.file_items[j + 1] = tmp;
    }
}

static void relayout(void) {
    gui_form_t *form = (gui_form_t *)state.form;
    int win_w = form->win.w;
    int win_h = form->win.h;

    int client_h = win_h - 40;
    int avail_h = client_h - CONTENT_Y;
    if (avail_h < 40) avail_h = 40;

    /* Path bar */
    int path_w = win_w - 16;
    if (path_w < 100) path_w = 100;
    sys_ctrl_set_prop(state.form, ID_PATH_TEXTBOX, PROP_W, path_w);

    /* Toolbar */
    sys_ctrl_set_prop(state.form, ID_TOOLBAR_FRAME, PROP_W, win_w - 16);

    /* Tree pane */
    sys_ctrl_set_prop(state.form, ID_TREEVIEW, PROP_H, avail_h - 9);

    /* File pane */
    state.files_x = TREE_X + TREE_W + 12;
    int sb_x = win_w - FILE_SCROLLBAR_W - 8;
    state.files_w = sb_x - state.files_x;
    if (state.files_w < FILE_ICON_W) state.files_w = FILE_ICON_W;

    state.file_cols = state.files_w / (FILE_ICON_W + FILE_PAD);
    if (state.file_cols < 1) state.file_cols = 1;
    if (state.file_cols > MAX_FILE_COLS) state.file_cols = MAX_FILE_COLS;

    state.file_rows = avail_h / (FILE_ICON_H + FILE_PAD);
    if (state.file_rows < 1) state.file_rows = 1;
    if (state.file_rows > MAX_FILE_ROWS) state.file_rows = MAX_FILE_ROWS;

    sys_ctrl_set_prop(state.form, ID_FILE_SCROLLBAR, PROP_X, sb_x);
    sys_ctrl_set_prop(state.form, ID_FILE_SCROLLBAR, PROP_H, avail_h-9);
}

static void clear_file_controls(void) {
    int max_count = MAX_FILE_ROWS * MAX_FILE_COLS;
    for (int i = 0; i < max_count; i++) {
        gui_control_t *ctrl = sys_win_get_control(state.form, ID_FILE_BASE + i);
        if (ctrl) ctrl_set_visible(state.form, ID_FILE_BASE + i, 0);
    }
}

static void refresh_display(void) {
    scan_directory();
    clear_file_controls();
    
    /* Update path textbox */
    char upper_path[256];
    strcpy(upper_path, state.current_path);
    str_toupper(upper_path);
    ctrl_set_text(state.form, ID_PATH_TEXTBOX, upper_path);
    sync_tree_control();
    
    /* Update file scrollbar */
    int file_rows_total = (state.file_count + state.file_cols - 1) / state.file_cols;
    if (file_rows_total > state.file_rows) {
        ctrl_set_visible(state.form, ID_FILE_SCROLLBAR, 1);
        gui_control_t *scrollbar = sys_win_get_control(state.form, ID_FILE_SCROLLBAR);
        if (scrollbar) {
            scrollbar->scrollbar.max_length = file_rows_total - state.file_rows;
            if (state.file_scroll_offset > scrollbar->scrollbar.max_length)
                state.file_scroll_offset = scrollbar->scrollbar.max_length;
            scrollbar->scrollbar.cursor_pos = state.file_scroll_offset;
        }
    } else {
        ctrl_set_visible(state.form, ID_FILE_SCROLLBAR, 0);
        state.file_scroll_offset = 0;
    }
    
    /* Update file items with repositioning for dynamic layout */
    int visible_file_count = state.file_rows * state.file_cols;
    for (int i = 0; i < visible_file_count; i++) {
        int idx = (state.file_scroll_offset * state.file_cols) + i;
        int ctrl_id = ID_FILE_BASE + i;
        gui_control_t *ctrl = sys_win_get_control(state.form, ctrl_id);
        
        if (idx < state.file_count && ctrl) {
            int col = i % state.file_cols;
            int row = i / state.file_cols;
            sys_ctrl_set_prop(state.form, ctrl_id, PROP_X, state.files_x + col * (FILE_ICON_W + FILE_PAD));
            sys_ctrl_set_prop(state.form, ctrl_id, PROP_Y, CONTENT_Y + row * (FILE_ICON_H + FILE_PAD));

            ctrl_set_text(state.form, ctrl_id, state.file_items[idx].name);
            ctrl_set_visible(state.form, ctrl_id, 1);
            
            if (state.file_items[idx].is_directory && strcmp(state.file_items[idx].name, "..") == 0) {
                ctrl_set_image(state.form, ctrl_id, "C:/ICONS/BACK.ICO");
            } else if (state.file_items[idx].is_directory) {
                ctrl_set_image(state.form, ctrl_id, "C:/ICONS/DIRECTRY.ICO");
            } else {
                const char *dot = strrchr(state.file_items[idx].name, '.');
                if (dot) {
                    if (strcasecmp(dot, ".elf") == 0) {
                        oslet_app_info_t info;
                        oslet_app_info_init(&info);
                        if (oslet_app_read_info(state.file_items[idx].full_path, &info) == 0 && info.icon_path[0]) {
                            ctrl_set_image(state.form, ctrl_id, info.icon_path);
                        } else {
                            ctrl_set_image(state.form, ctrl_id, "C:/ICONS/EXE.ICO");
                        }
                    } else if (strcasecmp(dot, ".txt") == 0 || (strcasecmp(dot, ".ini") == 0)) {
                        ctrl_set_image(state.form, ctrl_id, "C:/ICONS/TEXT.ICO");
                    } else if (strcasecmp(dot, ".bmp") == 0 || (strcasecmp(dot, ".ico") == 0)) {
                        ctrl_set_image(state.form, ctrl_id, "C:/ICONS/BITMAP.ICO");
                    } else if (strcasecmp(dot, ".wav") == 0) {
                        ctrl_set_image(state.form, ctrl_id, "C:/ICONS/SOUND.ICO");
                    } else if (strcasecmp(dot, ".bmf") == 0) {
                        ctrl_set_image(state.form, ctrl_id, "C:/ICONS/FONT.ICO");
                    } else if (strcasecmp(dot, ".grp") == 0) {
                        ctrl_set_image(state.form, ctrl_id, "C:/ICONS/GROUP.ICO");
                    } else {
                        ctrl_set_image(state.form, ctrl_id, "C:/ICONS/FILE.ICO");
                    }
                } else {
                    ctrl_set_image(state.form, ctrl_id, "C:/ICONS/FILE.ICO");
                }
            }
        } else if (ctrl) {
            ctrl_set_visible(state.form, ctrl_id, 0);
        }
    }
    
    sys_win_draw(state.form);
}

static int find_checked_file_index(void) {
    int visible_file_count = state.file_rows * state.file_cols;
    for (int i = 0; i < visible_file_count; i++) {
        gui_control_t *ctrl = sys_win_get_control(state.form, ID_FILE_BASE + i);
        if (ctrl && ctrl->icon.checked) {
            int actual_idx = (state.file_scroll_offset * state.file_cols) + i;
            if (actual_idx >= 0 && actual_idx < state.file_count)
                return actual_idx;
        }
    }
    return -1;
}

static int copy_file_data(const char *src, const char *dst) {
    int src_fd = sys_open(src, "r");
    if (src_fd < 0) return -1;

    int dst_fd = sys_open(dst, "w");
    if (dst_fd < 0) {
        sys_close(src_fd);
        return -1;
    }

    char buffer[512];
    int bytes_read;
    while ((bytes_read = sys_read(src_fd, buffer, sizeof(buffer))) > 0)
        sys_write_file(dst_fd, buffer, bytes_read);

    sys_close(src_fd);
    sys_close(dst_fd);
    return 0;
}

static void open_selected_file(int idx) {
    if (idx < 0 || idx >= state.file_count) return;

    if (state.file_items[idx].is_directory) {
        if (set_current_path(state.file_items[idx].full_path, 0))
            refresh_display();
        return;
    }

    const char *dot = strrchr(state.file_items[idx].name, '.');
    if (!dot) return;

    if (strcasecmp(dot, ".elf") == 0) {
        oslet_launch_program(state.file_items[idx].full_path,
                             "",
                             OSLET_LAUNCH_FROM_GIX,
                             NULL,
                             NULL);
    } else if (strcasecmp(dot, ".txt") == 0 || strcasecmp(dot, ".ini") == 0) {
        oslet_launch_program("C:/OSLET/START/NOTEPAD.ELF",
                             state.file_items[idx].full_path,
                             OSLET_LAUNCH_FROM_GIX,
                             NULL,
                             NULL);
    } else if (strcasecmp(dot, ".bmp") == 0 || strcasecmp(dot, ".ico") == 0) {
        oslet_launch_program("C:/OSLET/START/IMGVIEW.ELF",
                             state.file_items[idx].full_path,
                             OSLET_LAUNCH_FROM_GIX,
                             NULL,
                             NULL);
    }
}

static void do_cut_action(void) {
    int sel = find_checked_file_index();
    if (sel < 0) {
        sys_win_msgbox("Please select a file first", "OK", "Cut");
        sys_win_redraw_all();
        return;
    }
    state.selected_file_index = sel;
    strcpy(state.clipboard_path, state.file_items[sel].full_path);
    state.clipboard_is_cut = 1;
}

static void do_copy_action(void) {
    int sel = find_checked_file_index();
    if (sel < 0) {
        sys_win_msgbox("Please select a file first", "OK", "Copy");
        sys_win_redraw_all();
        return;
    }
    state.selected_file_index = sel;
    strcpy(state.clipboard_path, state.file_items[sel].full_path);
    state.clipboard_is_cut = 0;
}

static void do_paste_action(void) {
    if (!state.clipboard_path[0]) {
        sys_win_msgbox("Nothing to paste", "OK", "Paste");
        sys_win_redraw_all();
        return;
    }

    char *filename = strrchr(state.clipboard_path, '/');
    if (filename) filename++;
    else filename = state.clipboard_path;

    char dest_path[256];
    if (!build_unique_paste_path(dest_path, sizeof(dest_path), state.current_path, filename)) {
        sys_win_msgbox("Could not find a free filename", "OK", "Paste");
        sys_win_redraw_all();
        return;
    }

    if (copy_file_data(state.clipboard_path, dest_path) != 0) {
        sys_win_msgbox("Failed to paste file", "OK", "Paste");
        sys_win_redraw_all();
        return;
    }

    if (state.clipboard_is_cut && strcasecmp(state.clipboard_path, dest_path) != 0) {
        sys_unlink(state.clipboard_path);
        state.clipboard_path[0] = '\0';
    }

    refresh_display();
}

static void do_rename_action(void) {
    int sel = find_checked_file_index();
    if (sel < 0) {
        sys_win_msgbox("Please select a file first", "OK", "Rename");
        sys_win_redraw_all();
        return;
    }
    state.selected_file_index = sel;

    char *new_name = show_prompt_dialog("Rename", "New name:", state.file_items[sel].name);
    if (new_name && new_name[0]) {
        char new_path[256];
        build_path(new_path, sizeof(new_path), state.current_path, new_name);

        if (sys_rename(state.file_items[sel].full_path, new_path) == 0) {
            if (state.file_items[sel].is_directory) {
                tree_reload_children_for_path(state.current_path);
                tree_ensure_path(state.current_path);
            }
            refresh_display();
        } else {
            sys_win_msgbox("Failed to rename", "OK", "Rename");
            sys_win_redraw_all();
        }
    }
}

static void do_delete_action(void) {
    int sel = find_checked_file_index();
    if (sel < 0) {
        sys_win_msgbox("Please select a file first", "OK", "Delete");
        sys_win_redraw_all();
        return;
    }
    state.selected_file_index = sel;

    char msg[320];
    snprintf(msg, sizeof(msg), "Delete '%s'?", state.file_items[sel].name);
    int resp = sys_win_msgbox(msg, "Yes|No", "Confirm Delete");
    sys_win_redraw_all();
    if (resp != 1) return;

    int ok;
    if (state.file_items[sel].is_directory)
        ok = sys_rmdir(state.file_items[sel].full_path);
    else
        ok = sys_unlink(state.file_items[sel].full_path);

    if (ok == 0) {
        if (state.file_items[sel].is_directory) {
            tree_reload_children_for_path(state.current_path);
            tree_ensure_path(state.current_path);
        }
        state.selected_file_index = -1;
        refresh_display();
    } else {
        sys_win_msgbox("Delete failed", "OK", "Delete");
        sys_win_redraw_all();
    }
}

static void handle_shortcut(int key) {
    switch (key) {
        case 3:  /* Ctrl+C */
            do_copy_action();
            break;
        case 22: /* Ctrl+V */
            do_paste_action();
            break;
        case 24: /* Ctrl+X */
            do_cut_action();
            break;
        case KEY_DELETE:
            do_delete_action();
            break;
    }
}

/* Event handling — returns 1 to exit */
static int handle_event(int event) {
    if (event >= SYS_EVENT_CTRL_KEY_BASE && event < SYS_EVENT_CTRL_KEY_BASE + 32) {
        handle_shortcut(event - SYS_EVENT_CTRL_KEY_BASE);
        return 0;
    }

    if (event == MENU_FILE_EXIT)
        return 1;
    
    if (event <= 0)
        return 0;
    
    /* Handle path textbox */
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

    /* Back button (toolbar or menu) */
    if (event == ID_BACK_BUTTON || event == ID_TB_BACK || event == MENU_GO_BACK) {
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

    /* Go Home */
    if (event == MENU_GO_HOME) {
        if (set_current_path("C:/", 0))
            refresh_display();
        return 0;
    }
    
    /* Handle tree view */
    if (event == ID_TREEVIEW) {
        int action = ctrl_tree_get_action(state.form, ID_TREEVIEW);
        int idx = ctrl_tree_get_action_index(state.form, ID_TREEVIEW);
        ctrl_tree_clear_action(state.form, ID_TREEVIEW);

        if (idx >= 0 && idx < state.tree_count) {
            if (action == TREE_ACTION_TOGGLE) {
                if (state.tree_items[idx].flags & TREE_ITEM_EXPANDED) {
                    state.tree_items[idx].flags &= ~TREE_ITEM_EXPANDED;
                    if (!tree_item_visible(state.selected_tree_index))
                        state.selected_tree_index = tree_nearest_visible_ancestor(state.selected_tree_index);
                } else {
                    tree_load_children(idx);
                    if (state.tree_items[idx].flags & TREE_ITEM_HAS_CHILDREN)
                        state.tree_items[idx].flags |= TREE_ITEM_EXPANDED;
                }
                sync_tree_control();
                sys_win_draw(state.form);
            } else if (action == TREE_ACTION_SELECT) {
                if (set_current_path(state.tree_items[idx].full_path, 0))
                    refresh_display();
            }
        }
        return 0;
    }
    
    /* Handle file scrollbar */
    if (event == ID_FILE_SCROLLBAR) {
        gui_control_t *scrollbar = sys_win_get_control(state.form, ID_FILE_SCROLLBAR);
        if (scrollbar) {
            state.file_scroll_offset = scrollbar->scrollbar.cursor_pos;
            refresh_display();
        }
        return 0;
    }
    
    /* Handle file icon double-clicks */
    if (event >= ID_FILE_BASE && event < ID_FILE_BASE + (MAX_FILE_ROWS * MAX_FILE_COLS)) {
        int visible_idx = event - ID_FILE_BASE;
        int actual_idx = (state.file_scroll_offset * state.file_cols) + visible_idx;
        if (actual_idx >= 0 && actual_idx < state.file_count)
            open_selected_file(actual_idx);
        return 0;
    }
    
    /* Toolbar and edit menu */
    if (event == ID_TB_CUT || event == MENU_EDIT_CUT)       { do_cut_action();    return 0; }
    if (event == ID_TB_COPY || event == MENU_EDIT_COPY)     { do_copy_action();   return 0; }
    if (event == ID_TB_PASTE || event == MENU_EDIT_PASTE)   { do_paste_action();  return 0; }
    if (event == ID_TB_RENAME || event == MENU_EDIT_RENAME) { do_rename_action(); return 0; }
    if (event == ID_TB_DELETE || event == MENU_EDIT_DELETE) { do_delete_action(); return 0; }
    
    /* New Folder (toolbar + menu) */
    if (event == ID_TB_NEW_FOLDER || event == MENU_FILE_NEW_FOLDER) {
        char *folder_name = show_prompt_dialog("New Folder", "Name:", "");
        if (folder_name && folder_name[0]) {
            char folder_path[256];
            build_path(folder_path, sizeof(folder_path), state.current_path, folder_name);
            
            if (sys_mkdir(folder_path) == 0) {
                tree_reload_children_for_path(state.current_path);
                tree_ensure_path(state.current_path);
                refresh_display();
            } else {
                sys_win_msgbox("Failed to create folder", "OK", "New Folder");
                sys_win_redraw_all();
            }
        }
        return 0;
    }
    
    /* Toolbar and menu: New File */
    if (event == ID_TB_NEW_FILE || event == MENU_FILE_NEW_FILE) {
        char *file_name = show_prompt_dialog("New File", "Name:", "");
        if (file_name && file_name[0]) {
            char file_path[256];
            build_path(file_path, sizeof(file_path), state.current_path, file_name);
            
            int fd = sys_open(file_path, "w");
            if (fd >= 0) {
                sys_close(fd);
                refresh_display();
            } else {
                sys_win_msgbox("Failed to create file", "OK", "New File");
                sys_win_redraw_all();
            }
        }
        return 0;
    }
    
    /* Menu: Open removed */
    
    /* Menu: Refresh */
    if (event == MENU_VIEW_REFRESH) {
        tree_reload_children_for_path(state.current_path);
        tree_ensure_path(state.current_path);
        refresh_display();
        return 0;
    }
    
    /* Menu: About */
    if (event == MENU_HELP_ABOUT) {
        sys_win_msgbox("osLET File Manager\n\nThis is a work in progress!", "OK", "About");
        sys_win_redraw_all();
        return 0;
    }
    
    return 0;
}

/* Main entry point */
__attribute__((section(".entry"), used))
void _start(void) {
    memset(&state, 0, sizeof(state));
    state.selected_file_index = -1;
    state.selected_tree_index = -1;
    state.clipboard_is_cut = 0;
    
    sys_getcwd(state.current_path, sizeof(state.current_path));
    if (!state.current_path[0])
        strcpy(state.current_path, "C:/");
    normalize_path(state.current_path, sizeof(state.current_path));
    
    state.form = sys_win_create_form("File Manager", 80, 60, 450, 305);
    if (!state.form) sys_exit();
    
    sys_win_set_icon(state.form, "C:/ICONS/CABINET.ICO");
    sys_win_set_resizable(state.form, 1);
    
    /* Menus */
    sys_win_menubar_enable(state.form);
    
    int file_menu = sys_win_menubar_add_menu(state.form, "File");
    sys_win_menubar_add_item(state.form, file_menu, "New Folder", MENU_FILE_NEW_FOLDER);
    sys_win_menubar_add_item(state.form, file_menu, "New File", MENU_FILE_NEW_FILE);
    /* open menu item removed - users double-click files instead */
    sys_win_menubar_add_item(state.form, file_menu, "Exit", MENU_FILE_EXIT);
    
    int edit_menu = sys_win_menubar_add_menu(state.form, "Edit");
    sys_win_menubar_add_item(state.form, edit_menu, "Cut  Ctrl+X", MENU_EDIT_CUT);
    sys_win_menubar_add_item(state.form, edit_menu, "Copy  Ctrl+C", MENU_EDIT_COPY);
    sys_win_menubar_add_item(state.form, edit_menu, "Paste  Ctrl+V", MENU_EDIT_PASTE);
    sys_win_menubar_add_item(state.form, edit_menu, "Rename", MENU_EDIT_RENAME);
    sys_win_menubar_add_item(state.form, edit_menu, "Delete  Del", MENU_EDIT_DELETE);
    
    int go_menu = sys_win_menubar_add_menu(state.form, "Go");
    sys_win_menubar_add_item(state.form, go_menu, "Back", MENU_GO_BACK);
    sys_win_menubar_add_item(state.form, go_menu, "Home (C:/)", MENU_GO_HOME);
    
    int view_menu = sys_win_menubar_add_menu(state.form, "View");
    sys_win_menubar_add_item(state.form, view_menu, "Refresh", MENU_VIEW_REFRESH);
    
    int help_menu = sys_win_menubar_add_menu(state.form, "Help");
    sys_win_menubar_add_item(state.form, help_menu, "About", MENU_HELP_ABOUT);
    
    /* Path textbox */
    gui_control_t path_textbox = {0};
    path_textbox.type = CTRL_TEXTBOX;
    path_textbox.x = 8; path_textbox.y = 8; path_textbox.w = 408; path_textbox.h = 20;
    path_textbox.bg = 15;
    path_textbox.id = ID_PATH_TEXTBOX; path_textbox.font_size = 12;
    path_textbox.border = 1; path_textbox.border_color = 8;
    path_textbox.textbox.max_length = 255;
    char upper_path[256];
    strcpy(upper_path, state.current_path);
    str_toupper(upper_path);
    strncpy(path_textbox.text, upper_path, sizeof(path_textbox.text) - 1);
    sys_win_add_control(state.form, &path_textbox);

    /* Toolbar frame */
    gui_control_t toolbar_frame = {0};
    toolbar_frame.type = CTRL_FRAME;
    toolbar_frame.x = 8; toolbar_frame.y = TOOLBAR_Y - 6;
    toolbar_frame.w = 434; toolbar_frame.h = TOOLBAR_H;
    toolbar_frame.fg = 8; toolbar_frame.bg = 7;
    toolbar_frame.id = ID_TOOLBAR_FRAME;
    toolbar_frame.font_size = 12; toolbar_frame.border = 1; toolbar_frame.border_color = 8;
    sys_win_add_control(state.form, &toolbar_frame);

    /* Toolbar buttons */
    const int btn_x_start = 10;
    const int btn_y = TOOLBAR_Y + 5;
    const int btn_size = 22;
    const int btn_spacing = 24;
    const struct { int id; const char *icon; } tb_defs[] = {
        { ID_TB_CUT,        "C:/ICONS/CUT.ICO" },
        { ID_TB_COPY,       "C:/ICONS/CPY.ICO" },
        { ID_TB_PASTE,      "C:/ICONS/PST.ICO" },
        { ID_TB_RENAME,     "C:/ICONS/REN.ICO" },
        { ID_TB_DELETE,     "C:/ICONS/DEL.ICO" },
        { ID_TB_NEW_FOLDER, "C:/ICONS/DRA.ICO" },
        { ID_TB_NEW_FILE,   "C:/ICONS/FLA.ICO" },
        { ID_TB_BACK,       "C:/ICONS/BCK.ICO" },
    };
    int tb_count = sizeof(tb_defs) / sizeof(tb_defs[0]);
    for (int i = 0; i < tb_count; i++) {
        gui_control_t tb = {0};
        tb.type = CTRL_BUTTON;
        tb.x = btn_x_start + i * btn_spacing; tb.y = btn_y;
        tb.w = btn_size; tb.h = btn_size;
        tb.id = tb_defs[i].id; tb.font_size = 12;
        tb.bg = -1;
        sys_win_add_control(state.form, &tb);
        ctrl_set_image(state.form, tb_defs[i].id, tb_defs[i].icon);
    }

    /* Folder tree */
    gui_control_t treeview = {0};
    treeview.type = CTRL_TREEVIEW;
    treeview.x = TREE_X - 3; treeview.y = CONTENT_Y - 1;
    treeview.w = TREE_W + 8; treeview.h = 192;
    treeview.fg = 0; treeview.bg = 15;
    treeview.id = ID_TREEVIEW; treeview.font_size = 12;
    treeview.treeview.row_height = TREE_ITEM_H;
    sys_win_add_control(state.form, &treeview);
    ctrl_tree_set_icons(state.form, ID_TREEVIEW, "C:/ICONS/FLD.ICO", "C:/ICONS/FLO.ICO");

    /* File scrollbar */
    gui_control_t file_scrollbar = {0};
    file_scrollbar.type = CTRL_SCROLLBAR;
    file_scrollbar.x = TREE_X + TREE_W + 12 + MAX_FILE_COLS * (FILE_ICON_W + FILE_PAD);
    file_scrollbar.y = CONTENT_Y - 1;
    file_scrollbar.w = FILE_SCROLLBAR_W;
    file_scrollbar.h = MAX_FILE_ROWS * (FILE_ICON_H + FILE_PAD);
    file_scrollbar.fg = 8; file_scrollbar.bg = 7;
    file_scrollbar.id = ID_FILE_SCROLLBAR; file_scrollbar.font_size = 12;
    sys_win_add_control(state.form, &file_scrollbar);
    ctrl_set_visible(state.form, ID_FILE_SCROLLBAR, 0);

    /* Pre-create file icon controls */
    int max_file_count = MAX_FILE_ROWS * MAX_FILE_COLS;
    for (int i = 0; i < max_file_count; i++) {
        int col = i % MAX_FILE_COLS;
        int row = i / MAX_FILE_COLS;
        gui_control_t file_icon = {0};
        file_icon.type = CTRL_ICON;
        file_icon.x = (TREE_X + TREE_W + 12) + col * (FILE_ICON_W + FILE_PAD);
        file_icon.y = CONTENT_Y + row * (FILE_ICON_H + FILE_PAD);
        file_icon.w = FILE_ICON_W; file_icon.h = FILE_ICON_H;
        file_icon.bg = 15;
        file_icon.id = ID_FILE_BASE + i; file_icon.font_size = 9;
        sys_win_add_control(state.form, &file_icon);
        ctrl_set_visible(state.form, ID_FILE_BASE + i, 0);
    }
    
    tree_init_model();
    tree_ensure_path(state.current_path);

    /* Compute initial layout and display */
    relayout();
    refresh_display();
    
    int running = 1;
    while (running) {
        int ev = sys_win_pump_events(state.form);
        if (ev == -3) { running = 0; break; }
        if (ev == -4) { relayout(); refresh_display(); }
        if (ev == -1 || ev == -2) { sys_win_mark_dirty(state.form); }
        if (ev > 0 && handle_event(ev)) { running = 0; break; }
        if (sys_win_is_focused(state.form)) {
            gui_form_t *form = (gui_form_t*)state.form;
            if (form && form->focused_control_id < 0) {
                int key = sys_get_key_nonblock();
                if (key == 3 || key == 22 || key == 24 || key == KEY_DELETE)
                    handle_shortcut(key);
            }
        }
        sys_yield();
    }
    
    sys_win_destroy_form(state.form);
    sys_exit();
}
