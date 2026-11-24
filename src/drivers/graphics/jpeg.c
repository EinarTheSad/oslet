#include "gpriv.h"

#include "nanojpeg.h"

void *njAllocMem(int size) { return size > 0 ? kmalloc((size_t)size) : NULL; }
void njFreeMem(void *block) { kfree(block); }
void njFillMem(void *block, unsigned char byte, int size) { memset_s(block, byte, (size_t)size); }
void njCopyMem(void *dest, const void *src, int size) { memcpy_s(dest, src, (size_t)size); }

static int decoder_busy;

static int claim_decoder(void) {
    GFX_LOCK();
    int claimed = !decoder_busy;
    if (claimed) decoder_busy = 1;
    GFX_UNLOCK();
    return claimed;
}

static void release_decoder(void) {
    GFX_LOCK();
    decoder_busy = 0;
    GFX_UNLOCK();
}

uint8_t *gfx_load_jpeg_file(fat32_file_t *file, int *out_width, int *out_height) {
    uint8_t *input = NULL, *bitmap = NULL;
    uint32_t *colours = NULL;
    int width, height, colour, row_bytes;
    nj_result_t result;

    if (!claim_decoder()) {
        printf("JPEG decoder busy\n");
        return NULL;
    }
    if (file->size < 2 || file->size > 0x7FFFFFFF) goto fail;
    input = kmalloc(file->size);
    if (!input || fat32_read(file, input, file->size) != (int)file->size) goto fail;

    result = njDecode(input, (int)file->size);
    kfree(input);
    input = NULL;
    if (result != NJ_OK) {
        printf("JPEG decode failed: %d\n", (int)result);
        goto fail;
    }
    width = njGetWidth();
    height = njGetHeight();
    colour = njIsColor();
    row_bytes = (width + 1) / 2;
    bitmap = kmalloc((size_t)row_bytes * height);
    colours = kmalloc((size_t)width * height * sizeof(*colours));
    if (!bitmap || !colours) goto fail;

    const uint8_t *pixels = njGetImage();
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            size_t pos = (size_t)y * width + x;
            int r = pixels[pos * (colour ? 3 : 1)];
            int g = colour ? pixels[pos * 3 + 1] : r;
            int b = colour ? pixels[pos * 3 + 2] : r;
            colours[pos] = ((uint32_t)r << 16) |
                           ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
    if (gfx_dither_colors(bitmap, colours, width, height, 0) != 0) goto fail;
    if (gfx_bmp_set_colors(bitmap, colours) != 0) goto fail;
    njDone();
    release_decoder();
    *out_width = width;
    *out_height = height;
    return bitmap;

fail:
    printf("Could not load JPEG: %s\n", file->path);
    if (input) kfree(input);
    if (bitmap) kfree(bitmap);
    if (colours) kfree(colours);
    njDone();
    release_decoder();
    return NULL;
}
