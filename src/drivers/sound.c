#include "sound.h"
#include "sb16.h"
#include "fat32.h"
#include "dma.h"
#include "../irq/io.h"
#include "../mem/heap.h"
#include "../console.h"
#include "../task/task.h"

#define SB_DSP_WRITE        0x22C
#define SB_DSP_8BIT_ACK     0x22E
#define SB_DSP_16BIT_ACK    0x22F

#define DSP_CMD_SET_OUTPUT_RATE 0x41
#define DSP_CMD_DMA_8BIT_AUTO   0xC6
#define DSP_CMD_SPEAKER_ON      0xD1
#define DSP_CMD_SPEAKER_OFF     0xD3
#define DSP_CMD_PAUSE_DMA       0xD0
#define DSP_CMD_EXIT_AUTO_8BIT  0xDA

#define DMA_CHANNEL 1
#define DMA_MODE_AUTO 0x58

#define SOUND_BLOCK_SAMPLES 2048
#define SOUND_DMA_SAMPLES   (SOUND_BLOCK_SAMPLES * 2)
#define TEMP_BUFFER_SIZE    16384
#define FADE_SAMPLES        256
#define SOUND_MAX_PCM_BYTES (12 * 1024 * 1024)

typedef struct {
    char id[4];
    uint32_t size;
} wav_chunk_header_t;

typedef struct {
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} wav_format_t;

typedef struct {
    wav_format_t fmt;
    uint32_t data_size;
    uint32_t data_left;
    uint32_t total_samples;
    uint32_t sample_index;
} wav_stream_t;

typedef struct {
    volatile int active;
    uint8_t *pcm;
    uint32_t total_samples;
    uint32_t pos;
    uint32_t played;
    uint32_t block_samples[2];
    int playing_block;
} sound_playback_t;

static uint8_t dma_buffer[SOUND_DMA_SAMPLES] __attribute__((aligned(65536)));
static uint8_t temp_buffer[TEMP_BUFFER_SIZE];

static volatile int sound_lock = 0;
static volatile int worker_running = 0;
static volatile int request_pending = 0;
static volatile int stop_requested = 0;
static volatile uint32_t playback_generation = 0;
static char pending_path[FAT32_MAX_PATH];

static sound_playback_t playback;
static int callback_installed = 0;

static void sound_irq_callback(void);

static inline uint32_t irq_save(void) {
    uint32_t eflags;
    __asm__ volatile("pushfl\n\tpopl %0\n\tcli" : "=r"(eflags) :: "memory");
    return eflags;
}

static inline void irq_restore(uint32_t eflags) {
    __asm__ volatile("pushl %0\n\tpopfl" :: "r"(eflags) : "cc", "memory");
}

static void sound_acquire(void) {
    while (__sync_lock_test_and_set(&sound_lock, 1)) {
        task_yield();
    }
}

