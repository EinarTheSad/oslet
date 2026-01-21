#include "syscall.h"
#include "task.h"
#include "console.h"
#include "timer.h"
#include "drivers/fat32.h"
#include "drivers/vga.h"
#include "exec.h"
#include "mem/pmm.h"
#include "mem/heap.h"
#include "drivers/graphics.h"
#include "drivers/keyboard.h"
#include "drivers/mouse.h"
#include "fonts/bmf.h"
#include "rtc.h"
#include "win/window.h"
#include "win/wm_config.h"
#include "win/bitmap.h"
#include "win/wm.h"
#include "win/controls.h"
#include "win/menu.h"

#define MAX_OPEN_FILES 32

/* Global window manager */
static window_manager_t global_wm;
static int wm_initialized = 0;

typedef struct {
    fat32_file_t *file;
    int in_use;
    uint32_t owner_tid;  /* Task ID that owns this file descriptor */
} file_descriptor_t;

static file_descriptor_t fd_table[MAX_OPEN_FILES];

static void fd_init(void) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        fd_table[i].file = NULL;
        fd_table[i].in_use = 0;
        fd_table[i].owner_tid = 0;
    }
}

static int fd_alloc(fat32_file_t *file) {
    task_t *current = task_get_current();
    uint32_t tid = current ? current->tid : 0;

    for (int i = 3; i < MAX_OPEN_FILES; i++) {
        if (!fd_table[i].in_use) {
            fd_table[i].file = file;
            fd_table[i].in_use = 1;
            fd_table[i].owner_tid = tid;
            return i;
        }
    }
    return -1;
}

static fat32_file_t* fd_get(int fd) {
    if (fd < 0 || fd >= MAX_OPEN_FILES) return NULL;
    if (!fd_table[fd].in_use) return NULL;

    /* Check ownership - only the owning task can access this fd */
    task_t *current = task_get_current();
    if (current && fd_table[fd].owner_tid != current->tid) {
        return NULL;  /* Access denied - wrong task */
    }

    return fd_table[fd].file;
}

static void fd_free(int fd) {
    if (fd < 0 || fd >= MAX_OPEN_FILES) return;

    /* Check ownership before freeing */
    task_t *current = task_get_current();
    if (current && fd_table[fd].owner_tid != 0 && fd_table[fd].owner_tid != current->tid) {
        return;  /* Access denied - can't close another task's fd */
    }

    fd_table[fd].in_use = 0;
    fd_table[fd].file = NULL;
    fd_table[fd].owner_tid = 0;
}

/* Cleanup all file descriptors owned by a task (called on task exit) */
void fd_cleanup_task(uint32_t tid) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (fd_table[i].in_use && fd_table[i].owner_tid == tid) {
            if (fd_table[i].file) {
                fat32_close(fd_table[i].file);
            }
            fd_table[i].in_use = 0;
            fd_table[i].file = NULL;
            fd_table[i].owner_tid = 0;
        }
    }
}

static uint32_t handle_console(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    (void)edx;
    
    switch (al) {
        case 0x00:
            if (!ebx) return -1;
            {
                const char *s = (const char*)ebx;
                while (*s) putchar(*s++);
            }
            return 0;

        case 0x01:
            if (!ebx) return -1;
            return (uint32_t)kbd_getline((char*)ebx, ecx);

        case 0x02:
            vga_set_color((uint8_t)ebx, (uint8_t)ecx);
            return 0;
        
        case 0x03:
            return (uint32_t)kbd_getchar();
        
        case 0x04:
            vga_clear();
            return 0;

        case 0x05: /* Set cursor */
            if (!ebx) return -1;
            vga_set_cursor((int)ebx, (int)ecx);
            return 0;
        
        case 0x06: /* Get cursor position */
            if (!ebx || !ecx) return -1;
            vga_get_cursor((int*)ebx, (int*)ecx);
            return 0;
            
        default:
            return -1;
    }
}

static uint32_t handle_process(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    (void)ecx; (void)edx;
    
    switch (al) {
        case 0x00:
            task_exit();
            return 0;
            
        case 0x01: {
            if (!ebx) return -1;
            exec_image_t image;
            if (exec_load((const char*)ebx, &image) != 0) return -1;
            if (exec_run(&image) != 0) return -1;
            exec_free(&image);
            return 0;
        }
            
        case 0x02: {
            task_t *current = task_get_current();
            return current ? current->tid : 0;
        }
            
        case 0x03:
            task_sleep(ebx);
            return 0;
            
        case 0x04:
            task_yield();
            return 0;
        
        case 0x05:
            if (!ebx) return -1;
            return task_spawn_and_wait((const char*)ebx);

        case 0x06:  /* SYS_PROC_SPAWN_ASYNC - spawn without waiting */
            if (!ebx) return -1;
            return task_spawn((const char*)ebx);

        default:
            return -1;
    }
}

static uint32_t handle_file(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    switch (al) {
        case 0x00: {
            if (!ebx || !ecx) return (uint32_t)-1;
            
            fat32_file_t *file = fat32_open((const char*)ebx, (const char*)ecx);
            if (!file) return (uint32_t)-1;
            
            int fd = fd_alloc(file);
            if (fd < 0) {
                fat32_close(file);
                return (uint32_t)-1;
            }
            return (uint32_t)fd;
        }
            
        case 0x01: {
            fat32_file_t *file = fd_get((int)ebx);
            if (!file) return (uint32_t)-1;
            
            fat32_close(file);
            fd_free((int)ebx);
            return 0;
        }
            
        case 0x02: {
            if (!ecx) return (uint32_t)-1;
            
            fat32_file_t *file = fd_get((int)ebx);
            if (!file) return (uint32_t)-1;
            
            int result = fat32_read(file, (void*)ecx, (size_t)edx);
            return (uint32_t)result;
        }
            
        case 0x03: {
            if (!ecx) return (uint32_t)-1;
            
            fat32_file_t *file = fd_get((int)ebx);
            if (!file) return (uint32_t)-1;
            
            int result = fat32_write(file, (const void*)ecx, (size_t)edx);
            return (uint32_t)result;
        }
            
        case 0x04: {
            fat32_file_t *file = fd_get((int)ebx);
            if (!file) return (uint32_t)-1;
            
            int result = fat32_seek(file, ecx);
            return (uint32_t)result;
        }
        
        case 0x05: {
            if (!ebx) return (uint32_t)-1;
            int result = fat32_unlink((const char*)ebx);
            return (uint32_t)result;
        }
            
        default:
            return (uint32_t)-1;
    }
}

