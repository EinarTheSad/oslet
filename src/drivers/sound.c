#include "sound.h"
#include "sb16.h"
#include "fat32.h"
#include "dma.h"
#include "../irq/io.h"
#include "../mem/heap.h"
#include "../console.h"
#include "../timer.h"

#define SB_DSP_RESET     0x226
#define SB_DSP_READ      0x22A
#define SB_DSP_WRITE     0x22C
#define SB_DSP_READ_STATUS 0x22E
#define SB_DSP_INT_ACK   0x22F

#define DSP_CMD_SET_TIME_CONSTANT  0x40
#define DSP_CMD_SET_OUTPUT_RATE    0x41
#define DSP_CMD_DMA_8BIT_SINGLE    0x14
#define DSP_CMD_SPEAKER_ON         0xD1
#define DSP_CMD_SPEAKER_OFF        0xD3
#define DSP_CMD_PAUSE_DMA          0xD0

#define DMA_CHANNEL 1
#define DMA_BUFFER_SIZE 4096
#define TEMP_BUFFER_SIZE 8192

typedef struct {
    char riff[4];
    uint32_t file_size;
    char wave[4];
} wav_riff_header_t;

typedef struct {
    char fmt[4];
    uint32_t chunk_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} wav_fmt_chunk_t;

typedef struct {
    char data[4];
    uint32_t data_size;
} wav_data_header_t;

static uint8_t dma_buffer[DMA_BUFFER_SIZE] __attribute__((aligned(4096)));
static uint8_t temp_buffer[TEMP_BUFFER_SIZE];

static uint32_t get_dma_phys(void) {
    return (uint32_t)(uintptr_t)dma_buffer;
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

static void wait_dma_complete(uint32_t sample_rate, uint32_t samples) {
    uint32_t duration_ms = (samples * 1000) / sample_rate;
    uint32_t wait_ticks = (duration_ms + 5) / 10;
    if (wait_ticks < 1) wait_ticks = 1;
    
    for (uint32_t i = 0; i < wait_ticks; i++) {
        timer_wait(1);
    }
    
    for (int poll = 0; poll < 100; poll++) {
        if (sb16_check_irq_flag()) {
            return;
        }
        for (volatile int j = 0; j < 10; j++);
    }
}

int sound_play_wav(const char *path) {
    if (!sb16_detected()) {
        return -1;
    }

    uint32_t dma_phys = get_dma_phys();
    if (dma_phys >= 0x1000000) {
        return -1;
    }

    fat32_file_t *file = fat32_open(path, "r");
    if (!file) {
        return -1;
    }

    wav_riff_header_t riff;
    if (fat32_read(file, &riff, sizeof(riff)) != sizeof(riff)) {
        fat32_close(file);
        return -1;
    }

    if (memcmp_s(riff.riff, "RIFF", 4) != 0 || 
        memcmp_s(riff.wave, "WAVE", 4) != 0) {
        fat32_close(file);
        return -1;
    }

    wav_fmt_chunk_t fmt;
    if (fat32_read(file, &fmt, sizeof(fmt)) != sizeof(fmt)) {
        fat32_close(file);
        return -1;
    }

    if (memcmp_s(fmt.fmt, "fmt ", 4) != 0) {
        fat32_close(file);
        return -1;
    }

    if (fmt.audio_format != 1) {
        fat32_close(file);
        return -1;
    }

    if (fmt.chunk_size > 16) {
        uint32_t skip = fmt.chunk_size - 16;
        fat32_seek(file, fat32_tell(file) + skip);
    }

    wav_data_header_t data_hdr;
    if (fat32_read(file, &data_hdr, sizeof(data_hdr)) != sizeof(data_hdr)) {
        fat32_close(file);
        return -1;
    }

    if (memcmp_s(data_hdr.data, "data", 4) != 0) {
        fat32_close(file);
        return -1;
    }

    uint32_t data_size = data_hdr.data_size;
    if (data_size == 0) {
        fat32_close(file);
        return -1;
    }

    uint32_t bytes_per_sample = fmt.bits_per_sample / 8;
    uint32_t total_samples = data_size / (bytes_per_sample * fmt.num_channels);
    uint32_t sample_rate = fmt.sample_rate;

    sb16_acquire();
    dsp_write(DSP_CMD_SPEAKER_OFF);
    dma_stop_channel(DMA_CHANNEL);

    dsp_write(DSP_CMD_SET_OUTPUT_RATE);
    dsp_write((sample_rate >> 8) & 0xFF);
    dsp_write(sample_rate & 0xFF);
    sb16_release();

    uint32_t samples_played = 0;
    int is_stereo = (fmt.num_channels == 2);
    int is_16bit = (bytes_per_sample == 2);
    
    while (samples_played < total_samples) {
        uint32_t samples_to_play = total_samples - samples_played;
        if (samples_to_play > DMA_BUFFER_SIZE) {
            samples_to_play = DMA_BUFFER_SIZE;
        }

        uint32_t bytes_to_read = samples_to_play * bytes_per_sample * fmt.num_channels;
        if (bytes_to_read > TEMP_BUFFER_SIZE) {
            bytes_to_read = TEMP_BUFFER_SIZE;
            samples_to_play = bytes_to_read / (bytes_per_sample * fmt.num_channels);
        }

        int bytes_read = fat32_read(file, temp_buffer, bytes_to_read);
        if (bytes_read <= 0) {
            break;
        }

        uint32_t actual_samples = bytes_read / (bytes_per_sample * fmt.num_channels);

        for (uint32_t i = 0; i < actual_samples; i++) {
            int sample_value = 128;
            uint32_t src_offset = i * bytes_per_sample * fmt.num_channels;
            
            if (is_16bit) {
                if (src_offset + 2 <= (uint32_t)bytes_read) {
                    int16_t s16 = *(int16_t*)&temp_buffer[src_offset];
                    sample_value = (s16 >> 8) + 128;
                }
                
                if (is_stereo && src_offset + 4 <= (uint32_t)bytes_read) {
                    int16_t s16_2 = *(int16_t*)&temp_buffer[src_offset + 2];
                    int sample2 = (s16_2 >> 8) + 128;
                    sample_value = (sample_value + sample2) >> 1;
                }
            } else {
                if (src_offset < (uint32_t)bytes_read) {
                    sample_value = temp_buffer[src_offset];
                }
                
                if (is_stereo && src_offset + 2 <= (uint32_t)bytes_read) {
                    sample_value = ((int)sample_value + temp_buffer[src_offset + 1]) >> 1;
                }
            }
            
            dma_buffer[i] = (uint8_t)sample_value;
        }

        sb16_acquire();
        sb16_clear_irq_flag();
        dma_setup_channel(DMA_CHANNEL, dma_phys, actual_samples, 0x48);

        dsp_write(DSP_CMD_SPEAKER_ON);
        dma_start_channel(DMA_CHANNEL);

        dsp_write(DSP_CMD_DMA_8BIT_SINGLE);
        uint16_t block_size = actual_samples - 1;
        dsp_write(block_size & 0xFF);
        dsp_write((block_size >> 8) & 0xFF);
        sb16_release();

        wait_dma_complete(sample_rate, actual_samples);

        samples_played += actual_samples;
    }

    sb16_acquire();
    dsp_write(DSP_CMD_SPEAKER_OFF);
    dma_stop_channel(DMA_CHANNEL);
    inb(SB_DSP_INT_ACK);
    sb16_release();

    fat32_close(file);
    return 0;
}

void sound_stop(void) {
    sb16_acquire();
    sb16_stop();
    sb16_release();
}

