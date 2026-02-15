#include "sb16.h"
#include "dma.h"
#include "../irq/io.h"
#include "../irq/irq.h"
#include "../timer.h"

/* Waveform types */
#define WAVE_SQUARE    0
#define WAVE_TRIANGLE  1
#define WAVE_SINE      2
#define WAVE_SAWTOOTH  3

#define SB_DSP_RESET     0x226
#define SB_DSP_READ      0x22A
#define SB_DSP_WRITE     0x22C
#define SB_DSP_READ_STATUS 0x22E
#define SB_DSP_INT_ACK   0x22F

#define SB_MIXER_ADDR    0x224
#define SB_MIXER_DATA    0x225

#define DSP_CMD_SET_TIME_CONSTANT  0x40
#define DSP_CMD_SET_SAMPLE_RATE    0x41
#define DSP_CMD_SET_OUTPUT_RATE    0x41
#define DSP_CMD_SET_INPUT_RATE     0x42
#define DSP_CMD_DMA_8BIT_DAC       0xC0
#define DSP_CMD_DMA_8BIT_SINGLE    0x14
#define DSP_CMD_DMA_8BIT_AUTO      0x1C
#define DSP_CMD_SPEAKER_ON         0xD1
#define DSP_CMD_SPEAKER_OFF        0xD3
#define DSP_CMD_GET_VERSION        0xE1
#define DSP_CMD_PAUSE_DMA          0xD0
#define DSP_CMD_EXIT_AUTO_8BIT     0xDA

#define DMA_BUFFER_SIZE 4096
#define DMA_CHANNEL 1

static int sb16_present = 0;
static uint8_t dsp_major = 0;
static uint8_t dsp_minor = 0;
static uint8_t dma_buffer[DMA_BUFFER_SIZE] __attribute__((aligned(4096)));
static uint32_t dma_phys = 0;
static volatile int dma_complete = 0;

/* Sine wave lookup table (64 samples for one period) */
static const int8_t sine_table[64] = {
    0, 6, 12, 18, 24, 30, 36, 41, 45, 49, 53, 56, 59, 61, 63, 64,
    64, 64, 63, 61, 59, 56, 53, 49, 45, 41, 36, 30, 24, 18, 12, 6,
    0, -6, -12, -18, -24, -30, -36, -41, -45, -49, -53, -56, -59, -61, -63, -64,
    -64, -64, -63, -61, -59, -56, -53, -49, -45, -41, -36, -30, -24, -18, -12, -6
};

static int dsp_reset(void) {
    outb(SB_DSP_RESET, 1);
    for (volatile int i = 0; i < 1000; i++);
    outb(SB_DSP_RESET, 0);
    
    for (int timeout = 1000; timeout > 0; timeout--) {
        if (inb(SB_DSP_READ_STATUS) & 0x80) {
            if (inb(SB_DSP_READ) == 0xAA) {
                return 0;
            }
        }
        for (volatile int i = 0; i < 100; i++);
    }
    return -1;
}

static int dsp_write(uint8_t value) {
    for (int timeout = 10000; timeout > 0; timeout--) {
        if (!(inb(SB_DSP_WRITE) & 0x80)) {
            outb(SB_DSP_WRITE, value);
            return 0;
        }
    }
    return -1;
}

static int dsp_read(void) {
    for (int timeout = 10000; timeout > 0; timeout--) {
        if (inb(SB_DSP_READ_STATUS) & 0x80) {
            return inb(SB_DSP_READ);
        }
    }
    return -1;
}

static void mixer_write(uint8_t reg, uint8_t value) {
    outb(SB_MIXER_ADDR, reg);
    for (volatile int i = 0; i < 10; i++);
    outb(SB_MIXER_DATA, value);
}

static void sb16_irq_handler(void) {
    inb(SB_DSP_INT_ACK);
    dma_complete = 1;
}

void sb16_init(void) {
    sb16_present = 0;
    
    if (dsp_reset() != 0) {
        return;
    }
    
    if (dsp_write(DSP_CMD_GET_VERSION) != 0) {
        return;
    }
    
    int major = dsp_read();
    int minor = dsp_read();
    
    if (major < 0 || minor < 0) {
        return;
    }
    
    dsp_major = (uint8_t)major;
    dsp_minor = (uint8_t)minor;
    
    if (dsp_major < 4) {
        return;
    }
    
    dma_phys = (uint32_t)(uintptr_t)dma_buffer;
    if (dma_phys >= 0x1000000) {
        sb16_present = 0;
        return;
    }
    
    dma_init();
    irq_install_handler(5, sb16_irq_handler);
    
    mixer_write(0x04, 0xEE);
    mixer_write(0x22, 0xEE);
    
    sb16_present = 1;
}