static uint32_t handle_dir(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    switch (al) {
        case 0x00:
            if (!ebx) return -1;
            return fat32_chdir((const char*)ebx);
            
        case 0x01:
            if (!ebx) return -1;
            char *result = fat32_getcwd((char*)ebx, ecx);
            if (!result || ((char*)ebx)[0] == '\0') {
                if (ecx >= 4) {
                    memcpy_s((void*)ebx, "C:/", 4);
                    return (uint32_t)ebx;
                }
            }
            return (uint32_t)result;
        
        case 0x02:
            if (!ebx) return -1;
            return fat32_mkdir((const char*)ebx);
        
        case 0x03:
            if (!ebx) return -1;
            return fat32_rmdir((const char*)ebx);
        
        case 0x04: {
            if (!ebx || !ecx) return -1;
            if (edx > 256) edx = 256;
            fat32_dirent_t *fat_entries = (fat32_dirent_t*)kmalloc(sizeof(fat32_dirent_t) * edx);
            if (!fat_entries) return -1;
            
            int count = fat32_list_dir((const char*)ebx, fat_entries, (int)edx);
            
            if (count > 0) {
                sys_dirent_t *sys_entries = (sys_dirent_t*)ecx;
                for (int i = 0; i < count; i++) {
                    memcpy_s(sys_entries[i].name, fat_entries[i].name, sizeof(sys_entries[i].name));
                    sys_entries[i].size = fat_entries[i].size;
                    sys_entries[i].first_cluster = fat_entries[i].first_cluster;
                    sys_entries[i].mtime = fat_entries[i].mtime;
                    sys_entries[i].mdate = fat_entries[i].mdate;
                    sys_entries[i].is_directory = fat_entries[i].is_directory;
                    sys_entries[i].attr = fat_entries[i].attr;
                }
            }
            
            kfree(fat_entries);
            return count;
        }
            
        default:
            return -1;
    }
}

static uint32_t handle_ipc(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    switch (al) {
        case 0x00: {
            if (!ecx) return -2;
            if (edx > MSG_MAX_SIZE) return -1;
            
            task_t *sender = task_get_current();
            if (!sender) return -3;
            
            task_t *receiver = task_find_by_tid(ebx);
            if (!receiver) return -4;
            
            msg_queue_t *q = &receiver->msg_queue;
            if (q->count >= MSG_QUEUE_SIZE) return -5;
            
            message_t *msg = &q->msgs[q->head];
            msg->from_tid = sender->tid;
            msg->to_tid = ebx;
            msg->size = edx;
            memcpy_s(msg->data, (const void*)ecx, edx);
            
            q->head = (q->head + 1) % MSG_QUEUE_SIZE;
            q->count++;
            
            if (receiver->state == TASK_BLOCKED) {
                receiver->state = TASK_READY;
            }
            return 0;
        }
            
        case 0x01: {
            if (!ebx) return -1;
            
            task_t *current = task_get_current();
            if (!current) return -2;
            
            msg_queue_t *q = &current->msg_queue;
            if (q->count == 0) {
                current->state = TASK_BLOCKED;
                task_yield();
                if (q->count == 0) return -3;
            }
            
            message_t *msg = &q->msgs[q->tail];
            memcpy_s((void*)ebx, msg, sizeof(message_t));
            
            q->tail = (q->tail + 1) % MSG_QUEUE_SIZE;
            q->count--;
            return 0;
        }
            
        default:
            return -1;
    }
}

static uint32_t handle_time(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    (void)ecx; (void)edx;
    
    switch (al) {
        case 0x00: {
            if (!ebx) return -1;
            
            sys_time_t *time = (sys_time_t*)ebx;
            rtc_read_time(time);
            return 0;
        }
        case 0x01:
            return timer_get_ticks();
        
        default:
            return -1;
    }
}

static uint32_t handle_info(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    (void)edx;
    
    switch (al) {
        case 0x00: {
            if (!ebx) return -1;
            
            sys_meminfo_t *info = (sys_meminfo_t*)ebx;
            size_t total = pmm_total_frames();
            size_t free = pmm_count_free_frames();
            
            info->total_kb = (total * 4096) / 1024;
            info->free_kb = (free * 4096) / 1024;
            info->used_kb = info->total_kb - info->free_kb;
            return 0;
        }
        
        case 0x01: {
            if (!ebx) return -1;
            
            sys_taskinfo_t *tasks = (sys_taskinfo_t*)ebx;
            int max = (int)ecx;
            int count = 0;
            
            task_t *start = task_get_current();
            if (!start) return 0;
            
            task_t *t = start;
            do {
                if (count >= max) break;
                
                tasks[count].tid = t->tid;
                memcpy_s(tasks[count].name, t->name, 32);
                tasks[count].state = (uint8_t)t->state;
                tasks[count].priority = (uint8_t)t->priority;
                count++;
                
                t = t->next;
            } while (t != start && count < max);
            
            return count;
        }

        case 0x02:
            return (uint32_t)(uintptr_t)kernel_version;

        case 0x03: {
            if (!ebx) return -1;
            sys_heapinfo_t *info = (sys_heapinfo_t*)ebx;
            
            size_t free_blocks = 0;
            size_t used_blocks = 0;
            size_t free_mem = 0;
            size_t used_mem = 0;
            
            block_t *curr = heap_start;
            while (curr) {
                if (curr->free) {
                    free_blocks++;
                    free_mem += curr->size;
                } else {
                    used_blocks++;
                    used_mem += curr->size;
                }
                curr = curr->next;
            }
            
            info->total_kb = (heap_end - HEAP_START) / 1024;
            info->used_bytes = used_mem;
            info->free_bytes = free_mem;
            info->used_blocks = used_blocks;
            info->free_blocks = free_blocks;
            info->total_allocated = total_allocated;
            info->total_freed = total_freed;
            
            return 0;
        }

        case 0x04:
            return (uint32_t)(uintptr_t)shell_version;

        case 0x05:
            shell_version = (const char*)ebx;
            return 0;
        
        default:
            return -1;
    }
}

static uint32_t handle_memory(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    (void)ecx; (void)edx;
    
    switch (al) {
        case 0x00: {
            void *ptr = kmalloc((size_t)ebx);
            return (uint32_t)ptr;
        }
            
        case 0x01:
            kfree((void*)ebx);
            return 0;
            
        default:
            return (uint32_t)-1;
    }
}

