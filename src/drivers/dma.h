#pragma once
#include <stdint.h>

void dma_init(void);
void dma_setup_channel(uint8_t channel, uint32_t phys_addr, uint16_t length, uint8_t mode);
void dma_start_channel(uint8_t channel);
void dma_stop_channel(uint8_t channel);
