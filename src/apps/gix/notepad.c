#include "../../syscall.h"
#include "../../lib/string.h"
#include "../../lib/stdio.h"
#include "../../lib/pathdlg.h"
#include "../../lib/app.h"
#include "../../lib/gix_app.h"

OSLET_APP("Notepad", OSLET_KIND_GIX, "C:/ICONS/TEXT.ICO", OSLET_APP_FLAG_NONE);

#define ID_TEXT         1
#define ID_FIND_TEXT    3
#define ID_FIND_NEXT    4
#define ID_FIND_CLOSE   5

#define MENU_FILE_EXIT       100
#define MENU_FILE_NEW        101
#define MENU_FILE_OPEN       102
#define MENU_FILE_SAVE       103
#define MENU_FILE_SAVE_AS    104
#define MENU_EDIT_CUT        201
#define MENU_EDIT_COPY       202
#define MENU_EDIT_PASTE      203
#define MENU_EDIT_SELECT_ALL 204
#define MENU_EDIT_FIND       205
#define MENU_EDIT_FIND_NEXT  206
#define MENU_HELP_ABOUT      500

#define NOTEPAD_X       100
#define NOTEPAD_Y        50
#define NOTEPAD_W       360
#define NOTEPAD_H       255
#define TEXTBOX_MARGIN    5
#define TEXTBOX_BOTTOM   52
#define FIND_HEIGHT      20
#define FIND_GAP           6
#define FIND_BOTTOM       12
#define IO_CHUNK_SIZE    512
#define FIND_TEXT_SIZE   128
#define NOTEPAD_FILE_FILTER "*.bas;*.c;*.cfg;*.grp;*.ini;*.md;*.txt"

typedef struct {
    void *form;
    char current_file_path[256];
    uint32_t saved_revision;
    int modified;
    int find_visible;
} notepad_state_t;

static gui_control_t Form1_controls[] = {
    {
        .type = CTRL_TEXTBOX, .x = TEXTBOX_MARGIN, .y = TEXTBOX_MARGIN,
        .w = 10, .h = 10, .fg = -1, .bg = -1, .text = "", .id = ID_TEXT,
        .font_type = FONT_NORMAL, .font_size = 12, .border = 1,
        .border_color = 0, .textbox = { .is_multiline = 1 }
    },
    {
        .type = CTRL_TEXTBOX | 0x80, .x = TEXTBOX_MARGIN, .y = 0, .w = 10, .h = FIND_HEIGHT,
        .fg = -1, .bg = -1, .text = "", .id = ID_FIND_TEXT,
        .font_type = FONT_NORMAL, .font_size = 12, .border = 1,
        .border_color = 0, .textbox = { .max_length = FIND_TEXT_SIZE }
    },
    {
        .type = CTRL_BUTTON | 0x80, .x = 0, .y = 0, .w = 62, .h = FIND_HEIGHT,
        .fg = 0, .bg = 7, .text = "Find Next", .id = ID_FIND_NEXT,
        .font_type = FONT_NORMAL, .font_size = 10, .border = 1, .border_color = 0
    },
    {
        .type = CTRL_BUTTON | 0x80, .x = 0, .y = 0, .w = 42, .h = FIND_HEIGHT,
        .fg = 0, .bg = 7, .text = "Close", .id = ID_FIND_CLOSE,
        .font_type = FONT_NORMAL, .font_size = 10, .border = 1, .border_color = 0
    }
};

static int is_valid_text_char(uint8_t c) {
    return c == '\t' || c == '\n' || (c >= 0x20 && c < 0x7F);
}

