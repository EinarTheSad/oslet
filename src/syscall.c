#include "syscall.h"
#include "task.h"
#include "console.h"
#include "timer.h"
#include "drivers/fat32.h"
#include "drivers/vga.h"
#include "exec.h"
#include "mem/pmm.h"
#include "mem/heap.h"
#include <stddef.h>
#include "drivers/graphics.h"
#include "drivers/keyboard.h"
#include "drivers/mouse.h"
#include "fonts/bmf.h"
#include "rtc.h"
#include "win/window.h"

#define MAX_OPEN_FILES 32
typedef struct {
    fat32_file_t *file;
    int in_use;
} file_descriptor_t;

static file_descriptor_t fd_table[MAX_OPEN_FILES];

static void fd_init(void) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        fd_table[i].file = NULL;
        fd_table[i].in_use = 0;
    }
}

static int fd_alloc(fat32_file_t *file) {
    for (int i = 3; i < MAX_OPEN_FILES; i++) {
        if (!fd_table[i].in_use) {
            fd_table[i].file = file;
            fd_table[i].in_use = 1;
            return i;
        }
    }
    return -1;
}

static fat32_file_t* fd_get(int fd) {
    if (fd < 0 || fd >= MAX_OPEN_FILES) return NULL;
    if (!fd_table[fd].in_use) return NULL;
    return fd_table[fd].file;
}

