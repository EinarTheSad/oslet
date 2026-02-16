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

/* Remember last set mixer volumes (4-bit values 0..15). Default to 15 (max) */
static uint8_t current_left_volume = 15;
static uint8_t current_right_volume = 15;

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
    for (int timeout = 1000; timeout > 0; timeout--) {
        if (!(inb(SB_DSP_WRITE) & 0x80)) {
            outb(SB_DSP_WRITE, value);
            return 0;
        }
        if (timeout & 0x0F) continue;
        for (volatile int i = 0; i < 5; i++);
    }
    return -1;
}

static int dsp_read(void) {
    for (int timeout = 1000; timeout > 0; timeout--) {
        if (inb(SB_DSP_READ_STATUS) & 0x80) {
            return inb(SB_DSP_READ);
        }
        if (timeout & 0x0F) continue;
        for (volatile int i = 0; i < 5; i++);
    }
    return -1;
}

static void mixer_write(uint8_t reg, uint8_t value) {
    outb(SB_MIXER_ADDR, reg);
    for (volatile int i = 0; i < 50; i++);
    outb(SB_MIXER_DATA, value);
    for (volatile int i = 0; i < 50; i++);
}

static uint8_t mixer_read(uint8_t reg) {
    outb(SB_MIXER_ADDR, reg);
    for (volatile int i = 0; i < 50; i++);
    return inb(SB_MIXER_DATA);
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

    /* Reset mixer to defaults */
    mixer_write(0x00, 0x00);
    
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
    
    sb16_present = 1;

    /* Set Voice (PCM) to MAX once. From now on we only vary Master volume.
       This prevents the volume from dropping off too quickly (cascading attenuation). */
    mixer_write(0x04, 0xFF);         /* Legacy SB Pro Voice volume (max) */
    mixer_write(0x32, 0x1F << 3);    /* Native SB16 Voice volume L (max) */
    mixer_write(0x33, 0x1F << 3);    /* Native SB16 Voice volume R (max) */
    
    /* Set initial Master volume to max (5-bit range 0..31) */
    sb16_set_volume(31, 31);
}

int sb16_detected(void) {
    return sb16_present;
}

void sb16_set_volume(uint8_t left, uint8_t right) {
    if (!sb16_present) return;
    
    /* Store current settings (now 5-bit: 0..31) */
    current_left_volume = left & 0x1F;
    current_right_volume = right & 0x1F;

    /* 1. SB Pro compatibility Master register (0x22).
          It's 4-bit per channel, so scale down. */
    uint8_t l4 = current_left_volume >> 1;
    uint8_t r4 = current_right_volume >> 1;
    uint8_t vol4 = (l4 << 4) | r4;
    mixer_write(0x22, vol4);

    /* 2. SB16 native master volume (5-bit per channel, top 5 bits of register)
          0x30: Master Left, 0x31: Master Right */
    mixer_write(0x30, current_left_volume << 3);
    mixer_write(0x31, current_right_volume << 3);

    /* We no longer touch Voice (0x32, 0x33, 0x04) here to avoid squared attenuation.
       Voice is set to MAX in sb16_init. */

    /* Ensure output gain is 0dB */
    mixer_write(0x41, 0x00); /* Left gain */
    mixer_write(0x42, 0x00); /* Right gain */
}

