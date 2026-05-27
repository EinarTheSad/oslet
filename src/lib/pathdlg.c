#include "../syscall.h"
#include "string.h"

#define PATH_DLG_ID_TREE   1
#define PATH_DLG_ID_LIST   2
#define PATH_DLG_ID_TEXT   3
#define PATH_DLG_ID_LABEL  4
#define PATH_DLG_ID_OK     5
#define PATH_DLG_ID_CANCEL 6

#define PATH_DLG_X 130
#define PATH_DLG_Y 104
#define PATH_DLG_W 380
#define PATH_DLG_H 240
#define PATH_DLG_CONTROL_Y_OFFSET 20
#define PATH_DLG_TEXT_X 52
#define PATH_DLG_TEXT_Y 187
#define PATH_DLG_TEXT_W 234
#define PATH_DLG_TEXT_H 20
#define PATH_DLG_OK_X 298
#define PATH_DLG_OK_Y 26
#define PATH_DLG_OK_W 70
#define PATH_DLG_OK_H 23

#define PATH_TREE_MAX 64
#define PATH_LIST_MAX 128
#define PATH_SCAN_MAX 128

typedef struct {
    char name[64];
    char full_path[256];
    uint8_t level;
    uint8_t flags;
} path_tree_node_t;

typedef struct {
    char name[64];
    char full_path[256];
    uint8_t flags;
} path_list_node_t;

typedef struct {
    void *form;
    char current_path[256];
    path_tree_node_t tree[PATH_TREE_MAX];
    sys_tree_item_t tree_rows[PATH_TREE_MAX];
    path_list_node_t list[PATH_LIST_MAX];
    sys_list_item_t list_rows[PATH_LIST_MAX];
    sys_dirent_t scan[PATH_SCAN_MAX];
    sys_dirent_t probe[PATH_SCAN_MAX];
    char filter[64];
    int tree_count;
    int list_count;
    int selected_tree;
    int selected_list;
    int save_mode;
    unsigned char last_mouse_buttons;
    int suppress_ok_click;
} path_dialog_state_t;

static path_dialog_state_t state;