static int append_text(char **text, uint32_t *length, uint32_t *capacity,
                       const char *append, uint32_t append_length) {
    if (append_length > 0x7FFFFFFFU - *length) return -1;
    uint32_t required = *length + append_length;
    if (required > *capacity) {
        uint32_t new_capacity = *capacity ? *capacity : IO_CHUNK_SIZE;
        while (new_capacity < required) {
            if (new_capacity > 0x3FFFFFFFU) {
                new_capacity = required;
                break;
            }
            new_capacity *= 2;
        }

        char *new_text = (char*)sys_malloc(new_capacity);
        if (!new_text) return -1;
        if (*length) memcpy(new_text, *text, *length);
        if (*text) sys_free(*text);
        *text = new_text;
        *capacity = new_capacity;
    }

    if (append_length) memcpy(*text + *length, append, append_length);
    *length = required;
    return 0;
}

static int load_text_file(notepad_state_t *state, const char *path) {
    int fd = sys_open(path, "r");
    if (fd < 0) return -1;

    char *content = NULL;
    uint32_t content_length = 0;
    uint32_t content_capacity = 0;
    char buffer[IO_CHUNK_SIZE];
    char filtered[IO_CHUNK_SIZE];
    int result = 0;

    sys_mouse_busy_begin();
    for (;;) {
        int bytes_read = sys_read(fd, buffer, sizeof(buffer));
        if (bytes_read == 0) break;
        if (bytes_read < 0) {
            result = -1;
            break;
        }

        uint32_t filtered_length = 0;
        for (int i = 0; i < bytes_read; i++) {
            uint8_t c = (uint8_t)buffer[i];
            if (c == '\r') continue;
            if (!is_valid_text_char(c)) {
                result = -2;
                break;
            }
            filtered[filtered_length++] = (char)c;
        }
        if (result != 0) break;
        if (append_text(&content, &content_length, &content_capacity,
                        filtered, filtered_length) != 0) {
            result = -3;
            break;
        }
    }

    if (sys_close(fd) != 0 && result == 0) result = -1;
    if (result == 0 &&
        sys_win_textbox_replace(state->form, ID_TEXT, content, content_length) != 0)
        result = -3;
    sys_mouse_busy_end();

    if (content) sys_free(content);
    return result;
}

static int save_text_file(void *form, const char *path) {
    uint32_t length;
    if (sys_win_textbox_get_length(form, ID_TEXT, &length) != 0) return -1;

    int fd = sys_open(path, "w");
    if (fd < 0) return -1;

    char buffer[IO_CHUNK_SIZE];
    uint32_t offset = 0;
    int result = 0;
    sys_mouse_busy_begin();
    while (offset < length) {
        uint32_t chunk_length = length - offset;
        if (chunk_length > sizeof(buffer)) chunk_length = sizeof(buffer);
        int copied = sys_win_textbox_copy_range(form, ID_TEXT, offset,
                                                buffer, chunk_length);
        if (copied != (int)chunk_length ||
            sys_write_file(fd, buffer, chunk_length) != (int)chunk_length) {
            result = -1;
            break;
        }
        offset += chunk_length;
    }

    if (sys_close(fd) != 0) result = -1;
    sys_mouse_busy_end();
    return result;
}

static int prompt_for_path(notepad_state_t *state, const char *title,
                           char *out_path, int out_len) {
    char initial_path[256] = "C:/untitled.txt";
    int is_open = title && title[0] == 'O';

    if (state && state->current_file_path[0]) {
        strncpy(initial_path, state->current_file_path, sizeof(initial_path) - 1);
        initial_path[sizeof(initial_path) - 1] = '\0';
        if (is_open) {
            char *slash = strrchr(initial_path, '/');
            if (slash && slash >= initial_path + 2) {
                *(slash + 1) = '\0';
                if (strlen(initial_path) + strlen(NOTEPAD_FILE_FILTER) < sizeof(initial_path))
                    strcat(initial_path, NOTEPAD_FILE_FILTER);
            } else {
                strcpy(initial_path, "C:/" NOTEPAD_FILE_FILTER);
            }
        }
    } else if (is_open) {
        strcpy(initial_path, "C:/" NOTEPAD_FILE_FILTER);
    }

    return gui_show_path_dialog_filtered(title, initial_path, NOTEPAD_FILE_FILTER,
                                         out_path, out_len);
}

