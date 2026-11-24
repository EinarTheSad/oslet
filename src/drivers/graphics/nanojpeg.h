#pragma once

typedef enum {
    NJ_OK = 0,
    NJ_NO_JPEG,
    NJ_UNSUPPORTED,
    NJ_OUT_OF_MEM,
    NJ_INTERNAL_ERR,
    NJ_SYNTAX_ERROR,
    __NJ_FINISHED
} nj_result_t;

/* The decoded pixels are top-down RGB or greyscale and remain owned by NanoJPEG.
   The decoder has global state, so callers must serialise use until njDone(). */
nj_result_t njDecode(const void *jpeg, int size);
int njGetWidth(void);
int njGetHeight(void);
int njIsColor(void);
unsigned char *njGetImage(void);
void njDone(void);