static uint32_t handle_graphics(uint32_t al, uint32_t ebx, 
                                uint32_t ecx, uint32_t edx) {
    switch (al) {
        case 0x00:
            gfx_enter_mode();
            return 0;
            
        case 0x01:
            gfx_exit_mode();
            return 0;
            
        case 0x02:
            gfx_clear((uint8_t)ebx);
            return 0;
            
        case 0x03:
            gfx_swap_buffers();
            return 0;
            
        case 0x04:
            gfx_putpixel((int)ebx, (int)ecx, (uint8_t)edx);
            return 0;
            
        case 0x05: { /* Line */
            int x0 = (int)(ebx >> 16);
            int y0 = (int)(ebx & 0xFFFF);
            int x1 = (int)(ecx >> 16);
            int y1 = (int)(ecx & 0xFFFF);
            gfx_line(x0, y0, x1, y1, (uint8_t)edx);
            return 0;
        }
            
        case 0x06: { /* Rect */
            int x = (int)(ebx >> 16);
            int y = (int)(ebx & 0xFFFF);
            int w = (int)(ecx >> 16);
            int h = (int)(ecx & 0xFFFF);
            gfx_rect(x, y, w, h, (uint8_t)edx);
            return 0;
        }
            
        case 0x07: { /* Fillrect */
            int x = (int)(ebx >> 16);
            int y = (int)(ebx & 0xFFFF);
            int w = (int)(ecx >> 16);
            int h = (int)(ecx & 0xFFFF);
            gfx_fillrect(x, y, w, h, (uint8_t)edx);
            return 0;
        }
            
        case 0x08: {
            gfx_circle((int)ebx, (int)ecx, (int)(edx >> 8), (uint8_t)(edx & 0xFF));
            return 0;
        }
            
        case 0x09: {
            /* free */
            return 0;
        }
            
        case 0x0A:
            if (!ebx) return (uint32_t)-1;
            return gfx_load_bmp_4bit((const char*)ebx, (int)ecx, (int)edx);

        case 0x0B: { /* Fillrect gradient */
            int x = (int)(ebx >> 16);
            int y = (int)(ebx & 0xFFFF);
            int w = (int)(ecx >> 16);
            int h = (int)(ecx & 0xFFFF);
            uint8_t c_start = (edx >> 16) & 0xFF;
            uint8_t c_end = (edx >> 8) & 0xFF;
            int orientation = edx & 0xFF;
            
            gfx_fillrect_gradient(x, y, w, h, c_start, c_end, orientation);
            return 0;
        }
            
        default:
            return (uint32_t)-1;
    }
}

static uint32_t handle_mouse(uint32_t al, uint32_t ebx, 
                                uint32_t ecx, uint32_t edx) {
    switch (al) {
        case 0x00: {
            if (!ebx || !ecx || !edx) return (uint32_t)-1;
            *(int*)ebx = mouse_get_x();
            *(int*)ecx = mouse_get_y();
            *(uint8_t*)edx = mouse_get_buttons();
            return 0;
        }
        case 0x01: {
            extern int buffer_valid;
            if (!edx && buffer_valid) {
                mouse_restore();
            }
            
            mouse_save(ebx, ecx);
            mouse_draw_cursor(ebx, ecx);
            return 0;
        }
        
        default:
            return (uint32_t)-1;
    }
}

/* Helper functions for SYS_WIN_PUMP_EVENTS */
static void pump_deselect_all_icons(window_manager_t *wm) {
    for (int i = 0; i < wm->count; i++) {
        if (wm->windows[i] && wm->windows[i]->win.is_minimized && wm->windows[i]->win.minimized_icon) {
            icon_set_selected(wm->windows[i]->win.minimized_icon, 0);
        }
    }
}

static int pump_handle_icon_click(gui_form_t *form, int mx, int my) {
    uint32_t current_time = timer_get_ticks();

    if (win_is_icon_clicked(&form->win, mx, my)) {
        if (wm_is_icon_doubleclick(&global_wm, current_time, mx, my)) {
            /* Double-click - restore window */
            /* Release icon slot for reuse before restoring */
            if (form->win.minimized_icon) {
                wm_release_icon_slot(&global_wm,
                                     form->win.minimized_icon->x,
                                     form->win.minimized_icon->y);
            }
            win_restore(&form->win);
            return 2;  /* Window restored */
        } else {
            /* Single click - select this icon, deselect others */
            /* Invalidate cursor buffer to prevent artifacts */
            mouse_invalidate_buffer();

            pump_deselect_all_icons(&global_wm);
            if (form->win.minimized_icon) {
                icon_set_selected(form->win.minimized_icon, 1);
            }
            wm_set_icon_click(&global_wm, current_time, mx, my);
            return 1;  /* Selection changed, needs redraw */
        }
    }
    return 0;
}

static void init_window_menu(gui_form_t *form) {
    if (form->window_menu_initialized) return;

    menu_init(&form->window_menu);
    menu_add_item(&form->window_menu, "Minimise", MENU_ACTION_MINIMIZE, MENU_ITEM_ENABLED);
    menu_add_item(&form->window_menu, "Close", MENU_ACTION_CLOSE, MENU_ITEM_ENABLED);
    form->window_menu_initialized = 1;
}

static int pump_handle_minimize(gui_form_t *form, int mx, int my) {
    if (win_is_minimize_button(&form->win, mx, my)) {
        /* Initialize menu if needed */
        init_window_menu(form);

        /* Calculate menu position - below the minimize button */
        int menu_x = form->win.x + form->win.w - 80;
        int menu_y = form->win.y + WM_TITLEBAR_HEIGHT + 2;

        /* Show the window menu instead of minimizing */
        menu_show(&form->window_menu, menu_x, menu_y);
        return 2;  /* Menu shown, needs redraw but don't minimize */
    }
    return 0;
}

static int pump_handle_titlebar_click(gui_form_t *form, int mx, int my) {
    if (win_is_titlebar(&form->win, mx, my)) {
        form->dragging = 1;
        form->drag_start_x = mx;
        form->drag_start_y = my;
        form->press_control_id = -1;
        return 1;  /* Dragging started */
    }
    return 0;
}