static gui_control_t *text_control(notepad_state_t *state) {
    return state ? sys_win_get_control(state->form, ID_TEXT) : NULL;
}

static void update_title(notepad_state_t *state) {
    char title[64];
    const char *name = "Untitled";
    if (state->current_file_path[0]) {
        const char *slash = strrchr(state->current_file_path, '/');
        name = slash ? slash + 1 : state->current_file_path;
    }
    snprintf(title, sizeof(title), "%s%s - Notepad",
             state->modified ? "*" : "", name);
    if (sys_win_set_title(state->form, title) != 0)
        sys_win_msgbox("Could not update the window title.", "OK", "Notepad");
}

static void update_document_state(notepad_state_t *state) {
    gui_control_t *ctrl = text_control(state);
    if (!ctrl) return;

    int modified = ctrl->textbox.revision != state->saved_revision;
    if (modified != state->modified) {
        state->modified = modified;
        update_title(state);
    }
}

static void mark_document_saved(notepad_state_t *state) {
    gui_control_t *ctrl = text_control(state);
    state->saved_revision = ctrl ? ctrl->textbox.revision : 0;
    state->modified = 0;
    update_title(state);
}

static int notepad_save_to_path(notepad_state_t *state, const char *path) {
    if (save_text_file(state->form, path) != 0) {
        sys_win_msgbox("Failed to save the complete file.", "OK", "Save");
        return 0;
    }

    strncpy(state->current_file_path, path, sizeof(state->current_file_path) - 1);
    state->current_file_path[sizeof(state->current_file_path) - 1] = '\0';
    mark_document_saved(state);
    return 1;
}

static int notepad_save(notepad_state_t *state, int save_as) {
    char path[sizeof(state->current_file_path)] = "";
    if (!save_as && state->current_file_path[0])
        return notepad_save_to_path(state, state->current_file_path);

    if (!prompt_for_path(state, "Save As", path, sizeof(path))) return 0;
    return notepad_save_to_path(state, path);
}

static int confirm_discard_changes(notepad_state_t *state) {
    update_document_state(state);
    if (!state->modified) return 1;

    int choice = sys_win_msgbox("Save changes before continuing?",
                                "Yes|No|Cancel", "Notepad");
    if (choice == 1) return notepad_save(state, 0);
    return choice == 2;
}

static int notepad_new(notepad_state_t *state) {
    if (!confirm_discard_changes(state)) return 0;
    if (sys_win_textbox_replace(state->form, ID_TEXT, "", 0) != 0) {
        sys_win_msgbox("Could not create a new document.", "OK", "Notepad");
        return 0;
    }

    state->current_file_path[0] = '\0';
    mark_document_saved(state);
    sys_win_draw(state->form);
    return 0;
}

static int notepad_open(notepad_state_t *state) {
    if (!confirm_discard_changes(state)) return 0;

    char path[sizeof(state->current_file_path)] = "";
    if (!prompt_for_path(state, "Open", path, sizeof(path))) return 0;

    int result = load_text_file(state, path);
    if (result != 0) {
        const char *message = result == -2
            ? "The file contains unsupported or binary data."
            : result == -3
                ? "Not enough memory to load the file."
                : "Cannot read file.";
        sys_win_msgbox(message, "OK", "Open");
        return 0;
    }

    strncpy(state->current_file_path, path, sizeof(state->current_file_path) - 1);
    state->current_file_path[sizeof(state->current_file_path) - 1] = '\0';
    mark_document_saved(state);
    sys_win_draw(state->form);
    return 0;
}

static char ascii_lower(char c) {
    return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c;
}

