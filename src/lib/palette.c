#include "palette.h"
#include "../syscall.h"
#include "../lib/string.h"
#include "../lib/stdio.h"

static const uint8_t default_palette[16][3] = {
    {0x00, 0x00, 0x00},
    {0x1E, 0x1E, 0x64},
    {0x34, 0x65, 0x24},
    {0x40, 0x95, 0xAA},
    {0x64, 0x3C, 0x18},
    {0x46, 0x23, 0x37},
    {0x64, 0x64, 0x18},
    {0xA0, 0xA0, 0xA0},
    {0x55, 0x55, 0x55},
    {0x59, 0x7D, 0xCE},
    {0x6D, 0xAA, 0x2C},
    {0x6E, 0xCE, 0xD8},
    {0xD0, 0x46, 0x48},
    {0xD2, 0xAA, 0x99},
    {0xE6, 0xDC, 0x42},
    {0xFF, 0xFF, 0xFF},
};

void palette_set_default(uint8_t palette[16][3]) {
    memcpy(palette, default_palette, sizeof(default_palette));
}

static char *next_token(char **cursor) {
    char *p = *cursor;

    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    if (!*p) {
        *cursor = p;
        return NULL;
    }

    char *token = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
        p++;
    if (*p) {
        *p = '\0';
        p++;
    }

    *cursor = p;
    return token;
}

static int parse_byte_token(char *token, uint8_t *out) {
    if (!token || !token[0] || !out) return -1;

    int value = 0;
    for (int i = 0; token[i]; i++) {
        if (token[i] < '0' || token[i] > '9')
            return -1;
        value = value * 10 + (token[i] - '0');
        if (value > 255)
            return -1;
    }

    *out = (uint8_t)value;
    return 0;
}

static int parse_count_token(char *token) {
    if (!token || !token[0]) return -1;

    int value = 0;
    for (int i = 0; token[i]; i++) {
        if (token[i] < '0' || token[i] > '9')
            return -1;
        value = value * 10 + (token[i] - '0');
        if (value > 4096)
            return -1;
    }

    return value;
}

int palette_parse_pal(const char *path, uint8_t palette[16][3]) {
    if (!path || !path[0] || !palette) return -1;

    int fd = sys_open(path, "r");
    if (fd < 0) return -1;

    char buffer[4096];
    int bytes = sys_read(fd, buffer, sizeof(buffer) - 1);
    sys_close(fd);
    if (bytes <= 0) return -1;
    buffer[bytes] = '\0';

    char *cursor = buffer;
    char *token = next_token(&cursor);
    if (!token || strcmp(token, "JASC-PAL") != 0)
        return -1;

    token = next_token(&cursor);
    if (!token || strcmp(token, "0100") != 0)
        return -1;

    token = next_token(&cursor);
    int count = parse_count_token(token);
    if (count < 16)
        return -1;

    uint8_t temp[16][3];
    for (int i = 0; i < 16; i++) {
        uint8_t r, g, b;
        if (parse_byte_token(next_token(&cursor), &r) != 0 ||
            parse_byte_token(next_token(&cursor), &g) != 0 ||
            parse_byte_token(next_token(&cursor), &b) != 0)
            return -1;
        temp[i][0] = r;
        temp[i][1] = g;
        temp[i][2] = b;
    }

    memcpy(palette, temp, sizeof(temp));
    return 0;
}

int palette_save_pal(const char *path, const uint8_t palette[16][3]) {
    if (!path || !path[0] || !palette) return -1;

    int fd = sys_open(path, "w");
    if (fd < 0) return -1;

    char buffer[512];
    int offset = snprintf(buffer, sizeof(buffer),
        "JASC-PAL\r\n"
        "0100\r\n"
        "16\r\n");

    for (int i = 0; i < 16; i++) {
        if (offset < 0 || offset >= (int)sizeof(buffer)) {
            sys_close(fd);
            return -1;
        }
        offset += snprintf(buffer + offset, sizeof(buffer) - offset,
            "%d %d %d\r\n",
            palette[i][0], palette[i][1], palette[i][2]);
    }

    if (offset < 0 || offset >= (int)sizeof(buffer)) {
        sys_close(fd);
        return -1;
    }

    int written = sys_write_file(fd, buffer, offset);
    sys_close(fd);
    return (written == offset) ? 0 : -1;
}

int palette_load_default_file(const char *path, uint8_t palette[16][3]) {
    if (!path || !path[0] || !palette) return -1;
    return palette_parse_pal(path, palette);
}
