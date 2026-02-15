#include "dma.h"
#include "../irq/io.h"

#define DMA_MODE_READ   0x48
#define DMA_MODE_WRITE  0x44
#define DMA_MODE_AUTO   0x58

static const uint8_t dma_port_addr[8] = {0x00, 0x02, 0x04, 0x06, 0xC0, 0xC4, 0xC8, 0xCC};
static const uint8_t dma_port_count[8] = {0x01, 0x03, 0x05, 0x07, 0xC2, 0xC6, 0xCA, 0xCE};
static const uint8_t dma_port_page[8] = {0x87, 0x83, 0x81, 0x82, 0x8F, 0x8B, 0x89, 0x8A};
static const uint8_t dma_port_mask = 0x0A;
static const uint8_t dma_port_mode = 0x0B;
static const uint8_t dma_port_clear = 0x0C;

void dma_init(void) {
    outb(dma_port_clear, 0);
}

void dma_setup_channel(uint8_t channel, uint32_t phys_addr, uint16_t length, uint8_t mode) {
    if (channel >= 8 || channel == 4) return;
    
    uint8_t page = (phys_addr >> 16) & 0xFF;
    uint16_t offset = phys_addr & 0xFFFF;
    uint16_t count = length - 1;
    
    outb(dma_port_mask, 0x04 | (channel & 3));
    outb(dma_port_clear, 0);
    outb(dma_port_mode, mode | (channel & 3));
    outb(dma_port_addr[channel], offset & 0xFF);
    outb(dma_port_addr[channel], (offset >> 8) & 0xFF);
    outb(dma_port_page[channel], page);
    outb(dma_port_count[channel], count & 0xFF);
    outb(dma_port_count[channel], (count >> 8) & 0xFF);
}

void dma_start_channel(uint8_t channel) {
    if (channel >= 8 || channel == 4) return;
    outb(dma_port_mask, channel & 3);
}

void dma_stop_channel(uint8_t channel) {
    if (channel >= 8 || channel == 4) return;
    outb(dma_port_mask, 0x04 | (channel & 3));
}