static int find_in_range(notepad_state_t *state, const char *query,
                         uint32_t query_length, uint32_t start, uint32_t end,
                         uint32_t *found) {
    if (end < start || end - start < query_length) return 0;

    char buffer[IO_CHUNK_SIZE + FIND_TEXT_SIZE];
    uint32_t block = start;
    while (block <= end - query_length) {
        uint32_t remaining = end - block;
        uint32_t read_length = IO_CHUNK_SIZE + query_length - 1;
        if (read_length > remaining) read_length = remaining;
        int copied = sys_win_textbox_copy_range(state->form, ID_TEXT, block,
                                                buffer, read_length);
        if (copied != (int)read_length) return -1;

        uint32_t starts = read_length - query_length + 1;
        if (starts > IO_CHUNK_SIZE) starts = IO_CHUNK_SIZE;
        for (uint32_t i = 0; i < starts; i++) {
            uint32_t j = 0;
            while (j < query_length &&
                   ascii_lower(buffer[i + j]) == ascii_lower(query[j])) j++;
            if (j == query_length) {
                *found = block + i;
                return 1;
            }
        }
        block += starts;
    }
    return 0;
}

static void notepad_find_next(notepad_state_t *state) {
    const char *query = ctrl_get_text(state->form, ID_FIND_TEXT);
    uint32_t query_length = query ? (uint32_t)strlen(query) : 0;
    if (!query_length) {
        sys_win_msgbox("Enter text to find.", "OK", "Find");
        return;
    }

    gui_control_t *ctrl = text_control(state);
    uint32_t length;
    if (!ctrl || sys_win_textbox_get_length(state->form, ID_TEXT, &length) != 0) {
        sys_win_msgbox("Could not search the document.", "OK", "Find");
        return;
    }

    uint32_t start = ctrl->textbox.cursor_pos;
    if (start > length) start = length;
    uint32_t found;
    int result = find_in_range(state, query, query_length, start, length, &found);
    if (result == 0 && start > 0)
        result = find_in_range(state, query, query_length, 0, start, &found);
    if (result < 0) {
        sys_win_msgbox("Could not search the document.", "OK", "Find");
    } else if (result == 0) {
        sys_win_msgbox("Text not found.", "OK", "Find");
    } else {
        sys_win_textbox_select_range(state->form, ID_TEXT, found, query_length);
        sys_win_draw(state->form);
    }
}

static void update_layout(notepad_state_t *state) {
    gui_form_t *form = state->form;
    int win_w = form->win.w;
    int win_h = form->win.h;
    int control_y_offset = 20 +
        (form->menubar_enabled ? MENUBAR_HEIGHT : 0);
    int find_y = win_h - control_y_offset - FIND_BOTTOM - FIND_HEIGHT;
    int text_h = state->find_visible
        ? find_y - FIND_GAP - TEXTBOX_MARGIN
        : win_h - TEXTBOX_BOTTOM;
    int text_w = win_w - TEXTBOX_MARGIN * 2;
    if (text_w < 32) text_w = 32;
    if (text_h < 16) text_h = 16;

    sys_ctrl_set_prop(state->form, ID_TEXT, PROP_X, TEXTBOX_MARGIN);
    sys_ctrl_set_prop(state->form, ID_TEXT, PROP_Y, TEXTBOX_MARGIN);
    sys_ctrl_set_prop(state->form, ID_TEXT, PROP_W, text_w);
    sys_ctrl_set_prop(state->form, ID_TEXT, PROP_H, text_h);

    int close_w = 42;
    int next_w = 62;
    int find_w = win_w - TEXTBOX_MARGIN * 2 - close_w - next_w - 8;
    if (find_w < 32) find_w = 32;
    sys_ctrl_set_prop(state->form, ID_FIND_TEXT, PROP_X, TEXTBOX_MARGIN);
    sys_ctrl_set_prop(state->form, ID_FIND_TEXT, PROP_Y, find_y);
    sys_ctrl_set_prop(state->form, ID_FIND_TEXT, PROP_W, find_w);
    sys_ctrl_set_prop(state->form, ID_FIND_TEXT, PROP_H, FIND_HEIGHT);
    sys_ctrl_set_prop(state->form, ID_FIND_NEXT, PROP_X, TEXTBOX_MARGIN + find_w + 4);
    sys_ctrl_set_prop(state->form, ID_FIND_NEXT, PROP_Y, find_y);
    sys_ctrl_set_prop(state->form, ID_FIND_CLOSE, PROP_X,
                      TEXTBOX_MARGIN + find_w + next_w + 6);
    sys_ctrl_set_prop(state->form, ID_FIND_CLOSE, PROP_Y, find_y);
    sys_ctrl_set_prop(state->form, ID_FIND_TEXT, PROP_VISIBLE, state->find_visible);
    sys_ctrl_set_prop(state->form, ID_FIND_NEXT, PROP_VISIBLE, state->find_visible);
    sys_ctrl_set_prop(state->form, ID_FIND_CLOSE, PROP_VISIBLE, state->find_visible);
}

