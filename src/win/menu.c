#include "menu.h"
#include "theme.h"
#include "wm_config.h"
#include "../drivers/graphics.h"
#include "../fonts/bmf.h"
#include "../mem/heap.h"
#include "../console.h"

extern bmf_font_t font_n;
extern void mouse_invalidate_buffer(void);
extern void mouse_restore(void);

#define MENU_BG_COLOR       15
#define MENU_HOVER_COLOR    7
#define MENU_TEXT_COLOR     0
#define MENU_DISABLED_COLOR 8
#define MENU_BORDER_COLOR   0

void menu_init(menu_t *menu) {
    if (!menu) return;

    menu->x = 0;
    menu->y = 0;
    menu->w = 0;
    menu->h = 0;
    menu->visible = 0;
    menu->hovered_item = -1;
    menu->item_count = 0;
    menu->saved_bg = NULL;
    menu->just_opened = 0;
}

void menu_destroy(menu_t *menu) {
    if (!menu) return;

    menu_hide(menu);
    menu->item_count = 0;
}

int menu_add_item(menu_t *menu, const char *text, int16_t action_id, uint8_t flags) {
    if (!menu || menu->item_count >= MENU_MAX_ITEMS) return -1;

    menu_item_t *item = &menu->items[menu->item_count];
    strcpy_s(item->text, text, 32);
    item->action_id = action_id;
    item->flags = flags | MENU_ITEM_ENABLED;

    menu->item_count++;
    return menu->item_count - 1;
}

int menu_add_separator(menu_t *menu) {
    if (!menu || menu->item_count >= MENU_MAX_ITEMS) return -1;

    menu_item_t *item = &menu->items[menu->item_count];
    item->text[0] = '\0';
    item->action_id = MENU_ACTION_NONE;
    item->flags = MENU_ITEM_SEPARATOR;

    menu->item_count++;
    return menu->item_count - 1;
}

void menu_clear(menu_t *menu) {
    if (!menu) return;

    menu_hide(menu);
    menu->item_count = 0;
}

static void menu_calculate_size(menu_t *menu) {
    if (!menu) return;

    int max_width = MENU_MIN_WIDTH;

    /* Calculate width based on longest item text */
    if (font_n.data) {
        for (int i = 0; i < menu->item_count; i++) {
            if (!(menu->items[i].flags & MENU_ITEM_SEPARATOR)) {
                int text_w = bmf_measure_text(&font_n, 12, menu->items[i].text);
                int item_w = text_w + MENU_ITEM_PADDING * 2 + 8;
                if (item_w > max_width) {
                    max_width = item_w;
                }
            }
        }
    }

    menu->w = max_width;

    /* Calculate height based on item count */
    int total_height = 4;  /* Top/bottom padding */
    for (int i = 0; i < menu->item_count; i++) {
        if (menu->items[i].flags & MENU_ITEM_SEPARATOR) {
            total_height += 5;  /* Separator height */
        } else {
            total_height += MENU_ITEM_HEIGHT;
        }
    }
    menu->h = total_height;
}

