#pragma once
#include <stddef.h>

#define DISPLAY_SWITCH_FAILED       (-1)
#define DISPLAY_SWITCH_RESTORE_FAILED (-2)
#define DISPLAY_SWITCH_EXPIRED      (-3)

int display_switch_try(size_t index);
int display_switch_confirm(void);
int display_switch_revert(void);
int display_switch_poll(void);