static int pump_handle_control_press(gui_form_t *form, int mx, int my) {
    form->press_control_id = -1;
    int old_focus = form->focused_control_id;
    int clicked_on_focusable = 0;

    if (form->controls) {
        /* Iterate backwards to check top-most (last drawn) controls first */
        for (int i = form->ctrl_count - 1; i >= 0; i--) {
            gui_control_t *ctrl = &form->controls[i];

            /* Skip non-interactive controls */
            if (ctrl->type != CTRL_BUTTON &&
                ctrl->type != CTRL_CHECKBOX &&
                ctrl->type != CTRL_RADIOBUTTON &&
                ctrl->type != CTRL_TEXTBOX &&
                ctrl->type != CTRL_ICON) {
                continue;
            }

            int abs_x = form->win.x + ctrl->x;
            int abs_y = form->win.y + ctrl->y + 20;

            /* For checkbox and radio - hit area includes the control and label */
            int hit_w = ctrl->w;
            int hit_h = ctrl->h;

            /* For checkbox/radio, if w/h are just the box size, expand hit area to include text */
            if (ctrl->type == CTRL_CHECKBOX && ctrl->w == 13) {
                /* Estimate text width - in practice, user should set w to include text */
                hit_w = 100;  /* Generous hit area including label */
            } else if (ctrl->type == CTRL_RADIOBUTTON && ctrl->w == 12) {
                hit_w = 100;  /* Generous hit area including label */
            } else if (ctrl->type == CTRL_ICON) {
                /* Icon hit area: 48x58 (32px icon + 24px label + 2px spacing) */
                hit_w = ctrl->w > 0 ? ctrl->w : 48;
                hit_h = ctrl->h > 0 ? ctrl->h : 58;
            }

            if (mx >= abs_x && mx < abs_x + hit_w &&
                my >= abs_y && my < abs_y + hit_h) {
                form->press_control_id = ctrl->id;

                if (ctrl->type == CTRL_BUTTON) {
                    ctrl->pressed = 1;
                    return 1;  /* needs_redraw */
                }
                /* For checkbox and radio, just record the press */
                else if (ctrl->type == CTRL_CHECKBOX || ctrl->type == CTRL_RADIOBUTTON) {
                    return 0;  /* Press recorded, no visual change yet */
                }
                /* For textbox, set keyboard focus and cursor position */
                else if (ctrl->type == CTRL_TEXTBOX) {
                    form->focused_control_id = ctrl->id;
                    clicked_on_focusable = 1;

                    /* Calculate cursor position from mouse click */
                    extern bmf_font_t font_n;
                    int size = ctrl->font_size > 0 ? ctrl->font_size : 12;
                    int text_area_x = abs_x + 4;
                    int rel_x = mx - text_area_x;

                    int new_pos = textbox_pos_from_x(&font_n, size, ctrl->text,
                                                     ctrl->scroll_offset, rel_x);
                    ctrl->cursor_pos = new_pos;

                    /* Start mouse selection */
                    ctrl->sel_start = new_pos;
                    ctrl->sel_end = new_pos;
                    form->textbox_selecting = 1;

                    return 1;  /* Always redraw to show cursor position */
                }
                /* For icon, just record the press (selection happens on release) */
                else if (ctrl->type == CTRL_ICON) {
                    return 0;  /* Press recorded, no visual change yet */
                }
                break;
            }
        }
    }

    /* If clicked outside any focusable control, clear focus */
    if (!clicked_on_focusable && old_focus != -1) {
        form->focused_control_id = -1;
        return 1;  /* Focus changed, needs redraw */
    }

    /* If no control was pressed, deselect all icons in the form */
    if (form->press_control_id == -1 && form->controls) {
        int deselected = 0;
        for (int i = 0; i < form->ctrl_count; i++) {
            if (form->controls[i].type == CTRL_ICON && form->controls[i].checked) {
                form->controls[i].checked = 0;
                deselected = 1;
            }
        }
        if (deselected) {
            return 1;  /* Icons deselected, needs redraw */
        }
    }

    return 0;
}

/* Helper function to find control by ID */
static gui_control_t* find_control_by_id(gui_form_t *form, int16_t id) {
    if (!form || !form->controls || id < 0) return NULL;
    for (int i = 0; i < form->ctrl_count; i++) {
        if (form->controls[i].id == id) {
            return &form->controls[i];
        }
    }
    return NULL;
}

/* Helper: delete selected text in textbox, return 1 if deleted */
static int textbox_delete_selection(gui_control_t *ctrl) {
    if (ctrl->sel_start < 0 || ctrl->sel_start == ctrl->sel_end) return 0;

    int sel_min = ctrl->sel_start < ctrl->sel_end ? ctrl->sel_start : ctrl->sel_end;
    int sel_max = ctrl->sel_start > ctrl->sel_end ? ctrl->sel_start : ctrl->sel_end;

    int text_len = 0;
    while (ctrl->text[text_len]) text_len++;

    /* Shift text left to remove selected portion */
    int del_count = sel_max - sel_min;
    for (int i = sel_min; i <= text_len - del_count; i++) {
        ctrl->text[i] = ctrl->text[i + del_count];
    }

    ctrl->cursor_pos = sel_min;
    ctrl->sel_start = -1;
    ctrl->sel_end = -1;
    return 1;
}

/* Handle keyboard input for focused textbox */
static int pump_handle_keyboard(gui_form_t *form) {
    if (form->focused_control_id < 0) return 0;

    gui_control_t *ctrl = find_control_by_id(form, form->focused_control_id);
    if (!ctrl || ctrl->type != CTRL_TEXTBOX) return 0;

    int key = kbd_getchar_nonblock();
    if (key == 0) return 0;  /* No key pressed */

    int text_len = 0;
    while (ctrl->text[text_len]) text_len++;

    int max_len = ctrl->max_length > 0 ? ctrl->max_length : 255;
    int needs_redraw = 0;
    int has_selection = (ctrl->sel_start >= 0 && ctrl->sel_start != ctrl->sel_end);

    /* Handle special keys */
    if (key == KEY_LEFT) {
        if (has_selection) {
            /* Move cursor to start of selection, clear selection */
            int sel_min = ctrl->sel_start < ctrl->sel_end ? ctrl->sel_start : ctrl->sel_end;
            ctrl->cursor_pos = sel_min;
            ctrl->sel_start = -1;
            ctrl->sel_end = -1;
        } else if (ctrl->cursor_pos > 0) {
            ctrl->cursor_pos--;
        }
        needs_redraw = 1;
    }
    else if (key == KEY_RIGHT) {
        if (has_selection) {
            /* Move cursor to end of selection, clear selection */
            int sel_max = ctrl->sel_start > ctrl->sel_end ? ctrl->sel_start : ctrl->sel_end;
            ctrl->cursor_pos = sel_max;
            ctrl->sel_start = -1;
            ctrl->sel_end = -1;
        } else if (ctrl->cursor_pos < text_len) {
            ctrl->cursor_pos++;
        }
        needs_redraw = 1;
    }
    else if (key == KEY_HOME) {
        ctrl->cursor_pos = 0;
        ctrl->sel_start = -1;
        ctrl->sel_end = -1;
        needs_redraw = 1;
    }
    else if (key == KEY_END) {
        ctrl->cursor_pos = text_len;
        ctrl->sel_start = -1;
        ctrl->sel_end = -1;
        needs_redraw = 1;
    }
    else if (key == '\b') {  /* Backspace */
        if (has_selection) {
            textbox_delete_selection(ctrl);
            needs_redraw = 1;
        } else if (ctrl->cursor_pos > 0) {
            /* Shift text left from cursor position */
            for (int i = ctrl->cursor_pos - 1; i < text_len; i++) {
                ctrl->text[i] = ctrl->text[i + 1];
            }
            ctrl->cursor_pos--;
            needs_redraw = 1;
        }
    }
    else if (key == KEY_DELETE) {
        if (has_selection) {
            textbox_delete_selection(ctrl);
            needs_redraw = 1;
        } else if (ctrl->cursor_pos < text_len) {
            /* Shift text left from position after cursor */
            for (int i = ctrl->cursor_pos; i < text_len; i++) {
                ctrl->text[i] = ctrl->text[i + 1];
            }
            needs_redraw = 1;
        }
    }
    else if (key == '\t') {
        /* Tab could be used to move focus to next control - for now skip */
    }
    else if (key == '\n' || key == '\r') {
        /* Enter - could signal form submission, for now ignore in single-line textbox */
    }
    else if (key == KEY_ESC) {
        /* Escape - clear selection and focus */
        ctrl->sel_start = -1;
        ctrl->sel_end = -1;
        form->focused_control_id = -1;
        needs_redraw = 1;
    }
    else if (key >= 0x20 && key < 0x80) {
        /* Printable ASCII character */
        /* Delete selection first if any */
        if (has_selection) {
            textbox_delete_selection(ctrl);
            /* Recalculate text length after deletion */
            text_len = 0;
            while (ctrl->text[text_len]) text_len++;
        }

        if (text_len < max_len - 1) {
            /* Shift text right from cursor position */
            for (int i = text_len; i >= ctrl->cursor_pos; i--) {
                ctrl->text[i + 1] = ctrl->text[i];
            }
            ctrl->text[ctrl->cursor_pos] = (char)key;
            ctrl->cursor_pos++;
            needs_redraw = 1;
        }
    }
    /* Ignore other special keys (function keys, alt combinations, etc.) */

    return needs_redraw;
}