static void show_find_bar(notepad_state_t *state) {
    state->find_visible = 1;
    update_layout(state);
    sys_win_focus_control(state->form, ID_FIND_TEXT);
    sys_win_draw(state->form);
}

static void close_find_bar(notepad_state_t *state) {
    state->find_visible = 0;
    update_layout(state);
    sys_win_focus_control(state->form, ID_TEXT);
    sys_win_draw(state->form);
}

static void perform_text_edit(notepad_state_t *state, int command) {
    int result = sys_ctrl_set_prop(state->form, ID_TEXT, PROP_TEXTBOX_EDIT, command);
    if (result < 0) {
        sys_win_msgbox("Not enough memory to complete the edit.", "OK", "Notepad");
        return;
    }
    if (result) {
        sys_win_draw(state->form);
        update_document_state(state);
    }
}

static int handle_shortcut(notepad_state_t *state, int key) {
    switch (key) {
        case 14:
            return notepad_new(state);
        case 15:
            return notepad_open(state);
        case 19:
            notepad_save(state, 0);
            return 0;
        case 6:
            show_find_bar(state);
            return 0;
        default:
            return 0;
    }
}

static int handle_events(void *form, const sys_win_event_t *event, void *userdata) {
    notepad_state_t *state = userdata;
    (void)form;

    if (event->type == SYS_WIN_EVENT_CONTROL_KEY)
        return handle_shortcut(state, event->id);

    if (event->type == SYS_WIN_EVENT_TEXT_UPDATE) {
        update_document_state(state);
        return 0;
    }
    if (event->type == SYS_WIN_EVENT_TEXT_ERROR) {
        sys_win_msgbox("Not enough memory to edit this document.", "OK", "Notepad");
        return 0;
    }
    if (event->type == SYS_WIN_EVENT_CLOSE)
        return confirm_discard_changes(state) ? 1 : GIX_APP_EVENT_CANCEL_CLOSE;

    if (event->type == SYS_WIN_EVENT_CONTROL) {
        if (event->id == ID_FIND_TEXT || event->id == ID_FIND_NEXT) {
            notepad_find_next(state);
            return 0;
        }
        if (event->id == ID_FIND_CLOSE) {
            close_find_bar(state);
            return 0;
        }
    }

    if (event->type == SYS_WIN_EVENT_MENU) {
        switch (event->id) {
            case MENU_FILE_OPEN:
                return notepad_open(state);
            case MENU_FILE_NEW:
                return notepad_new(state);
            case MENU_FILE_SAVE:
                notepad_save(state, 0);
                return 0;
            case MENU_FILE_SAVE_AS:
                notepad_save(state, 1);
                return 0;
            case MENU_FILE_EXIT:
                return confirm_discard_changes(state) ? 1 : 0;
            case MENU_EDIT_CUT:
                perform_text_edit(state, TEXTBOX_EDIT_CUT);
                return 0;
            case MENU_EDIT_COPY:
                perform_text_edit(state, TEXTBOX_EDIT_COPY);
                return 0;
            case MENU_EDIT_PASTE:
                perform_text_edit(state, TEXTBOX_EDIT_PASTE);
                return 0;
            case MENU_EDIT_SELECT_ALL:
                perform_text_edit(state, TEXTBOX_EDIT_SELECT_ALL);
                return 0;
            case MENU_EDIT_FIND:
                show_find_bar(state);
                return 0;
            case MENU_EDIT_FIND_NEXT:
                notepad_find_next(state);
                return 0;
            case MENU_HELP_ABOUT:
                sys_win_msgbox("osLET Notepad v1.3, 2026", "OK", "About");
                return 0;
        }
    }

    return 0;
}

