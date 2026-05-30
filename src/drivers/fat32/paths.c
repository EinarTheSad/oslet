#include "private.h"

uint16_t rtc_to_fat_time(const rtc_time_t *rtc) {
    return ((uint16_t)(rtc->hour & 0x1F) << 11) |
           ((uint16_t)(rtc->minute & 0x3F) << 5) |
           ((uint16_t)(rtc->second / 2) & 0x1F);
}

uint16_t rtc_to_fat_date(const rtc_time_t *rtc) {
    uint16_t year = (rtc->year >= 1980) ? (rtc->year - 1980) : 0;
    if (year > 127) year = 127;
    return ((year & 0x7F) << 9) |
           ((uint16_t)(rtc->month & 0x0F) << 5) |
           ((uint16_t)(rtc->day & 0x1F));
}

void get_fat_timestamp(uint16_t *time, uint16_t *date) {
    rtc_time_t rtc;
    rtc_read_time(&rtc);
    if (time) *time = rtc_to_fat_time(&rtc);
    if (date) *date = rtc_to_fat_date(&rtc);
}

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

int parse_path(const char *path, uint8_t *drive, char *rest, size_t rest_size) {
    if (!path || !drive || !rest) return -1;
    
    *drive = current_dir[0];
    
    if (path[0] && path[1] == ':') {
        *drive = toupper_s(path[0]);
        path += 2;
    }
    
    if (path[0] != '/' && path[0] != '\0') {
        char temp[FAT32_MAX_PATH];
        strcpy_s(temp, current_dir + 3, sizeof(temp));
        
        size_t len = strlen_s(temp);
        if (len > 0 && temp[len-1] != '/') {
            if (len < FAT32_MAX_PATH - 1) {
                temp[len] = '/';
                temp[len+1] = '\0';
            }
        }
        
        size_t temp_len = strlen_s(temp);
        size_t path_len = strlen_s(path);
        if (temp_len + path_len < sizeof(temp)) {
            strcpy_s(temp + temp_len, path, sizeof(temp) - temp_len);
        }
        strcpy_s(rest, temp, rest_size);
    } else if (path[0] == '\0' || strcmp_s(path, ".") == 0) {
        strcpy_s(rest, current_dir + 3, rest_size);
    } else {
        if (path[0] == '/') path++;
        strcpy_s(rest, path, rest_size);
    }
    
    return 0;
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
