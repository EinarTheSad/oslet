#include "private.h"

static char fat32_short_name_char(unsigned char ch) {
    if (ch == 0 || ch == ' ' || ch == '.' || ch == '/' || ch == '\\' ||
        ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' ||
        ch == '>' || ch == '|' || ch == '+' || ch == ',' || ch == ';' ||
        ch == '=' || ch == '[' || ch == ']' || ch == '\t' || ch == '\r' ||
        ch == '\n') {
        return '_';
    }
    return (char)toupper_s(ch);
}

uint8_t lfn_checksum(const char *short_name) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = ((sum & 1) << 7) + (sum >> 1) + (uint8_t)short_name[i];
    }
    return sum;
}

int lfn_matches_short_name(const char *long_name, const char *short_name) {
    char generated[11];
    if (!long_name || !short_name || !long_name[0]) return 0;
    parse_filename(long_name, generated);
    for (int i = 0; i < 11; i++) {
        if (toupper_s(generated[i]) != toupper_s(short_name[i])) return 0;
    }
    return 1;
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
    if (!name || !out_name) return;

    const char *dot = NULL;
    for (const char *p = name; *p; p++) {
        if (*p == '.') {
            dot = p;
            break;
        }
    }

    size_t base_len = dot ? (size_t)(dot - name) : strlen_s(name);
    size_t ext_len = dot ? strlen_s(dot + 1) : 0;

    if (base_len == 0 && dot && dot == name) {
        base_len = 0;
    }

    if (base_len > 8 || ext_len > 3) {
        size_t copy_len = base_len > 8 ? 6 : base_len;
        for (size_t i = 0; i < copy_len && i < 8; i++) {
            out_name[i] = fat32_short_name_char((unsigned char)name[i]);
        }

        if (base_len > 8) {
            out_name[6] = '~';
            out_name[7] = '1';
        }

        size_t ext_copy = ext_len > 3 ? 3 : ext_len;
        for (size_t i = 0; i < ext_copy; i++) {
            out_name[8 + i] = fat32_short_name_char((unsigned char)dot[1 + i]);
        }
        return;
    }

    size_t i = 0;
    while (name[i] && name[i] != '.' && i < 8) {
        out_name[i] = fat32_short_name_char((unsigned char)name[i]);
        i++;
    }

    if (dot && dot[1]) {
        size_t j = 8;
        for (size_t k = 1; dot[k] && j < 11; k++) {
            out_name[j++] = fat32_short_name_char((unsigned char)dot[k]);
        }
    }
}