static const gix_app_menu_item_t file_items[] = {
    { "New\tCtrl+N", MENU_FILE_NEW },
    { "Open...\tCtrl+O", MENU_FILE_OPEN },
    { "Save\tCtrl+S", MENU_FILE_SAVE },
    { "Save As...", MENU_FILE_SAVE_AS },
    GIX_MENU_SEPARATOR,
    { "Exit", MENU_FILE_EXIT }
};

static const gix_app_menu_item_t edit_items[] = {
    { "Cut\tCtrl+X", MENU_EDIT_CUT },
    { "Copy\tCtrl+C", MENU_EDIT_COPY },
    { "Paste\tCtrl+V", MENU_EDIT_PASTE },
    GIX_MENU_SEPARATOR,
    { "Select All\tCtrl+A", MENU_EDIT_SELECT_ALL },
    GIX_MENU_SEPARATOR,
    { "Find...\tCtrl+F", MENU_EDIT_FIND },
    { "Find Next", MENU_EDIT_FIND_NEXT }
};

static const gix_app_menu_item_t help_items[] = {
    { "About", MENU_HELP_ABOUT }
};

static const gix_app_menu_t menus[] = {
    { "File", file_items, sizeof(file_items) / sizeof(file_items[0]) },
    { "Edit", edit_items, sizeof(edit_items) / sizeof(edit_items[0]) },
    { "Help", help_items, sizeof(help_items) / sizeof(help_items[0]) }
};

static void notepad_init(void *form, void *userdata) {
    notepad_state_t *state = userdata;
    state->form = form;
    state->current_file_path[0] = '\0';
    state->modified = 0;
    state->find_visible = 0;

    gui_control_t *ctrl = text_control(state);
    state->saved_revision = ctrl ? ctrl->textbox.revision : 0;
    update_layout(state);
    update_title(state);

    char args[256];
    if (sys_getargs(args, sizeof(args)) && args[0]) {
        if (load_text_file(state, args) == 0) {
            strncpy(state->current_file_path, args, sizeof(state->current_file_path) - 1);
            state->current_file_path[sizeof(state->current_file_path) - 1] = '\0';
            mark_document_saved(state);
        } else {
            sys_win_msgbox("Cannot open the requested text file.", "OK", "Notepad");
        }
    }
    if (sys_win_focus_control(state->form, ID_TEXT) != 0)
        sys_win_msgbox("Could not focus the document text.", "OK", "Notepad");
}

static void notepad_resize(void *form, void *userdata) {
    (void)form;
    update_layout((notepad_state_t*)userdata);
}

__attribute__((section(".entry"), used))
void _start(void) {
    static notepad_state_t state;
    static gix_app_desc_t app = {
        .title = "Notepad",
        .icon_path = "C:/ICONS/TEXT.ICO",
        .x = NOTEPAD_X,
        .y = NOTEPAD_Y,
        .w = NOTEPAD_W,
        .h = NOTEPAD_H,
        .resizable = 1,
        .controls = Form1_controls,
        .control_count = sizeof(Form1_controls) / sizeof(Form1_controls[0]),
        .menus = menus,
        .menu_count = sizeof(menus) / sizeof(menus[0]),
        .on_init = notepad_init,
        .on_resize = notepad_resize,
        .on_event = handle_events,
        .userdata = &state
    };

    gix_app_run(&app);
}