int sb16_detected(void) {
    return sb16_present;
}

void sb16_set_volume(uint8_t left, uint8_t right) {
    if (!sb16_present) return;
    
    uint8_t vol = ((left & 0x0F) << 4) | (right & 0x0F);
    mixer_write(0x04, vol);
    mixer_write(0x22, vol);
}

void sb16_play_tone(uint16_t frequency, uint32_t duration_ms, uint8_t waveform) {
    if (!sb16_present || dma_phys == 0) return;
    
    if (dma_phys >= 0x1000000) {
        return;
    }
    
    uint32_t sample_rate = 44100;
    uint32_t total_samples = (sample_rate * duration_ms) / 1000;
    if (total_samples > DMA_BUFFER_SIZE) {
        total_samples = DMA_BUFFER_SIZE;
    }
    if (total_samples < 1) total_samples = 1;
    
    /* Fill buffer with waveform or silence */
    if (frequency == 0) {
        /* Silence */
        for (uint32_t i = 0; i < total_samples; i++) {
            dma_buffer[i] = 128;
        }
    } else {
        uint32_t samples_per_period = sample_rate / frequency;
        if (samples_per_period < 4) samples_per_period = 4;
        
        for (uint32_t i = 0; i < total_samples; i++) {
            uint32_t phase = i % samples_per_period;
            int value = 128;
            
            switch (waveform) {
                case WAVE_SQUARE:
                    value = (phase < samples_per_period / 2) ? 158 : 98;
                    break;
                    
                case WAVE_TRIANGLE: {
                    int amplitude;
                    if (phase < samples_per_period / 2) {
                        amplitude = (phase * 60) / (samples_per_period / 2);
                    } else {
                        amplitude = 60 - ((phase - samples_per_period / 2) * 60) / (samples_per_period / 2);
                    }
                    value = 128 + amplitude - 30;
                    break;
                }
                    
                case WAVE_SAWTOOTH: {
                    int amplitude = (phase * 60) / samples_per_period;
                    value = 128 + amplitude - 30;
                    break;
                }
                    
                case WAVE_SINE: {
                    /* Use lookup table with linear interpolation */
                    uint32_t table_index = (phase * 64) / samples_per_period;
                    uint32_t next_index = (table_index + 1) % 64;
                    uint32_t frac = ((phase * 64) % samples_per_period) * 256 / samples_per_period;
                    
                    /* Linear interpolation between table entries */
                    int sine_val = sine_table[table_index] + 
                                   ((sine_table[next_index] - sine_table[table_index]) * (int)frac) / 256;
                    
                    value = 128 + (sine_val * 30) / 64;
                    break;
                }
            }
            
            dma_buffer[i] = (uint8_t)value;
        }
    }
    
    dsp_write(DSP_CMD_SPEAKER_OFF);
    dma_stop_channel(DMA_CHANNEL);
    
    /* Single-cycle DMA mode */
    dma_setup_channel(DMA_CHANNEL, dma_phys, total_samples, 0x48);
    
    uint8_t time_constant = (uint8_t)(256 - (1000000 / sample_rate));
    dsp_write(DSP_CMD_SET_TIME_CONSTANT);
    dsp_write(time_constant);
    
    dsp_write(DSP_CMD_SPEAKER_ON);
    dma_start_channel(DMA_CHANNEL);
    
    /* Single-cycle playback command */
    dsp_write(DSP_CMD_DMA_8BIT_SINGLE);
    uint16_t block_size = total_samples - 1;
    dsp_write(block_size & 0xFF);
    dsp_write((block_size >> 8) & 0xFF);
    
    /* Wait for playback using timer */
    uint32_t wait_ticks = (duration_ms + 9) / 10;
    if (wait_ticks < 1) wait_ticks = 1;
    timer_wait(wait_ticks);
    
    dsp_write(DSP_CMD_SPEAKER_OFF);
    dma_stop_channel(DMA_CHANNEL);
}

void sb16_stop(void) {
    if (!sb16_present) return;
    
    dsp_write(DSP_CMD_PAUSE_DMA);
    dsp_write(DSP_CMD_EXIT_AUTO_8BIT);
    dma_stop_channel(DMA_CHANNEL);
    dsp_write(DSP_CMD_SPEAKER_OFF);
}