static void fd_free(int fd) {
    if (fd < 0 || fd >= MAX_OPEN_FILES) return;
    fd_table[fd].in_use = 0;
    fd_table[fd].file = NULL;
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
            int full_redraw = (edx >> 8) & 0xFF;
            uint8_t color = edx & 0xFF;
            extern int buffer_valid;
            if (!full_redraw && buffer_valid) {
                mouse_restore();
            }
            
            mouse_save(ebx, ecx);
            mouse_draw_cursor(ebx, ecx, color);
            return 0;
        }
        
        default:
            return (uint32_t)-1;
    }
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

            /* Create msgbox */
            static msgbox_t box;
            win_msgbox_create(&box, msg, btn, title);
            win_msgbox_draw(&box);

            /* Get initial mouse position and draw cursor */
            int mx = mouse_get_x();
            int my = mouse_get_y();

            /* Initial cursor draw (save new background) */
            mouse_save(mx, my);
            mouse_draw_cursor(mx, my, 15);
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
                    if (win_is_titlebar(&box.base, mx, my)) {
                        dragging = 1;
                        drag_start_x = mx;
                        drag_start_y = my;
                    }
                }

                if (button_released) {
                    /* Check if button was clicked */
                    if (!dragging && win_msgbox_handle_click(&box, mx, my)) {
                        /* Button clicked - restore cursor and window background */
                        mouse_restore();
                        win_restore_background(&box.base);
                        gfx_swap_buffers();
                        return 1;
                    }
                    dragging = 0;
                }

                /* Perform dragging */
                if (dragging && (mb & 1)) {
                    int dx = mx - drag_start_x;
                    int dy = my - drag_start_y;

                    if (dx != 0 || dy != 0) {
                        win_move(&box.base, dx, dy);
                        drag_start_x = mx;
                        drag_start_y = my;

                        /* Redraw window and cursor (save new position during drag) */
                        win_msgbox_draw(&box);
                        mouse_save(mx, my);
                        mouse_draw_cursor(mx, my, 15);
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
                mouse_draw_cursor(mx, my, 15);
                gfx_swap_buffers();
            }

            return 0;
        }

        case 0x05: { /* SYS_WIN_CREATE_FORM */
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

            /* Handle mouse button press */
            if (button_pressed) {
                /* Check if titlebar was clicked */
                if (win_is_titlebar(&form->win, mx, my)) {
                    form->dragging = 1;
                    form->drag_start_x = mx;
                    form->drag_start_y = my;
                    form->press_control_id = -1;  /* Not pressing a control */
                } else {
                    /* Find which control (if any) was pressed */
                    form->press_control_id = -1;

                    if (form->controls) {
                        for (int i = 0; i < form->ctrl_count; i++) {
                            gui_control_t *ctrl = &form->controls[i];

                            /* Calculate absolute control position */
                            int abs_x = form->win.x + ctrl->x;
                            int abs_y = form->win.y + ctrl->y + 20;

                            /* Check if mouse is within control bounds */
                            if (mx >= abs_x && mx < abs_x + ctrl->w &&
                                my >= abs_y && my < abs_y + ctrl->h) {
                                form->press_control_id = ctrl->id;
                                break;
                            }
                        }
                    }
                }
            }

            /* Handle mouse button release */
            if (button_released) {
                /* End dragging */
                if (form->dragging) {
                    form->dragging = 0;
                }

                /* Check if release is on the same control that was pressed */
                if (form->press_control_id != -1 && form->controls) {
                    for (int i = 0; i < form->ctrl_count; i++) {
                        gui_control_t *ctrl = &form->controls[i];

                        if (ctrl->id == form->press_control_id) {
                            /* Calculate absolute control position */
                            int abs_x = form->win.x + ctrl->x;
                            int abs_y = form->win.y + ctrl->y + 20;

                            /* Check if release is within same control */
                            if (mx >= abs_x && mx < abs_x + ctrl->w &&
                                my >= abs_y && my < abs_y + ctrl->h) {
                                /* Valid click detected */
                                form->clicked_id = ctrl->id;
                                event_count = 1;
                            }
                            break;
                        }
                    }
                }

                /* Clear press state after release */
                form->press_control_id = -1;
            }

            /* Handle active dragging */
            if (form->dragging && (mb & 1)) {
                int dx = mx - form->drag_start_x;
                int dy = my - form->drag_start_y;

                if (dx != 0 || dy != 0) {
                    win_move(&form->win, dx, dy);
                    form->drag_start_x = mx;
                    form->drag_start_y = my;
                    /* Return -1 to indicate window was moved (needs redraw) */
                    return (uint32_t)-1;
                }
            }

            /* Return clicked control ID directly (0 if none clicked) */
            if (event_count > 0) {
                return form->clicked_id;
            }

            return 0;
        }

        case 0x08: { /* SYS_WIN_ADD_CONTROL */
            gui_form_t *form = (gui_form_t*)ebx;
            gui_control_t *ctrl = (gui_control_t*)ecx;

            if (!form || !ctrl) return 0;

            /* Allocate or expand controls array */
            if (!form->controls) {
                form->controls = (gui_control_t*)kmalloc(sizeof(gui_control_t) * 16);
                if (!form->controls) return 0;
            }

            /* Add control (simple append for now) */
            if (form->ctrl_count < 16) {
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
                dest->bmp_width = 0;
                dest->bmp_height = 0;

                /* Copy text */
                int i = 0;
                while (ctrl->text[i] && i < 255) {
                    dest->text[i] = ctrl->text[i];
                    i++;
                }
                dest->text[i] = '\0';

                form->ctrl_count++;
            }
            return 0;
        }

        case 0x09: { /* SYS_WIN_DRAW */
            gui_form_t *form = (gui_form_t*)ebx;
            if (!form) return 0;
            win_draw(&form->win);

            /* Draw all controls */
            for (int i = 0; i < form->ctrl_count; i++) {
                win_draw_control(&form->win, &form->controls[i]);
            }
            return 0;
        }

        case 0x0A: { /* SYS_WIN_DESTROY_FORM */
            gui_form_t *form = (gui_form_t*)ebx;
            if (!form) return 0;

            /* Free cached bitmaps in controls */
            if (form->controls) {
                for (int i = 0; i < form->ctrl_count; i++) {
                    if (form->controls[i].cached_bitmap) {
                        kfree(form->controls[i].cached_bitmap);
                        form->controls[i].cached_bitmap = NULL;
                    }
                }
                kfree(form->controls);
            }

            /* Destroy window */
            win_destroy(&form->win);

            /* Free form */
            kfree(form);

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