uint16_t sb16_get_volume(void) {
    if (!sb16_present) return 0;
    
    /* Read native SB16 Master registers (5-bit) */
    uint8_t l5 = mixer_read(0x30) >> 3;
    uint8_t r5 = mixer_read(0x31) >> 3;
    
    return ((uint16_t)l5 << 8) | r5;
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
        /* Silence - use memset equivalent for speed */
        uint32_t *buf32 = (uint32_t*)dma_buffer;
        uint32_t fill = 0x80808080;
        uint32_t count32 = total_samples >> 2;
        for (uint32_t i = 0; i < count32; i++) {
            buf32[i] = fill;
        }
        for (uint32_t i = count32 << 2; i < total_samples; i++) {
            dma_buffer[i] = 128;
        }
    } else {
        uint32_t samples_per_period = sample_rate / frequency;
        if (samples_per_period < 4) samples_per_period = 4;
        
        /* Phase accumulator approach - avoids expensive modulo per sample */
        uint32_t phase = 0;
        uint32_t phase_inc = (samples_per_period > 0) ? ((1 << 16) / samples_per_period) : (1 << 16);
        uint32_t half_period = samples_per_period >> 1;
        
        for (uint32_t i = 0; i < total_samples; i++) {
            int value = 128;
            uint32_t pos = phase >> 16;
            
            switch (waveform) {
                case WAVE_SQUARE:
                    value = (pos < half_period) ? 158 : 98;
                    break;
                    
                case WAVE_TRIANGLE: {
                    int amplitude;
                    if (pos < half_period) {
                        amplitude = (pos * 60) / half_period;
                    } else {
                        amplitude = 60 - ((pos - half_period) * 60) / half_period;
                    }
                    value = 98 + amplitude;
                    break;
                }
                    
                case WAVE_SAWTOOTH: {
                    int amplitude = (pos * 60) / samples_per_period;
                    value = 98 + amplitude;
                    break;
                }
                    
                case WAVE_SINE: {
                    /* Direct lookup with proper amplitude */
                    uint32_t idx = (pos << 6) / samples_per_period;
                    if (idx >= 64) idx = 63;
                    int sine_val = sine_table[idx];
                    /* Scale by 30/64 efficiently: (x*30)>>6 */
                    value = 128 + ((sine_val * 30) >> 6);
                    break;
                }
            }
            
            dma_buffer[i] = (uint8_t)value;
            
            /* Increment phase and wrap */
            phase += phase_inc;
            if ((phase >> 16) >= samples_per_period) {
                phase = 0;
            }
        }
        
        /* Fill remaining buffer with silence to prevent buzz */
        for (uint32_t i = total_samples; i < DMA_BUFFER_SIZE; i++) {
            dma_buffer[i] = 128;
        }
    }
    
    dsp_write(DSP_CMD_SPEAKER_OFF);
    dma_stop_channel(DMA_CHANNEL);
    
    /* Clear completion flag before starting */
    dma_complete = 0;
    
    /* 25ms pre-buffering delay for smoother playback */
    timer_wait(3);
    
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
    
    /* Wait for DMA completion via IRQ */
    uint32_t timeout = (duration_ms + 100) / 10;
    for (uint32_t i = 0; i < timeout && !dma_complete; i++) {
        timer_wait(1);
    }
    
    /* Aggressive cleanup sequence to eliminate buzz and prevent blocking */
    dsp_write(DSP_CMD_PAUSE_DMA);
    for (volatile int i = 0; i < 50; i++);
    
    inb(SB_DSP_INT_ACK);
    dma_stop_channel(DMA_CHANNEL);
    
    dsp_write(DSP_CMD_SPEAKER_OFF);
    for (volatile int i = 0; i < 50; i++);
    
    /* Soft reset DSP to clear any lingering state */
    outb(SB_DSP_RESET, 1);
    for (volatile int i = 0; i < 50; i++);
    outb(SB_DSP_RESET, 0);
    for (volatile int i = 0; i < 100; i++);
    
    /* Wait for DSP ready */
    for (int timeout_rst = 100; timeout_rst > 0; timeout_rst--) {
        if (inb(SB_DSP_READ_STATUS) & 0x80) {
            inb(SB_DSP_READ);
            break;
        }
    }
}

void sb16_stop(void) {
    if (!sb16_present) return;
    
    dsp_write(DSP_CMD_PAUSE_DMA);
    dsp_write(DSP_CMD_EXIT_AUTO_8BIT);
    dma_stop_channel(DMA_CHANNEL);
    dsp_write(DSP_CMD_SPEAKER_OFF);
}

void sb16_clear_irq_flag(void) {
    dma_complete = 0;
}

int sb16_check_irq_flag(void) {
    return dma_complete;
}