void menu_show(menu_t *menu, int x, int y) {
    if (!menu || menu->item_count == 0) return;

    /* Restore cursor before saving background to avoid capturing cursor pixels */
    mouse_restore();
    mouse_invalidate_buffer();

    menu_calculate_size(menu);

    /* Adjust position to keep menu on screen */
    x -= 2;
    if (x + menu->w > WM_SCREEN_WIDTH) {
        x = WM_SCREEN_WIDTH - menu->w - 2;
    }
    if (y + menu->h > WM_SCREEN_HEIGHT) {
        y = WM_SCREEN_HEIGHT - menu->h - 2;
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    menu->x = x;
    menu->y = y;
    menu->visible = 1;
    menu->hovered_item = -1;
    menu->just_opened = 1;

    /* Save background */
    int save_w = menu->w;
    int save_h = menu->h;
    int row_bytes = (save_w + 1) / 2;

    if (menu->saved_bg) {
        kfree(menu->saved_bg);
    }
    menu->saved_bg = kmalloc(row_bytes * save_h);
    if (menu->saved_bg) {
        if (menu->x >= 0 && menu->y >= 0 && menu->x + save_w <= WM_SCREEN_WIDTH && menu->y + save_h <= WM_SCREEN_HEIGHT && (menu->x & 1) == 0) {
            gfx_read_screen_region_packed(menu->saved_bg, save_w, save_h, menu->x, menu->y);
        } else {
            for (int py = 0; py < save_h; py++) {
                uint8_t *dst_row = menu->saved_bg + py * row_bytes;
                for (int b = 0; b < row_bytes; b++) dst_row[b] = 0;
                for (int px = 0; px < save_w; px++) {
                    int sx = menu->x + px;
                    int sy = menu->y + py;
                    if (sx >= 0 && sx < WM_SCREEN_WIDTH && sy >= 0 && sy < WM_SCREEN_HEIGHT) {
                        uint8_t pix = gfx_getpixel(sx, sy);
                        int byte_idx = px / 2;
                        if (px & 1) dst_row[byte_idx] = (dst_row[byte_idx] & 0xF0) | (pix & 0x0F);
                        else dst_row[byte_idx] = (dst_row[byte_idx] & 0x0F) | (pix << 4);
                    }
                }
            }
        }
    }

    menu_draw(menu);
}

void menu_hide(menu_t *menu) {
    if (!menu || !menu->visible) return;

    /* Restore background */
    if (menu->saved_bg) {
        int save_w = menu->w;
        int save_h = menu->h;
        int row_bytes = (save_w + 1) / 2;

        if (menu->x >= 0 && menu->y >= 0 && menu->x + save_w <= WM_SCREEN_WIDTH && menu->y + save_h <= WM_SCREEN_HEIGHT && (menu->x & 1) == 0) {
            gfx_write_screen_region_packed(menu->saved_bg, save_w, save_h, menu->x, menu->y);
        } else {
            for (int py = 0; py < save_h; py++) {
                uint8_t *src_row = menu->saved_bg + py * row_bytes;
                for (int px = 0; px < save_w; px++) {
                    int sx = menu->x + px;
                    int sy = menu->y + py;
                    if (sx >= 0 && sx < WM_SCREEN_WIDTH && sy >= 0 && sy < WM_SCREEN_HEIGHT) {
                        int byte_idx = px / 2;
                        uint8_t packed = src_row[byte_idx];
                        uint8_t pix = (px & 1) ? (packed & 0x0F) : (packed >> 4);
                        gfx_putpixel(sx, sy, pix);
                    }
                }
            }
        }

        kfree(menu->saved_bg);
        menu->saved_bg = NULL;
    }
    mouse_invalidate_buffer();
    menu->visible = 0;
    menu->hovered_item = -1;
}

void menu_draw(menu_t *menu) {
    if (!menu || !menu->visible) return;

    /* Invalidate cursor buffer before drawing to prevent artifacts */
    mouse_restore();
    mouse_invalidate_buffer();

    gfx_fillrect(menu->x, menu->y, menu->w, menu->h, MENU_BG_COLOR);
    gfx_rect(menu->x, menu->y, menu->w, menu->h, MENU_BORDER_COLOR);

    /* Draw items */
    int item_y = menu->y + 2;

    for (int i = 0; i < menu->item_count; i++) {
        menu_item_t *item = &menu->items[i];

        if (item->flags & MENU_ITEM_SEPARATOR) {
            /* Draw simple separator line */
            int sep_y = item_y + 2;
            gfx_hline(menu->x + 2, sep_y, menu->w - 4, MENU_BORDER_COLOR);
            item_y += 5;
        } else {
            /* Draw item background if hovered (light gray) */
            if (i == menu->hovered_item && (item->flags & MENU_ITEM_ENABLED)) {
                gfx_fillrect(menu->x + 1, item_y, menu->w - 2, MENU_ITEM_HEIGHT, MENU_HOVER_COLOR);
            }

            if (font_n.data) {
                uint8_t text_color = (item->flags & MENU_ITEM_ENABLED) ?
                                     MENU_TEXT_COLOR : MENU_DISABLED_COLOR;
                bmf_printf(menu->x + MENU_ITEM_PADDING + 2, item_y + 4,
                          &font_n, 12, text_color, "%s", item->text);
            }

            item_y += MENU_ITEM_HEIGHT;
        }
    }
}

int menu_contains_point(menu_t *menu, int mx, int my) {
    if (!menu || !menu->visible) return 0;

    return (mx >= menu->x && mx < menu->x + menu->w &&
            my >= menu->y && my < menu->y + menu->h);
}

static int menu_get_item_at(menu_t *menu, int mx, int my) {
    if (!menu || !menu->visible) return -1;
    if (!menu_contains_point(menu, mx, my)) return -1;

    int item_y = menu->y + 2;

    for (int i = 0; i < menu->item_count; i++) {
        menu_item_t *item = &menu->items[i];
        int item_h = (item->flags & MENU_ITEM_SEPARATOR) ? 5 : MENU_ITEM_HEIGHT;

        if (my >= item_y && my < item_y + item_h) {
            /* Don't return separator items */
            if (item->flags & MENU_ITEM_SEPARATOR) {
                return -1;
            }
            return i;
        }
        item_y += item_h;
    }

    return -1;
}

int menu_handle_mouse(menu_t *menu, int mx, int my, int button_pressed, int button_released) {
    (void)button_pressed;  /* Used by caller for click-outside detection */
    if (!menu || !menu->visible) return 0;

    int old_hover = menu->hovered_item;
    int new_hover = menu_get_item_at(menu, mx, my);

    /* Update hover state */
    if (new_hover != old_hover) {
        menu->hovered_item = new_hover;
        menu_draw(menu);
    }

    /* Handle click */
    if (button_released) {
        /* Ignore the first button release after opening (from the opening click) */
        if (menu->just_opened) {
            menu->just_opened = 0;
            return 0;
        }

        if (new_hover >= 0 && (menu->items[new_hover].flags & MENU_ITEM_ENABLED)) {
            int action = menu->items[new_hover].action_id;
            menu_hide(menu);
            return action;
        } else if (!menu_contains_point(menu, mx, my)) {
            /* Clicked outside menu - close it */
            menu_hide(menu);
            return -1;  /* Indicate menu was closed without selection */
        }
    }

    return 0;  /* No action yet */
}

void menubar_init(menubar_t *menubar) {
    if (!menubar) return;
    menubar->menu_count = 0;
    menubar->active_menu = -1;
    menubar->hovered_menu = -1;
    menubar->visible = 1;
}

void menubar_destroy(menubar_t *menubar) {
    if (!menubar) return;
    for (int i = 0; i < menubar->menu_count; i++) {
        menu_destroy(&menubar->menus[i].menu);
    }
    menubar->menu_count = 0;
    menubar->active_menu = -1;
    menubar->visible = 0;
}

int menubar_add_menu(menubar_t *menubar, const char *title) {
    if (!menubar || menubar->menu_count >= MENUBAR_MAX_MENUS) return -1;
    
    menubar_entry_t *entry = &menubar->menus[menubar->menu_count];
    strcpy_s(entry->title, title, 16);
    menu_init(&entry->menu);
    
    if (font_n.data) {
        entry->title_width = bmf_measure_text(&font_n, 12, title) + 16;
    } else {
        entry->title_width = strlen_s(title) * 8 + 16;
    }
    
    menubar->menu_count++;
    return menubar->menu_count - 1;
}

menu_t* menubar_get_menu(menubar_t *menubar, int index) {
    if (!menubar || index < 0 || index >= menubar->menu_count) return NULL;
    return &menubar->menus[index].menu;
}

void menubar_draw(menubar_t *menubar, int win_x, int win_y, int win_w) {
    if (!menubar || !menubar->visible) return;
    
    window_theme_t *theme = theme_get_current();
    int bar_y = win_y + WM_TITLEBAR_HEIGHT + 2;
    
    mouse_restore();
    mouse_invalidate_buffer();
    
    gfx_fillrect(win_x + 2, bar_y, win_w - 4, MENUBAR_HEIGHT, theme->bg_color);
    gfx_hline(win_x + 2, bar_y + MENUBAR_HEIGHT - 1, win_w - 4, theme->frame_dark);
    
    int x_offset = win_x + 7;
    for (int i = 0; i < menubar->menu_count; i++) {
        menubar_entry_t *entry = &menubar->menus[i];
        uint8_t bg_color = theme->bg_color;
        uint8_t text_color = theme->text_color;
        
        if (i == menubar->hovered_menu || i == menubar->active_menu) {
            bg_color = 7;
            gfx_fillrect(x_offset - 4, bar_y + 2, entry->title_width - 4, MENUBAR_HEIGHT - 4, bg_color);
        }
        
        if (font_n.data) {
            bmf_printf(x_offset, bar_y + 5, &font_n, 12, text_color, "%s", entry->title);
        }
        
        x_offset += entry->title_width;
    }
    
    if (menubar->active_menu >= 0 && menubar->active_menu < menubar->menu_count) {
        menu_draw(&menubar->menus[menubar->active_menu].menu);
    }
}

int menubar_get_height(menubar_t *menubar) {
    return menubar && menubar->visible ? MENUBAR_HEIGHT : 0;
}

void menubar_close_all(menubar_t *menubar) {
    if (!menubar) return;
    
    if (menubar->active_menu >= 0 && menubar->active_menu < menubar->menu_count) {
        menu_hide(&menubar->menus[menubar->active_menu].menu);
    }
    menubar->active_menu = -1;
    menubar->hovered_menu = -1;
}

static int menubar_get_menu_at(menubar_t *menubar, int win_x, int win_y, int mx, int my) {
    if (!menubar || !menubar->visible) return -1;
    
    int bar_y = win_y + WM_TITLEBAR_HEIGHT + 2;
    if (my < bar_y || my >= bar_y + MENUBAR_HEIGHT) return -1;
    
    int x_offset = win_x + 6;
    for (int i = 0; i < menubar->menu_count; i++) {
        menubar_entry_t *entry = &menubar->menus[i];
        if (mx >= x_offset - 4 && mx < x_offset + entry->title_width - 4) {
            return i;
        }
        x_offset += entry->title_width;
    }
    return -1;
}

int menubar_handle_mouse(menubar_t *menubar, int win_x, int win_y, int win_w, int mx, int my, int button_pressed, int button_released) {
    if (!menubar || !menubar->visible) return 0;
    
    int bar_y = win_y + WM_TITLEBAR_HEIGHT + 2;
    int menu_at = menubar_get_menu_at(menubar, win_x, win_y, mx, my);
    
    if (menubar->active_menu >= 0) {
        menu_t *active = &menubar->menus[menubar->active_menu].menu;
        
        if (active->visible) {
            int result = menu_handle_mouse(active, mx, my, button_pressed, button_released);
            
            if (result != 0) {
                menubar->active_menu = -1;
                menubar->hovered_menu = -1;
                return result;
            }
            
            if (menu_at >= 0 && menu_at != menubar->active_menu && button_pressed) {
                menu_hide(active);
                menubar->active_menu = menu_at;
                
                int x_offset = win_x + 6;
                for (int i = 0; i < menu_at; i++) {
                    x_offset += menubar->menus[i].title_width;
                }
                
                menu_show(&menubar->menus[menu_at].menu, x_offset - 4, bar_y + MENUBAR_HEIGHT);
                return 0;
            }
        }
    }
    
    if (menu_at >= 0) {
        menubar->hovered_menu = menu_at;
        
        if (button_pressed) {
            if (menubar->active_menu == menu_at) {
                menu_hide(&menubar->menus[menu_at].menu);
                menubar->active_menu = -1;
            } else {
                if (menubar->active_menu >= 0) {
                    menu_hide(&menubar->menus[menubar->active_menu].menu);
                }
                menubar->active_menu = menu_at;
                
                int x_offset = win_x + 6;
                for (int i = 0; i < menu_at; i++) {
                    x_offset += menubar->menus[i].title_width;
                }
                
                menu_show(&menubar->menus[menu_at].menu, x_offset - 4, bar_y + MENUBAR_HEIGHT);
            }
        }
    } else if (my < bar_y || my >= bar_y + MENUBAR_HEIGHT) {
        menubar->hovered_menu = -1;
    }
    
    return 0;
}
