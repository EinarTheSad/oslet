#pragma once
#include <stdint.h>

void sb16_init(void);
int sb16_detected(void);
void sb16_play_tone(uint16_t frequency, uint32_t duration_ms, uint8_t waveform);
void sb16_stop(void);
void sb16_set_volume(uint8_t left, uint8_t right);
uint16_t sb16_get_volume(void); /* returns (left<<8) | right */

void sb16_acquire(void);
void sb16_release(void);

/* IRQ completion helpers for sound.c */
void sb16_clear_irq_flag(void);
int sb16_check_irq_flag(void);
