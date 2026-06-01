#include "private.h"

uint8_t lfn_checksum(const char *short_name) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = ((sum & 1) << 7) + (sum >> 1) + (uint8_t)short_name[i];
    }
    return sum;
}

void utf16_to_ascii(const uint16_t *src, char *dst, int max_chars) {
    for (int i = 0; i < max_chars && src[i] != 0 && src[i] != 0xFFFF; i++) {
        dst[i] = (src[i] < 128) ? (char)src[i] : '?';
    }
}

void ascii_to_utf16(const char *src, uint16_t *dst, int max_chars) {
    int i;
    for (i = 0; i < max_chars && src[i]; i++) {
        dst[i] = (uint16_t)(uint8_t)src[i];
    }
    for (; i < max_chars; i++) {
        dst[i] = 0xFFFF;
    }
}

void parse_filename(const char *name, char *out_name) {
    memset_s(out_name, ' ', 11);
    int i = 0, j = 0;

    while (name[i] && name[i] != '.' && j < 8) {
        out_name[j++] = toupper_s(name[i++]);
    }

    if (name[i] == '.') {
        i++;
        j = 8;
        while (name[i] && j < 11) {
            out_name[j++] = toupper_s(name[i++]);
        }
    }
}