static void sound_release(void) {
    __sync_lock_release(&sound_lock);
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

static int write_dsp_cmd(uint8_t value) {
    if (dsp_write(value) != 0) return -1;
    return 0;
}

static void sound_hw_stop_unlocked(void) {
    if (!sb16_detected()) return;

    dsp_write(DSP_CMD_PAUSE_DMA);
    dsp_write(DSP_CMD_EXIT_AUTO_8BIT);
    dma_stop_channel(DMA_CHANNEL);
    dsp_write(DSP_CMD_SPEAKER_OFF);
    inb(SB_DSP_8BIT_ACK);
    inb(SB_DSP_16BIT_ACK);
}

static void sound_hw_stop(void) {
    if (!sb16_detected()) return;

    sb16_acquire();
    sound_hw_stop_unlocked();
    sb16_release();
}

static void sound_reset_playback(void) {
    uint8_t *old_pcm;
    uint32_t eflags;

    eflags = irq_save();
    playback.active = 0;
    old_pcm = playback.pcm;
    playback.pcm = 0;
    playback.total_samples = 0;
    playback.pos = 0;
    playback.played = 0;
    playback.block_samples[0] = 0;
    playback.block_samples[1] = 0;
    playback.playing_block = 0;
    irq_restore(eflags);

    if (old_pcm)
        kfree(old_pcm);
}

static int sound_hw_start(uint32_t sample_rate) {
    uint32_t dma_phys = (uint32_t)(uintptr_t)dma_buffer;
    uint16_t block_size = SOUND_BLOCK_SAMPLES - 1;

    if (dma_phys >= 0x1000000) return -1;

    sb16_acquire();
    dma_stop_channel(DMA_CHANNEL);
    dma_setup_channel(DMA_CHANNEL, dma_phys, SOUND_DMA_SAMPLES, DMA_MODE_AUTO);

    if (write_dsp_cmd(DSP_CMD_SET_OUTPUT_RATE) != 0 ||
        write_dsp_cmd((sample_rate >> 8) & 0xFF) != 0 ||
        write_dsp_cmd(sample_rate & 0xFF) != 0 ||
        write_dsp_cmd(DSP_CMD_SPEAKER_ON) != 0) {
        dma_stop_channel(DMA_CHANNEL);
        sb16_release();
        return -1;
    }

    dma_start_channel(DMA_CHANNEL);

    if (write_dsp_cmd(DSP_CMD_DMA_8BIT_AUTO) != 0 ||
        write_dsp_cmd(0x00) != 0 ||
        write_dsp_cmd(block_size & 0xFF) != 0 ||
        write_dsp_cmd((block_size >> 8) & 0xFF) != 0) {
        dma_stop_channel(DMA_CHANNEL);
        dsp_write(DSP_CMD_SPEAKER_OFF);
        sb16_release();
        return -1;
    }

    sb16_release();
    return 0;
}

static int should_stop(uint32_t generation) {
    int stop;

    sound_acquire();
    stop = stop_requested || generation != playback_generation;
    sound_release();

    return stop;
}

static int read_exact(fat32_file_t *file, void *buffer, uint32_t size) {
    uint8_t *dst = (uint8_t *)buffer;
    uint32_t done = 0;

    while (done < size) {
        int n = fat32_read(file, dst + done, size - done);
        if (n <= 0) return -1;
        done += (uint32_t)n;
    }

    return 0;
}

static int parse_wav(fat32_file_t *file, wav_stream_t *stream) {
    char riff[4];
    uint32_t riff_size;
    char wave[4];
    int found_fmt = 0;

    memset_s(stream, 0, sizeof(*stream));

    if (read_exact(file, riff, 4) != 0 ||
        read_exact(file, &riff_size, sizeof(riff_size)) != 0 ||
        read_exact(file, wave, 4) != 0) {
        return -1;
    }
    (void)riff_size;

    if (memcmp_s(riff, "RIFF", 4) != 0 ||
        memcmp_s(wave, "WAVE", 4) != 0) {
        return -1;
    }

    while (fat32_tell(file) + sizeof(wav_chunk_header_t) <= file->size) {
        wav_chunk_header_t chunk;
        uint32_t chunk_start;
        uint32_t padded_size;

        if (read_exact(file, &chunk, sizeof(chunk)) != 0)
            return -1;

        chunk_start = fat32_tell(file);
        padded_size = chunk.size + (chunk.size & 1);

        if (memcmp_s(chunk.id, "fmt ", 4) == 0) {
            uint32_t fmt_bytes = chunk.size;

            if (fmt_bytes < 16)
                return -1;

            if (read_exact(file, &stream->fmt, sizeof(wav_format_t)) != 0)
                return -1;

            found_fmt = 1;
        } else if (memcmp_s(chunk.id, "data", 4) == 0) {
            if (!found_fmt)
                return -1;

            stream->data_size = chunk.size;
            stream->data_left = chunk.size;
            break;
        }

        if (fat32_seek(file, chunk_start + padded_size) != 0)
            return -1;
    }

    if (!found_fmt || stream->data_size == 0)
        return -1;

    if (stream->fmt.audio_format != 1 ||
        (stream->fmt.num_channels != 1 && stream->fmt.num_channels != 2) ||
        (stream->fmt.bits_per_sample != 8 && stream->fmt.bits_per_sample != 16) ||
        stream->fmt.sample_rate < 4000 ||
        stream->fmt.sample_rate > 48000) {
        return -1;
    }

    uint32_t bytes_per_sample = stream->fmt.bits_per_sample / 8;
    uint32_t frame_size = bytes_per_sample * stream->fmt.num_channels;

    if (stream->fmt.block_align < frame_size)
        stream->fmt.block_align = (uint16_t)frame_size;

    if (stream->fmt.block_align == 0)
        return -1;

    stream->total_samples = stream->data_size / stream->fmt.block_align;
    if (stream->total_samples == 0)
        return -1;

    return 0;
}

static uint8_t fade_sample(uint8_t sample, uint32_t sample_index, uint32_t total_samples) {
    int centered = (int)sample - 128;
    uint32_t gain = FADE_SAMPLES;
    uint32_t remaining;

    if (sample_index < gain)
        gain = sample_index;

    remaining = total_samples - sample_index;
    if (remaining < gain)
        gain = remaining;

    if (gain >= FADE_SAMPLES)
        return sample;

    return (uint8_t)(128 + (centered * (int)gain) / (int)FADE_SAMPLES);
}

static int16_t read_s16le(const uint8_t *p) {
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t decode_wav_to_pcm(fat32_file_t *file, wav_stream_t *stream,
                                  uint8_t *pcm, uint32_t generation) {
    uint32_t decoded = 0;
    uint32_t frame_size = stream->fmt.block_align;
    int stereo = stream->fmt.num_channels == 2;
    int is_16bit = stream->fmt.bits_per_sample == 16;

    while (!should_stop(generation) &&
           decoded < stream->total_samples &&
           stream->data_left >= frame_size) {
        uint32_t wanted_frames = stream->total_samples - decoded;
        uint32_t wanted_bytes = wanted_frames * frame_size;
        int bytes_read;

        if (wanted_bytes > TEMP_BUFFER_SIZE)
            wanted_bytes = TEMP_BUFFER_SIZE;
        if (wanted_bytes > stream->data_left)
            wanted_bytes = stream->data_left;

        wanted_bytes -= wanted_bytes % frame_size;
        if (wanted_bytes == 0)
            break;

        bytes_read = fat32_read(file, temp_buffer, wanted_bytes);
        if (bytes_read <= 0)
            break;

        bytes_read -= bytes_read % (int)frame_size;
        stream->data_left -= (uint32_t)bytes_read;

        for (int offset = 0; offset < bytes_read; offset += (int)frame_size) {
            int sample;

            if (is_16bit) {
                int s1 = read_s16le(temp_buffer + offset);
                if (stereo) {
                    int s2 = read_s16le(temp_buffer + offset + 2);
                    s1 = (s1 + s2) / 2;
                }
                sample = (s1 >> 8) + 128;
            } else {
                sample = temp_buffer[offset];
                if (stereo)
                    sample = (sample + temp_buffer[offset + 1]) / 2;
            }

            if (sample < 0) sample = 0;
            if (sample > 255) sample = 255;

            pcm[decoded++] = fade_sample((uint8_t)sample,
                                         stream->sample_index,
                                         stream->total_samples);
            stream->sample_index++;

            if ((decoded & 0x1FF) == 0) {
                if (should_stop(generation))
                    break;
                task_yield();
            }
        }

        task_yield();
    }

    return decoded;
}

static uint32_t fill_dma_from_pcm(const uint8_t *pcm, uint32_t total_samples,
                                  uint32_t *pcm_pos, uint8_t *dst,
                                  uint32_t max_samples) {
    uint32_t remaining;
    uint32_t count;

    memset_s(dst, 0x80, max_samples);

    if (*pcm_pos >= total_samples)
        return 0;

    remaining = total_samples - *pcm_pos;
    count = remaining < max_samples ? remaining : max_samples;
    memcpy_s(dst, pcm + *pcm_pos, count);
    *pcm_pos += count;

    return count;
}

static void sound_irq_callback(void) {
    int done_block;

    if (!playback.active || !playback.pcm)
        return;

    done_block = playback.playing_block;
    playback.played += playback.block_samples[done_block];

    if (playback.played >= playback.total_samples) {
        playback.active = 0;
        sound_hw_stop_unlocked();
        return;
    }

    playback.block_samples[done_block] =
        fill_dma_from_pcm(playback.pcm,
                          playback.total_samples,
                          &playback.pos,
                          dma_buffer + (done_block * SOUND_BLOCK_SAMPLES),
                          SOUND_BLOCK_SAMPLES);

    playback.playing_block ^= 1;
}

static int load_and_start_wav(const char *path, uint32_t generation) {
    fat32_file_t *file;
    wav_stream_t stream;
    uint8_t *pcm;
    uint32_t decoded_samples;
    uint32_t eflags;

    if (!sb16_detected())
        return -1;

    file = fat32_open(path, "r");
    if (!file)
        return -1;

    if (parse_wav(file, &stream) != 0) {
        fat32_close(file);
        return -1;
    }

    if (stream.total_samples > SOUND_MAX_PCM_BYTES) {
        fat32_close(file);
        return -1;
    }

    pcm = (uint8_t *)kmalloc(stream.total_samples);
    if (!pcm) {
        fat32_close(file);
        return -1;
    }

    decoded_samples = decode_wav_to_pcm(file, &stream, pcm, generation);
    fat32_close(file);

    if (decoded_samples == 0 || should_stop(generation)) {
        kfree(pcm);
        return -1;
    }

    stream.total_samples = decoded_samples;

    eflags = irq_save();
    playback.active = 0;
    if (playback.pcm) {
        uint8_t *old_pcm = playback.pcm;
        playback.pcm = 0;
        irq_restore(eflags);
        kfree(old_pcm);
        eflags = irq_save();
    }

    playback.pcm = pcm;
    playback.total_samples = stream.total_samples;
    playback.pos = 0;
    playback.played = 0;
    playback.playing_block = 0;
    playback.block_samples[0] =
        fill_dma_from_pcm(playback.pcm, playback.total_samples,
                          &playback.pos, dma_buffer,
                          SOUND_BLOCK_SAMPLES);
    playback.block_samples[1] =
        fill_dma_from_pcm(playback.pcm, playback.total_samples,
                          &playback.pos,
                          dma_buffer + SOUND_BLOCK_SAMPLES,
                          SOUND_BLOCK_SAMPLES);

    if (playback.block_samples[0] == 0) {
        playback.pcm = 0;
        playback.total_samples = 0;
        irq_restore(eflags);
        kfree(pcm);
        return -1;
    }
    playback.active = 1;
    irq_restore(eflags);

    if (sound_hw_start(stream.fmt.sample_rate) != 0) {
        eflags = irq_save();
        playback.active = 0;
        playback.pcm = 0;
        playback.total_samples = 0;
        irq_restore(eflags);
        kfree(pcm);
        return -1;
    }

    return 0;
}

static int take_request(char *path, uint32_t *generation) {
    sound_acquire();

    if (!request_pending) {
        worker_running = 0;
        sound_release();
        return 0;
    }

    strcpy_s(path, pending_path, FAT32_MAX_PATH);
    *generation = playback_generation;
    request_pending = 0;
    stop_requested = 0;

    sound_release();
    return 1;
}

static void sound_worker(void) {
    char path[FAT32_MAX_PATH];
    uint32_t generation;

    while (take_request(path, &generation)) {
        load_and_start_wav(path, generation);
    }
}

int sound_play_wav(const char *path) {
    int start_worker = 0;

    if (!sb16_detected() || !path || !path[0])
        return -1;

    if (!callback_installed) {
        sb16_set_irq_callback(sound_irq_callback);
        callback_installed = 1;
    }

    sound_acquire();
    strcpy_s(pending_path, path, sizeof(pending_path));
    playback_generation++;
    stop_requested = 1;
    request_pending = 1;

    if (!worker_running) {
        worker_running = 1;
        start_worker = 1;
    }
    sound_release();

    sound_hw_stop();
    sound_reset_playback();

    if (start_worker) {
        if (task_create(sound_worker, "sound", PRIORITY_NORMAL) == 0) {
            sound_acquire();
            worker_running = 0;
            request_pending = 0;
            stop_requested = 0;
            sound_release();
            return -1;
        }
    }

    return 0;
}

void sound_stop(void) {
    sound_acquire();
    playback_generation++;
    stop_requested = 1;
    request_pending = 0;
    sound_release();

    sound_hw_stop();
    sound_reset_playback();
}
