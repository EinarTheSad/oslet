#pragma once
#include <stdint.h>

void sb16_init(void);
int sb16_detected(void);
void sb16_stop(void);
void sb16_set_volume(uint8_t left, uint8_t right);
uint16_t sb16_get_volume(void); /* returns (left<<8) | right */

void sb16_acquire(void);
void sb16_release(void);

typedef void (*sb16_irq_callback_t)(void);
void sb16_set_irq_callback(sb16_irq_callback_t callback);
