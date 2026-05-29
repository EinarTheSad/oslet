#pragma once

#include "../syscall.h"
#include "../task/task.h"
#include "../console.h"
#include "../task/timer.h"
#include "../drivers/fat32.h"
#include "../drivers/vga.h"
#include "../task/exec.h"
#include "../mem/pmm.h"
#include "../mem/heap.h"
#include "../mem/paging.h"
#include "../drivers/graphics.h"
#include "../drivers/keyboard.h"
#include "../drivers/mouse.h"
#include "../drivers/sb16.h"
#include "../drivers/sound.h"
#include "../fonts/bmf.h"
#include "../rtc.h"
#include "../win/window.h"
#include "../win/wm_config.h"
#include "../win/icon.h"
#include "../win/bitmap.h"
#include "../win/wm.h"
#include "../win/controls.h"
#include "../win/menu.h"
#include "../win/compositor.h"
#include "../irq/io.h"
#include "../win/theme.h"
#include "../vconsole.h"

extern int buffer_valid;

uint32_t sys_irq_save(void);
void sys_irq_restore(uint32_t eflags);
int sys_range_mapped(uint32_t addr, size_t size);
int sys_copy_string(char *dst, uint32_t src, size_t dst_size);

uint32_t handle_window(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx);
int current_task_owns_focused_window(void);