static void safe_copy(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    if (!src) src = "";
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static int title_is_save(const char *title) {
    if (!title) return 0;
    for (int i = 0; title[i]; i++) {
        char a = title[i];
        char b = title[i + 1];
        char c = title[i + 2];
        char d = title[i + 3];
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (c >= 'a' && c <= 'z') c -= 32;
        if (d >= 'a' && d <= 'z') d -= 32;
        if (a == 'S' && b == 'A' && c == 'V' && d == 'E')
            return 1;
    }
    return 0;
}

static int has_wildcard(const char *text) {
    if (!text) return 0;
    while (*text) {
        if (*text == '*' || *text == '?')
            return 1;
        text++;
    }
    return 0;
}

static int filename_matches_filter(const char *name) {
    if (!state.filter[0])
        return 1;

    const char *p = state.filter;
    while (*p) {
        while (*p == ' ' || *p == ';' || *p == ',')
            p++;
        if (!*p) break;

        char token[64];
        int len = 0;
        while (*p && *p != ';' && *p != ',' && *p != ' ' && len < (int)sizeof(token) - 1)
            token[len++] = *p++;
        token[len] = '\0';

        if (len > 0 && str_match_wildcard(token, name))
            return 1;
    }

    return 0;
}

static void default_save_name_from_filter(char *dest, size_t dest_size) {
    if (!dest || dest_size == 0) return;
    safe_copy(dest, dest_size, "untitled.txt");

    const char *p = state.filter;
    while (*p == ' ' || *p == ';' || *p == ',')
        p++;
    if (!*p) return;

    char token[64];
    int len = 0;
    while (*p && *p != ';' && *p != ',' && *p != ' ' && len < (int)sizeof(token) - 1)
        token[len++] = *p++;
    token[len] = '\0';

    char *dot = strrchr(token, '.');
    if (!dot || !dot[1]) return;
    for (int i = 1; dot[i]; i++) {
        if (dot[i] == '*' || dot[i] == '?' || dot[i] == ';' || dot[i] == ',' || dot[i] == ' ')
            return;
    }

    safe_copy(dest, dest_size, "untitled");
    if (strlen(dest) + strlen(dot) < dest_size)
        strcat(dest, dot);
}

static void ensure_dir_suffix(char *path, size_t size) {
    size_t len = strlen(path);
    if (len == 0 || size == 0) return;
    if (len == 2 && path[1] == ':' && len + 1 < size) {
        path[2] = '/';
        path[3] = '\0';
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
    if (base && base[0])
        safe_copy(dest, dest_size, base);

    size_t len = strlen(dest);
    if (len > 0 && dest[len - 1] != '/' && dest[len - 1] != ':') {
        if (len + 1 < dest_size) {
            dest[len] = '/';
            dest[len + 1] = '\0';
            len++;
        }
    }

    if (name && name[0] && len < dest_size - 1)
        safe_copy(dest + len, dest_size - len, name);
}

static int path_is_dir(const char *path) {
    sys_dirent_t tmp[1];
    return sys_readdir(path, tmp, 1) >= 0;
}

static void split_initial_path(const char *initial_path, char *folder, size_t folder_size,
                               char *filename, size_t filename_size) {
    folder[0] = '\0';
    filename[0] = '\0';

    if (initial_path && initial_path[0]) {
        char temp[256];
        safe_copy(temp, sizeof(temp), initial_path);

        if (has_wildcard(temp)) {
            char *slash = strrchr(temp, '/');
            if (slash && slash > temp + 2) {
                safe_copy(filename, filename_size, slash + 1);
                safe_copy(state.filter, sizeof(state.filter), slash + 1);
                if (state.save_mode)
                    default_save_name_from_filter(filename, filename_size);
                *(slash + 1) = '\0';
                safe_copy(folder, folder_size, temp);
                normalize_path(folder, folder_size);
                return;
            } else if (slash && temp[1] == ':') {
                safe_copy(filename, filename_size, slash + 1);
                safe_copy(state.filter, sizeof(state.filter), slash + 1);
                if (state.save_mode)
                    default_save_name_from_filter(filename, filename_size);
                temp[3] = '\0';
                safe_copy(folder, folder_size, temp);
                normalize_path(folder, folder_size);
                return;
            }

            safe_copy(filename, filename_size, temp);
            safe_copy(state.filter, sizeof(state.filter), temp);
            if (state.save_mode)
                default_save_name_from_filter(filename, filename_size);
            safe_copy(folder, folder_size, "C:/");
            return;
        }

        if (path_is_dir(temp)) {
            safe_copy(folder, folder_size, temp);
            normalize_path(folder, folder_size);
            return;
        }

        char *slash = strrchr(temp, '/');
        if (slash && slash > temp + 2) {
            safe_copy(filename, filename_size, slash + 1);
            *(slash + 1) = '\0';
            safe_copy(folder, folder_size, temp);
            normalize_path(folder, folder_size);
            return;
        } else if (slash && temp[1] == ':') {
            safe_copy(filename, filename_size, slash + 1);
            temp[3] = '\0';
            safe_copy(folder, folder_size, temp);
            normalize_path(folder, folder_size);
            return;
        }
    }

    sys_getcwd(folder, folder_size);
    if (!folder[0])
        safe_copy(folder, folder_size, "C:/");
    normalize_path(folder, folder_size);
    if (state.save_mode && !filename[0])
        safe_copy(filename, filename_size, "untitled.txt");
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

static int tree_find_child(int parent, const char *name, size_t name_len) {
    if (parent < 0 || parent >= state.tree_count) return -1;
    int child_level = state.tree[parent].level + 1;
    for (int i = parent + 1; i < state.tree_count; i++) {
        if (state.tree[i].level <= state.tree[parent].level)
            break;
        if (state.tree[i].level == child_level &&
            tree_name_matches(state.tree[i].name, name, name_len))
            return i;
    }
    return -1;
}

static void sort_dirents(sys_dirent_t *entries, int count) {
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
    int count = sys_readdir(path, state.probe, PATH_SCAN_MAX);
    if (count < 0) return 0;
    for (int i = 0; i < count; i++) {
        if (!state.probe[i].is_directory) continue;
        if (strcmp(state.probe[i].name, ".") == 0 || strcmp(state.probe[i].name, "..") == 0) continue;
        return 1;
    }
    return 0;
}

static void tree_load_children(int idx) {
    if (idx < 0 || idx >= state.tree_count) return;
    if (state.tree[idx].flags & TREE_ITEM_LOADED) return;

    int count = sys_readdir(state.tree[idx].full_path, state.scan, PATH_SCAN_MAX);
    if (count < 0) {
        state.tree[idx].flags |= TREE_ITEM_LOADED;
        state.tree[idx].flags &= ~TREE_ITEM_HAS_CHILDREN;
        return;
    }

    int dir_count = 0;
    for (int i = 0; i < count && dir_count < PATH_SCAN_MAX; i++) {
        if (!state.scan[i].is_directory) continue;
        if (strcmp(state.scan[i].name, ".") == 0 || strcmp(state.scan[i].name, "..") == 0) continue;
        if (dir_count != i)
            state.scan[dir_count] = state.scan[i];
        dir_count++;
    }
    sort_dirents(state.scan, dir_count);

    if (dir_count == 0) {
        state.tree[idx].flags |= TREE_ITEM_LOADED;
        state.tree[idx].flags &= ~(TREE_ITEM_HAS_CHILDREN | TREE_ITEM_EXPANDED);
        return;
    }

    int insert_at = idx + 1;
    while (insert_at < state.tree_count && state.tree[insert_at].level > state.tree[idx].level)
        insert_at++;

    for (int i = 0; i < dir_count && state.tree_count < PATH_TREE_MAX; i++) {
        for (int j = state.tree_count; j > insert_at; j--)
            state.tree[j] = state.tree[j - 1];

        path_tree_node_t *child = &state.tree[insert_at];
        safe_copy(child->name, sizeof(child->name), state.scan[i].name);
        build_path(child->full_path, sizeof(child->full_path), state.tree[idx].full_path, state.scan[i].name);
        normalize_path(child->full_path, sizeof(child->full_path));
        child->level = state.tree[idx].level + 1;
        child->flags = TREE_ITEM_FOLDER;
        if (directory_has_subdirs(child->full_path))
            child->flags |= TREE_ITEM_HAS_CHILDREN;
        else
            child->flags |= TREE_ITEM_LOADED;
        state.tree_count++;
        insert_at++;
    }

    state.tree[idx].flags |= TREE_ITEM_LOADED | TREE_ITEM_HAS_CHILDREN;
}

static void tree_init(void) {
    state.tree_count = 1;
    state.selected_tree = 0;
    safe_copy(state.tree[0].name, sizeof(state.tree[0].name), "C:/");
    safe_copy(state.tree[0].full_path, sizeof(state.tree[0].full_path), "C:/");
    state.tree[0].level = 0;
    state.tree[0].flags = TREE_ITEM_FOLDER | TREE_ITEM_HAS_CHILDREN | TREE_ITEM_EXPANDED;
    tree_load_children(0);
}

static void tree_ensure_path(const char *path) {
    if (state.tree_count <= 0)
        tree_init();

    char normalized[256];
    safe_copy(normalized, sizeof(normalized), path);
    normalize_path(normalized, sizeof(normalized));

    int current = 0;
    state.tree[current].flags |= TREE_ITEM_EXPANDED;
    tree_load_children(current);

    if (strcmp(normalized, "C:/") != 0) {
        char temp[256];
        safe_copy(temp, sizeof(temp), normalized);
        int len = strlen(temp);
        if (len > 3 && temp[len - 1] == '/')
            temp[len - 1] = '\0';

        char *token = temp + 3;
        while (*token) {
            char *slash = strchr(token, '/');
            size_t token_len = slash ? (size_t)(slash - token) : strlen(token);
            if (token_len > 0) {
                int child = tree_find_child(current, token, token_len);
                if (child < 0)
                    break;
                current = child;
                if (slash) {
                    state.tree[current].flags |= TREE_ITEM_EXPANDED;
                    tree_load_children(current);
                }
            }
            if (!slash) break;
            token = slash + 1;
        }
    }

    state.selected_tree = current;
}

static void sync_tree(void) {
    if (!state.form || state.tree_count <= 0) return;

    for (int i = 0; i < state.tree_count; i++) {
        safe_copy(state.tree_rows[i].text, sizeof(state.tree_rows[i].text), state.tree[i].name);
        state.tree_rows[i].level = state.tree[i].level;
        state.tree_rows[i].flags = state.tree[i].flags;
        state.tree_rows[i].app_id = (uint16_t)i;
    }

    ctrl_tree_set_items(state.form, PATH_DLG_ID_TREE, state.tree_rows, state.tree_count);
    ctrl_tree_set_selected(state.form, PATH_DLG_ID_TREE, state.selected_tree);
}

static void sort_list_nodes(path_list_node_t *items, int start, int end) {
    for (int i = start + 1; i < end; i++) {
        path_list_node_t tmp = items[i];
        int j = i - 1;
        while (j >= start && strcasecmp(items[j].name, tmp.name) > 0) {
            items[j + 1] = items[j];
            j--;
        }
        items[j + 1] = tmp;
    }
}

static void refresh_list(void) {
    int count = sys_readdir(state.current_path, state.scan, PATH_SCAN_MAX);
    state.list_count = 0;
    state.selected_list = -1;
    if (count < 0) {
        ctrl_list_set_items(state.form, PATH_DLG_ID_LIST, state.list_rows, 0);
        return;
    }

    for (int i = 0; i < count && state.list_count < PATH_LIST_MAX; i++) {
        if (state.scan[i].is_directory) continue;
        if (strcmp(state.scan[i].name, ".") == 0 || strcmp(state.scan[i].name, "..") == 0) continue;
        if (!filename_matches_filter(state.scan[i].name)) continue;
        path_list_node_t *item = &state.list[state.list_count++];
        safe_copy(item->name, sizeof(item->name), state.scan[i].name);
        build_path(item->full_path, sizeof(item->full_path), state.current_path, state.scan[i].name);
        item->flags = 0;
    }

    sort_list_nodes(state.list, 0, state.list_count);

    for (int i = 0; i < state.list_count; i++) {
        safe_copy(state.list_rows[i].text, sizeof(state.list_rows[i].text), state.list[i].name);
        state.list_rows[i].flags = state.list[i].flags;
        state.list_rows[i].app_id = (uint16_t)i;
    }

    ctrl_list_set_items(state.form, PATH_DLG_ID_LIST, state.list_rows, state.list_count);
    ctrl_list_set_selected(state.form, PATH_DLG_ID_LIST, -1);
    sys_ctrl_set_prop(state.form, PATH_DLG_ID_LIST, PROP_LIST_SCROLL, 0);
}

static void set_current_folder(const char *path) {
    safe_copy(state.current_path, sizeof(state.current_path), path);
    normalize_path(state.current_path, sizeof(state.current_path));
    tree_ensure_path(state.current_path);
    sync_tree();
    refresh_list();
    if (!state.save_mode)
        ctrl_set_text(state.form, PATH_DLG_ID_TEXT, state.filter);
}

static int selected_list_index(void) {
    int idx = ctrl_list_get_selected(state.form, PATH_DLG_ID_LIST);
    if (idx < 0 || idx >= state.list_count)
        return -1;
    return idx;
}

static int apply_typed_filter_if_needed(void) {
    const char *typed = ctrl_get_text(state.form, PATH_DLG_ID_TEXT);
    if (!typed || !typed[0] || !has_wildcard(typed))
        return 0;

    int changed = strcmp(typed, state.filter) != 0;
    if (changed) {
        safe_copy(state.filter, sizeof(state.filter), typed);
        refresh_list();
    }

    if (state.save_mode) {
        char save_name[64];
        default_save_name_from_filter(save_name, sizeof(save_name));
        ctrl_set_text(state.form, PATH_DLG_ID_TEXT, save_name);
        return 1;
    }

    return changed;
}

static int text_click_away_filter(void) {
    int mx = 0, my = 0;
    unsigned char buttons = 0;
    sys_get_mouse_state(&mx, &my, &buttons);

    int clicked = (buttons & 1) && !(state.last_mouse_buttons & 1);
    state.last_mouse_buttons = buttons;
    if (!clicked || !state.form)
        return 0;

    gui_form_t *form = (gui_form_t*)state.form;
    int text_x = form->win.x + PATH_DLG_TEXT_X;
    int text_y = form->win.y + PATH_DLG_TEXT_Y + PATH_DLG_CONTROL_Y_OFFSET;
    if (mx >= text_x && mx < text_x + PATH_DLG_TEXT_W &&
        my >= text_y && my < text_y + PATH_DLG_TEXT_H)
        return 0;

    int applied = apply_typed_filter_if_needed();
    if (applied) {
        int ok_x = form->win.x + PATH_DLG_OK_X;
        int ok_y = form->win.y + PATH_DLG_OK_Y + PATH_DLG_CONTROL_Y_OFFSET;
        if (mx >= ok_x && mx < ok_x + PATH_DLG_OK_W &&
            my >= ok_y && my < ok_y + PATH_DLG_OK_H)
            state.suppress_ok_click = 1;
    }

    return applied;
}

static void accept_path(char *out_path, int out_len) {
    const char *typed = ctrl_get_text(state.form, PATH_DLG_ID_TEXT);
    int idx = selected_list_index();

    if (typed && typed[0] && has_wildcard(typed)) {
        apply_typed_filter_if_needed();
        out_path[0] = '\0';
    } else if (typed && typed[0]) {
        if (typed[0] && typed[1] == ':') {
            safe_copy(out_path, out_len, typed);
        } else {
            build_path(out_path, out_len, state.current_path, typed);
        }
    } else if (idx >= 0) {
        safe_copy(out_path, out_len, state.list[idx].full_path);
    } else {
        out_path[0] = '\0';
    }
}

int gui_show_path_dialog_filtered(const char *title, const char *initial_path,
                                  const char *default_filter,
                                  char *out_path, int out_len) {
    if (!title || !out_path || out_len <= 0) return 0;
    memset(&state, 0, sizeof(state));
    state.selected_tree = 0;
    state.selected_list = -1;
    state.save_mode = title_is_save(title);
    safe_copy(state.filter, sizeof(state.filter), default_filter);

    char initial_folder[256];
    char initial_file[64];
    split_initial_path(initial_path, initial_folder, sizeof(initial_folder),
                       initial_file, sizeof(initial_file));

    void *dlg = sys_win_create_form(title, PATH_DLG_X, PATH_DLG_Y, PATH_DLG_W, PATH_DLG_H);
    if (!dlg) return 0;
    state.form = dlg;

    gui_control_t tree = {0};
    tree.type = CTRL_TREEVIEW;
    tree.x = 8; tree.y = 8; tree.w = 122; tree.h = 166;
    tree.fg = 0; tree.bg = 15; tree.id = PATH_DLG_ID_TREE; tree.font_size = 12;
    tree.treeview.row_height = 18;
    sys_win_add_control(dlg, &tree);
    ctrl_tree_set_icons(dlg, PATH_DLG_ID_TREE, "C:/ICONS/FLD.ICO", "C:/ICONS/FLO.ICO");

    gui_control_t list = {0};
    list.type = CTRL_LISTBOX;
    list.x = 136; list.y = 8; list.w = 150; list.h = 166;
    list.fg = 0; list.bg = 15; list.id = PATH_DLG_ID_LIST; list.font_size = 12;
    list.listbox.row_height = 16;
    sys_win_add_control(dlg, &list);

    gui_control_t label = {0};
    label.type = CTRL_LABEL;
    label.x = 8; label.y = 190; label.fg = 0; label.bg = -1;
    label.id = PATH_DLG_ID_LABEL; label.font_size = 12;
    safe_copy(label.text, sizeof(label.text), "File:");
    sys_win_add_control(dlg, &label);

    gui_control_t textbox = {0};
    textbox.type = CTRL_TEXTBOX;
    textbox.x = PATH_DLG_TEXT_X; textbox.y = PATH_DLG_TEXT_Y;
    textbox.w = PATH_DLG_TEXT_W; textbox.h = PATH_DLG_TEXT_H;
    textbox.fg = 0; textbox.bg = 15; textbox.id = PATH_DLG_ID_TEXT; textbox.font_size = 12;
    textbox.textbox.max_length = 255;
    safe_copy(textbox.text, sizeof(textbox.text), initial_file);
    sys_win_add_control(dlg, &textbox);

    gui_control_t ok = {0};
    ok.type = CTRL_BUTTON;
    ok.x = PATH_DLG_OK_X; ok.y = PATH_DLG_OK_Y;
    ok.w = PATH_DLG_OK_W; ok.h = PATH_DLG_OK_H;
    ok.fg = 0; ok.bg = -1; ok.id = PATH_DLG_ID_OK; ok.font_size = 12;
    safe_copy(ok.text, sizeof(ok.text), state.save_mode ? "Save" : "Open");
    sys_win_add_control(dlg, &ok);

    gui_control_t cancel = {0};
    cancel.type = CTRL_BUTTON;
    cancel.x = 298; cancel.y = 56; cancel.w = 70; cancel.h = 23;
    cancel.fg = 0; cancel.bg = -1; cancel.id = PATH_DLG_ID_CANCEL; cancel.font_size = 12;
    safe_copy(cancel.text, sizeof(cancel.text), "Cancel");
    sys_win_add_control(dlg, &cancel);

    tree_init();
    set_current_folder(initial_folder);
    if (initial_file[0])
        ctrl_set_text(dlg, PATH_DLG_ID_TEXT, initial_file);

    sys_win_draw(dlg);
    sys_win_redraw_all();
    sys_mouse_invalidate();

    out_path[0] = '\0';
    int init_mx = 0, init_my = 0;
    sys_get_mouse_state(&init_mx, &init_my, &state.last_mouse_buttons);

    while (1) {
        gui_form_t *form = (gui_form_t*)dlg;
        int text_was_focused = form && form->focused_control_id == PATH_DLG_ID_TEXT;
        int filter_applied_before_pump = text_click_away_filter();
        int ev = sys_win_pump_events(dlg);
        int text_lost_focus = text_was_focused && form &&
                              form->focused_control_id != PATH_DLG_ID_TEXT;
        int filter_applied = filter_applied_before_pump;

        if (ev == -3 || ev == PATH_DLG_ID_CANCEL)
            break;

        if (state.suppress_ok_click && ev != PATH_DLG_ID_OK) {
            int mx = 0, my = 0;
            unsigned char buttons = 0;
            sys_get_mouse_state(&mx, &my, &buttons);
            if (!(buttons & 1))
                state.suppress_ok_click = 0;
        }

        if (!filter_applied && ev != PATH_DLG_ID_OK && ev != PATH_DLG_ID_TEXT && text_lost_focus)
            filter_applied = apply_typed_filter_if_needed();

        if ((state.suppress_ok_click || filter_applied) && ev == PATH_DLG_ID_OK) {
            state.suppress_ok_click = 0;
            sys_win_draw(dlg);
            sys_win_redraw_all();
            sys_yield();
            continue;
        }

        if (filter_applied && !filter_applied_before_pump && ev == PATH_DLG_ID_LIST) {
            ctrl_list_clear_action(dlg, PATH_DLG_ID_LIST);
            sys_win_draw(dlg);
            sys_win_redraw_all();
            sys_yield();
            continue;
        }

        if (ev == -1 || ev == -2 || ev == -4) {
            sys_win_draw(dlg);
            sys_win_redraw_all();
        } else if (ev == PATH_DLG_ID_TREE) {
            int action = ctrl_tree_get_action(dlg, PATH_DLG_ID_TREE);
            int idx = ctrl_tree_get_action_index(dlg, PATH_DLG_ID_TREE);
            ctrl_tree_clear_action(dlg, PATH_DLG_ID_TREE);
            if (idx >= 0 && idx < state.tree_count) {
                if (action == TREE_ACTION_TOGGLE) {
                    if (state.tree[idx].flags & TREE_ITEM_EXPANDED) {
                        state.tree[idx].flags &= ~TREE_ITEM_EXPANDED;
                    } else {
                        tree_load_children(idx);
                        if (state.tree[idx].flags & TREE_ITEM_HAS_CHILDREN)
                            state.tree[idx].flags |= TREE_ITEM_EXPANDED;
                    }
                    sync_tree();
                    sys_win_draw(dlg);
                } else if (action == TREE_ACTION_SELECT) {
                    set_current_folder(state.tree[idx].full_path);
                    sys_win_draw(dlg);
                }
            }
        } else if (ev == PATH_DLG_ID_LIST) {
            int idx = ctrl_list_get_action_index(dlg, PATH_DLG_ID_LIST);
            ctrl_list_clear_action(dlg, PATH_DLG_ID_LIST);
            if (idx >= 0 && idx < state.list_count) {
                state.selected_list = idx;
                if (state.list[idx].flags & LIST_ITEM_DIRECTORY)
                    ctrl_set_text(dlg, PATH_DLG_ID_TEXT, "");
                else
                    ctrl_set_text(dlg, PATH_DLG_ID_TEXT, state.list[idx].name);
                sys_win_draw(dlg);
            }
        } else if (ev == PATH_DLG_ID_OK || ev == PATH_DLG_ID_TEXT) {
            accept_path(out_path, out_len);
            if (out_path[0]) {
                sys_win_destroy_form(dlg);
                sys_win_redraw_all();
                return 1;
            }
            sys_win_draw(dlg);
            sys_win_redraw_all();
        }

        sys_yield();
    }

    sys_win_destroy_form(dlg);
    sys_win_redraw_all();
    return 0;
}

int gui_show_path_dialog(const char *title, const char *initial_path,
                         char *out_path, int out_len) {
    return gui_show_path_dialog_filtered(title, initial_path, NULL, out_path, out_len);
}
