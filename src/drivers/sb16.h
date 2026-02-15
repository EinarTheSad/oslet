#pragma once
#include <stdint.h>

void sb16_init(void);
int sb16_detected(void);
void sb16_play_tone(uint16_t frequency, uint32_t duration_ms, uint8_t waveform);
void sb16_stop(void);
void sb16_set_volume(uint8_t left, uint8_t right);
