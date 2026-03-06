#pragma once
#include <stdint.h>

void timer_init(uint32_t frequency);
uint32_t timer_get_ticks(void);
uint32_t timer_get_frequency(void);
void timer_wait(uint32_t ticks);
void timer_enable_scheduling(void);