static uint32_t handle_window(uint32_t al, uint32_t ebx,
                               uint32_t ecx, uint32_t edx) {
    switch (al) {
        case 0x00: { /* SYS_WIN_MSGBOX - Modal message box */
            const char *msg = (const char*)ebx;
            const char *btn = (const char*)ecx;
            const char *title = (const char*)edx;

            /* Restore any existing cursor first to clean up the screen */
            extern int buffer_valid;
            if (buffer_valid) {
                mouse_restore();
                gfx_swap_buffers();
            }

            /* Create msgbox (allocate dynamically to support multiple concurrent msgboxes) */
            msgbox_t *box = (msgbox_t*)kmalloc(sizeof(msgbox_t));
            if (!box) return -1;

            win_msgbox_create(box, msg, btn, title);
            win_msgbox_draw(box);

            /* Get initial mouse position and draw cursor */
            int mx = mouse_get_x();
            int my = mouse_get_y();

            /* Initial cursor draw (save new background) */
            mouse_save(mx, my);
            mouse_draw_cursor(mx, my);
            gfx_swap_buffers();

            /* Modal event loop */
            int dragging = 0;
            int drag_start_x = 0, drag_start_y = 0;
            uint8_t last_mb = 0;

            while (1) {
                mx = mouse_get_x();
                my = mouse_get_y();
                uint8_t mb = mouse_get_buttons();

                /* Detect button click */
                uint8_t button_pressed = (mb & 1) && !(last_mb & 1);
                uint8_t button_released = !(mb & 1) && (last_mb & 1);

                /* Handle dragging */
                if (button_pressed) {
                    if (win_is_titlebar(&box->base, mx, my)) {
                        dragging = 1;
                        drag_start_x = mx;
                        drag_start_y = my;
                    }
                }

                if (button_released) {
                    /* Check if button was clicked */
                    if (!dragging && win_msgbox_handle_click(box, mx, my)) {
                        /* Button clicked - restore cursor and window background */
                        mouse_restore();
                        win_restore_background(&box->base);
                        gfx_swap_buffers();
                        kfree(box);
                        return 1;
                    }
                    dragging = 0;
                }

                /* Perform dragging */
                if (dragging && (mb & 1)) {
                    int dx = mx - drag_start_x;
                    int dy = my - drag_start_y;

                    if (dx != 0 || dy != 0) {
                        win_move(&box->base, dx, dy);
                        drag_start_x = mx;
                        drag_start_y = my;

                        /* Redraw window and cursor (save new position during drag) */
                        win_msgbox_draw(box);
                        mouse_save(mx, my);
                        mouse_draw_cursor(mx, my);
                        gfx_swap_buffers();
                        continue;
                    }
                }

                last_mb = mb;

                /* Smart cursor update: restore old, save new, draw new */
                if (buffer_valid) {
                    mouse_restore();
                }
                mouse_save(mx, my);
                mouse_draw_cursor(mx, my);
                gfx_swap_buffers();
            }

            return 0;
        }

        case 0x05: { /* SYS_WIN_CREATE_FORM */
            /* Initialize window manager on first use */
            if (!wm_initialized) {
                wm_init(&global_wm);
                wm_initialized = 1;
            }

            gui_form_t *form = (gui_form_t*)kmalloc(sizeof(gui_form_t));
            if (!form) return (uint32_t)NULL;

            int x = (int16_t)(ecx >> 16);
            int y = (int16_t)(ecx & 0xFFFF);
            int w = (int16_t)(edx >> 16);
            int h = (int16_t)(edx & 0xFFFF);

            win_create(&form->win, x, y, w, h, (const char*)ebx);
            form->controls = NULL;
            form->ctrl_count = 0;
            form->clicked_id = -1;
            form->last_mouse_buttons = 0;
            form->press_control_id = -1;
            form->dragging = 0;
            form->drag_start_x = 0;
            form->drag_start_y = 0;
            form->icon_path[0] = '\0';
            form->focused_control_id = -1;  /* No control focused initially */
            form->textbox_selecting = 0;
            form->last_icon_click_time = 0;
            form->last_icon_click_id = -1;
            form->window_menu_initialized = 0;
            /* Initialize menu structure to safe defaults */
            form->window_menu.visible = 0;
            form->window_menu.saved_bg = NULL;
            form->window_menu.item_count = 0;
            form->window_menu.hovered_item = -1;

            /* Register window with window manager */
            if (!wm_register_window(&global_wm, form)) {
                /* Failed to register - cleanup */
                kfree(form);
                return (uint32_t)NULL;
            }

            win_draw(&form->win);

            return (uint32_t)form;
        }
              
        case 0x07: { /* SYS_WIN_PUMP_EVENTS */
            gui_form_t *form = (gui_form_t*)ebx;

            if (!form) return 0;

            /* Reset clicked_id at start of event processing */
            form->clicked_id = -1;

            /* Get current mouse state */
            int mx = mouse_get_x();
            int my = mouse_get_y();
            uint8_t mb = mouse_get_buttons();

            /* Detect button transitions */
            uint8_t button_pressed = (mb & 1) && !(form->last_mouse_buttons & 1);
            uint8_t button_released = !(mb & 1) && (form->last_mouse_buttons & 1);

            /* Update last button state for next call */
            form->last_mouse_buttons = mb;

            int event_count = 0;
            int needs_redraw = 0;  /* Track if visual state changed */

            /* Handle window menu if visible */
            if (form->window_menu.visible) {
                int action = menu_handle_mouse(&form->window_menu, mx, my,
                                               button_pressed, button_released);
                if (action > 0) {
                    /* Menu action selected */
                    if (action == MENU_ACTION_MINIMIZE) {
                        int icon_x, icon_y;
                        wm_get_next_icon_pos(&global_wm, &icon_x, &icon_y);
                        const char *icon_path = form->icon_path[0] ? form->icon_path : NULL;
                        win_minimize(&form->win, icon_x, icon_y, icon_path);
                        return -1;  /* Window minimized */
                    } else if (action == MENU_ACTION_CLOSE) {
                        /* Signal close request - return special value */
                        return -3;  /* Close requested */
                    }
                } else if (action == -1) {
                    /* Menu closed without selection */
                    needs_redraw = 1;
                } else if (button_pressed && !menu_contains_point(&form->window_menu, mx, my)) {
                    /* Click outside menu - close it */
                    menu_hide(&form->window_menu);
                    needs_redraw = 1;
                }
                /* Don't process other events while menu is visible */
                if (form->window_menu.visible || action != 0) {
                    if (needs_redraw) {
                        return (uint32_t)-1;
                    }
                    return 0;
                }
            }

            /* Handle mouse button press */
            if (button_pressed) {
                if (form->win.is_minimized) {
                    /* Check for click on icon (single or double) */
                    int icon_result = pump_handle_icon_click(form, mx, my);
                    if (icon_result == 2) {
                        /* Double-click - window restored */
                        return -1;
                    } else if (icon_result == 1) {
                        /* Single click - selection changed */
                        needs_redraw = 1;
                    } else if (icon_result == 0) {
                        /* Click outside this icon - check if we need to deselect */
                        if (form->win.minimized_icon && form->win.minimized_icon->selected) {
                            /* Check if click was on ANY other icon */
                            int clicked_other_icon = 0;
                            for (int i = 0; i < global_wm.count; i++) {
                                if (global_wm.windows[i] &&
                                    global_wm.windows[i] != form &&
                                    global_wm.windows[i]->win.is_minimized &&
                                    win_is_icon_clicked(&global_wm.windows[i]->win, mx, my)) {
                                    clicked_other_icon = 1;
                                    break;
                                }
                            }
                            /* If no icon was clicked, deselect all */
                            if (!clicked_other_icon) {
                                pump_deselect_all_icons(&global_wm);
                                needs_redraw = 1;
                            }
                        }
                    }
                } else {
                    /* Check if click is within window bounds - bring to front if so */
                    if (mx >= form->win.x && mx < form->win.x + form->win.w &&
                        my >= form->win.y && my < form->win.y + form->win.h) {
                        wm_bring_to_front(&global_wm, form);
                    }

                    /* Check minimize button */
                    int min_result = pump_handle_minimize(form, mx, my);
                    if (min_result == 2) {
                        /* Menu shown */
                        needs_redraw = 1;
                    } else if (min_result == 1) {
                        return -1;  /* Window minimized directly */
                    }
                    /* Check if titlebar was clicked */
                    else if (pump_handle_titlebar_click(form, mx, my)) {
                        /* Dragging started, continue */
                    } else {
                        /* Find which control (if any) was pressed */
                        if (pump_handle_control_press(form, mx, my)) {
                            needs_redraw = 1;
                        }
                    }
                }
            }

            /* Handle mouse button release */
            if (button_released) {
                /* End dragging - signal full redraw needed */
                int was_dragging = form->dragging;
                if (form->dragging) {
                    form->dragging = 0;
                }

                /* Check if release is on the same control that was pressed */
                if (form->press_control_id != -1 && form->controls) {
                    for (int i = 0; i < form->ctrl_count; i++) {
                        gui_control_t *ctrl = &form->controls[i];

                        if (ctrl->id == form->press_control_id) {
                            /* Clear pressed state for buttons */
                            if (ctrl->type == CTRL_BUTTON) {
                                ctrl->pressed = 0;
                                needs_redraw = 1;  /* Button visual changed */
                            }

                            /* Calculate absolute control position */
                            int abs_x = form->win.x + ctrl->x;
                            int abs_y = form->win.y + ctrl->y + 20;

                            /* For checkbox and radio - hit area includes the control and label */
                            int hit_w = ctrl->w;
                            int hit_h = ctrl->h;

                            /* For checkbox/radio, if w/h are just the box size, expand hit area to include text */
                            if (ctrl->type == CTRL_CHECKBOX && ctrl->w == 13) {
                                hit_w = 100;  /* Generous hit area including label */
                            } else if (ctrl->type == CTRL_RADIOBUTTON && ctrl->w == 12) {
                                hit_w = 100;  /* Generous hit area including label */
                            } else if (ctrl->type == CTRL_ICON) {
                                /* Icon hit area: 48x58 (32px icon + 24px label + 2px spacing) */
                                hit_w = ctrl->w > 0 ? ctrl->w : 48;
                                hit_h = ctrl->h > 0 ? ctrl->h : 58;
                            }

                            /* Check if release is within same control */
                            if (mx >= abs_x && mx < abs_x + hit_w &&
                                my >= abs_y && my < abs_y + hit_h) {

                                /* Handle checkbox toggle */
                                if (ctrl->type == CTRL_CHECKBOX) {
                                    ctrl->checked = !ctrl->checked;
                                    needs_redraw = 1;
                                    /* Don't set clicked_id for checkbox - just redraw */
                                }

                                /* Handle radio button selection */
                                else if (ctrl->type == CTRL_RADIOBUTTON) {
                                    /* Uncheck all radio buttons in the same group */
                                    for (int j = 0; j < form->ctrl_count; j++) {
                                        gui_control_t *other = &form->controls[j];
                                        if (other->type == CTRL_RADIOBUTTON &&
                                            other->group_id == ctrl->group_id) {
                                            other->checked = 0;
                                        }
                                    }
                                    /* Check this radio button */
                                    ctrl->checked = 1;
                                    needs_redraw = 1;
                                    /* Don't set clicked_id for radio - just redraw */
                                }

                                /* Handle icon click/double-click */
                                else if (ctrl->type == CTRL_ICON) {
                                    uint32_t now = timer_get_ticks();

                                    /* Deselect all other icons first */
                                    for (int j = 0; j < form->ctrl_count; j++) {
                                        gui_control_t *other = &form->controls[j];
                                        if (other->type == CTRL_ICON && other->id != ctrl->id) {
                                            other->checked = 0;
                                        }
                                    }

                                    /* Check for double-click */
                                    if (form->last_icon_click_id == ctrl->id &&
                                        (now - form->last_icon_click_time) < WM_DOUBLECLICK_MS) {
                                        /* Double-click - activate the icon and deselect it */
                                        ctrl->checked = 0;
                                        form->clicked_id = ctrl->id;
                                        event_count = 1;
                                        form->last_icon_click_id = -1;
                                    } else {
                                        /* Single click - just select */
                                        ctrl->checked = 1;
                                        form->last_icon_click_time = now;
                                        form->last_icon_click_id = ctrl->id;
                                    }
                                    needs_redraw = 1;
                                }

                                /* Valid click detected for buttons and other controls */
                                else {
                                    form->clicked_id = ctrl->id;
                                    event_count = 1;
                                }
                            }
                            break;
                        }
                    }
                }

                /* Clear all pressed states on any button that might still be pressed */
                if (form->controls) {
                    for (int i = 0; i < form->ctrl_count; i++) {
                        if (form->controls[i].type == CTRL_BUTTON && form->controls[i].pressed) {
                            form->controls[i].pressed = 0;
                            needs_redraw = 1;  /* Button visual changed */
                        }
                    }
                }

                /* Clear press state after release */
                form->press_control_id = -1;

                /* If we just finished dragging, signal full redraw needed */
                if (was_dragging) {
                    return (uint32_t)-2;  /* -2 = drag ended, full redraw needed */
                }
            }

            /* Handle active dragging */
            if (form->dragging && (mb & 1)) {
                int dx = mx - form->drag_start_x;
                int dy = my - form->drag_start_y;

                if (dx != 0 || dy != 0) {
                    win_move(&form->win, dx, dy);
                    form->drag_start_x = mx;
                    form->drag_start_y = my;
                    /* During drag, just redraw this window (no full screen redraw) */
                    win_draw(&form->win);
                    needs_redraw = 0;  /* Already drawn */
                }
            }

            /* Handle textbox mouse selection (dragging to select text) */
            if (form->textbox_selecting && (mb & 1) && form->focused_control_id >= 0) {
                gui_control_t *ctrl = find_control_by_id(form, form->focused_control_id);
                if (ctrl && ctrl->type == CTRL_TEXTBOX) {
                    int abs_x = form->win.x + ctrl->x;
                    //int abs_y = form->win.y + ctrl->y + 20;

                    extern bmf_font_t font_n;
                    int size = ctrl->font_size > 0 ? ctrl->font_size : 12;
                    int text_area_x = abs_x + 4;
                    int rel_x = mx - text_area_x;

                    int new_pos = textbox_pos_from_x(&font_n, size, ctrl->text,
                                                     ctrl->scroll_offset, rel_x);

                    /* Update selection end and cursor */
                    if (new_pos != ctrl->sel_end) {
                        ctrl->sel_end = new_pos;
                        ctrl->cursor_pos = new_pos;
                        needs_redraw = 1;
                    }
                }
            }

            /* End textbox selection on mouse release */
            if (button_released && form->textbox_selecting) {
                form->textbox_selecting = 0;
                /* If sel_start == sel_end, clear selection */
                gui_control_t *ctrl = find_control_by_id(form, form->focused_control_id);
                if (ctrl && ctrl->type == CTRL_TEXTBOX) {
                    if (ctrl->sel_start == ctrl->sel_end) {
                        ctrl->sel_start = -1;
                        ctrl->sel_end = -1;
                    }
                }
            }

            /* Handle keyboard input for focused textbox */
            if (!form->win.is_minimized && form->focused_control_id >= 0) {
                if (pump_handle_keyboard(form)) {
                    needs_redraw = 1;
                }
            }

            /* Return clicked control ID directly (0 if none clicked) */
            if (event_count > 0) {
                /* If button was pressed, we need to redraw to show unpressed state */
                if (needs_redraw) {
                    mouse_invalidate_buffer();
                    wm_draw_all(&global_wm);
                }
                return form->clicked_id;
            }

            /* Return -1 if visual state changed and needs redraw */
            if (needs_redraw) {
                mouse_invalidate_buffer();
                return (uint32_t)-1;
            }

            return 0;
        }

        case 0x08: { /* SYS_WIN_ADD_CONTROL */
            gui_form_t *form = (gui_form_t*)ebx;
            gui_control_t *ctrl = (gui_control_t*)ecx;

            if (!form || !ctrl) return 0;

            /* Allocate or expand controls array */
            if (!form->controls) {
                form->controls = (gui_control_t*)kmalloc(sizeof(gui_control_t) * WM_MAX_CONTROLS_PER_FORM);
                if (!form->controls) return 0;
            }

            /* Add control (simple append for now) */
            if (form->ctrl_count < WM_MAX_CONTROLS_PER_FORM) {
                /* Copy the control data */
                gui_control_t *dest = &form->controls[form->ctrl_count];
                dest->type = ctrl->type;
                dest->x = ctrl->x;
                dest->y = ctrl->y;
                dest->w = ctrl->w;
                dest->h = ctrl->h;
                dest->fg = ctrl->fg;
                dest->bg = ctrl->bg;
                dest->id = ctrl->id;
                dest->font_type = ctrl->font_type;
                dest->font_size = ctrl->font_size;
                dest->border = ctrl->border;
                dest->border_color = ctrl->border_color;
                dest->cached_bitmap = NULL;
                dest->pressed = 0;
                dest->checked = ctrl->checked;
                dest->group_id = ctrl->group_id;
                /* Textbox-specific fields */
                dest->cursor_pos = ctrl->cursor_pos;
                dest->max_length = ctrl->max_length > 0 ? ctrl->max_length : 255;
                dest->scroll_offset = 0;
                dest->is_focused = 0;
                dest->sel_start = -1;
                dest->sel_end = -1;

                strcpy_s(dest->text, ctrl->text, 256);

                /* For textbox, set cursor to end of initial text */
                if (dest->type == CTRL_TEXTBOX) {
                    int len = 0;
                    while (dest->text[len]) len++;
                    dest->cursor_pos = len;
                }

                form->ctrl_count++;
            }
            return 0;
        }

        case 0x09: { /* SYS_WIN_DRAW */
            gui_form_t *form = (gui_form_t*)ebx;
            if (!form) return 0;
            win_draw(&form->win);

            /* Don't draw controls if window is minimized */
            if (form->win.is_minimized) return 0;

            /* Draw all controls */
            for (int i = 0; i < form->ctrl_count; i++) {
                /* Set focus state before drawing (used by textbox for cursor) */
                form->controls[i].is_focused =
                    (form->controls[i].id == form->focused_control_id) ? 1 : 0;
                win_draw_control(&form->win, &form->controls[i]);
            }
            return 0;
        }

        case 0x0A: { /* SYS_WIN_DESTROY_FORM */
            gui_form_t *form = (gui_form_t*)ebx;
            if (!form) return 0;

            /* Release icon slot if window was minimized */
            if (form->win.is_minimized && form->win.minimized_icon) {
                wm_release_icon_slot(&global_wm,
                                     form->win.minimized_icon->x,
                                     form->win.minimized_icon->y);
            }

            /* Unregister from window manager */
            wm_unregister_window(&global_wm, form);

            /* Free cached bitmaps in controls */
            if (form->controls) {
                for (int i = 0; i < form->ctrl_count; i++) {
                    if (form->controls[i].cached_bitmap) {
                        bitmap_free(form->controls[i].cached_bitmap);
                        form->controls[i].cached_bitmap = NULL;
                    }
                }
                kfree(form->controls);
            }

            /* Destroy window menu */
            if (form->window_menu_initialized) {
                menu_destroy(&form->window_menu);
            }

            /* Destroy window */
            win_destroy(&form->win);

            /* Free form */
            kfree(form);

            return 0;
        }

        case 0x0B: { /* SYS_WIN_SET_ICON */
            gui_form_t *form = (gui_form_t*)ebx;
            const char *icon_path = (const char*)ecx;

            if (!form || !icon_path) return 0;

            /* Store icon path for later use when minimizing */
            strcpy_s(form->icon_path, icon_path, 64);

            return 0;
        }

        case 0x0C: { /* SYS_WIN_REDRAW_ALL */
            wm_draw_all(&global_wm);
            return 0;
        }

        case 0x0D: { /* SYS_WIN_GET_CONTROL */
            gui_form_t *form = (gui_form_t*)ebx;
            int16_t id = (int16_t)ecx;

            if (!form) return 0;

            for (int i = 0; i < form->ctrl_count; i++) {
                if (form->controls[i].id == id) {
                    return (uint32_t)&form->controls[i];
                }
            }
            return 0;
        }

        case 0x0E: { /* SYS_WIN_CTRL_SET_PROP */
            gui_form_t *form = (gui_form_t*)ebx;
            int16_t ctrl_id = (int16_t)(ecx >> 16);
            int16_t prop_id = (int16_t)(ecx & 0xFFFF);
            uint32_t value = edx;

            if (!form) return 0;

            /* Find control */
            gui_control_t *ctrl = NULL;
            for (int i = 0; i < form->ctrl_count; i++) {
                if (form->controls[i].id == ctrl_id) {
                    ctrl = &form->controls[i];
                    break;
                }
            }
            if (!ctrl) return 0;

            switch (prop_id) {
                case 0: /* PROP_TEXT */
                    if (value) strcpy_s(ctrl->text, (const char*)value, 256);
                    break;
                case 1: /* PROP_CHECKED */
                    ctrl->checked = (uint8_t)value;
                    break;
                case 2: /* PROP_X */
                    ctrl->x = (uint16_t)value;
                    break;
                case 3: /* PROP_Y */
                    ctrl->y = (uint16_t)value;
                    break;
                case 4: /* PROP_W */
                    ctrl->w = (uint16_t)value;
                    break;
                case 5: /* PROP_H */
                    ctrl->h = (uint16_t)value;
                    break;
                case 6: /* PROP_VISIBLE - use high bit of type as flag */
                    if (value)
                        ctrl->type &= 0x7F;  /* Clear hidden bit */
                    else
                        ctrl->type |= 0x80;  /* Set hidden bit */
                    break;
                case 7: /* PROP_FG */
                    ctrl->fg = (uint8_t)value;
                    break;
                case 8: /* PROP_BG */
                    ctrl->bg = (uint8_t)value;
                    break;
                case 9: /* PROP_IMAGE - for picturebox stores path in text, for icon loads bitmap directly */
                    if (value) {
                        if (ctrl->type == CTRL_ICON) {
                            /* For icons, load bitmap directly without overwriting text (label) */
                            if (ctrl->cached_bitmap) {
                                bitmap_free(ctrl->cached_bitmap);
                            }
                            ctrl->cached_bitmap = bitmap_load_from_file((const char*)value);
                        } else {
                            /* For picturebox, store path in text and clear cache */
                            strcpy_s(ctrl->text, (const char*)value, 256);
                            if (ctrl->cached_bitmap) {
                                ctrl->cached_bitmap = NULL;
                            }
                        }
                    }
                    break;
                default:
                    return 0;
            }
            return 1;
        }

        case 0x0F: { /* SYS_WIN_CTRL_GET_PROP */
            gui_form_t *form = (gui_form_t*)ebx;
            int16_t ctrl_id = (int16_t)(ecx >> 16);
            int16_t prop_id = (int16_t)(ecx & 0xFFFF);

            if (!form) return 0;

            /* Find control */
            gui_control_t *ctrl = NULL;
            for (int i = 0; i < form->ctrl_count; i++) {
                if (form->controls[i].id == ctrl_id) {
                    ctrl = &form->controls[i];
                    break;
                }
            }
            if (!ctrl) return 0;

            switch (prop_id) {
                case 0: /* PROP_TEXT */
                    return (uint32_t)ctrl->text;
                case 1: /* PROP_CHECKED */
                    return ctrl->checked;
                case 2: /* PROP_X */
                    return ctrl->x;
                case 3: /* PROP_Y */
                    return ctrl->y;
                case 4: /* PROP_W */
                    return ctrl->w;
                case 5: /* PROP_H */
                    return ctrl->h;
                case 6: /* PROP_VISIBLE */
                    return (ctrl->type & 0x80) ? 0 : 1;
                case 7: /* PROP_FG */
                    return ctrl->fg;
                case 8: /* PROP_BG */
                    return ctrl->bg;
                case 9: /* PROP_IMAGE */
                    return (uint32_t)ctrl->text;
                default:
                    return 0;
            }
        }

        case 0x10: { /* SYS_WIN_INVALIDATE_ICONS */
            wm_invalidate_icon_backgrounds(&global_wm);
            return 0;
        }

        default:
            return (uint32_t)-1;
    }
}

uint32_t syscall_handler(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    uint32_t ah = (eax >> 8) & 0xFF;
    uint32_t al = eax & 0xFF;
    
    switch (ah) {
        case 0x01: return handle_console(al, ebx, ecx, edx);
        case 0x02: return handle_process(al, ebx, ecx, edx);
        case 0x03: return handle_file(al, ebx, ecx, edx);
        case 0x04: return handle_dir(al, ebx, ecx, edx);
        case 0x05: return handle_ipc(al, ebx, ecx, edx);
        case 0x06: return handle_memory(al, ebx, ecx, edx);
        case 0x07: return handle_time(al, ebx, ecx, edx);
        case 0x08: return handle_info(al, ebx, ecx, edx);
        case 0x09: return handle_graphics(al, ebx, ecx, edx);
        case 0x0A: return handle_mouse(al, ebx, ecx, edx);
        case 0x0B: return handle_window(al, ebx, ecx, edx);
        
        default:
            return (uint32_t)-1;
    }
}

void syscall_init(void) {
    fd_init();
}