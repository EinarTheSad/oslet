#include "internal.h"

int text_split_lines(const char *text, char lines[][256], int max_lines) {
    int line_count = 0;
    const char *p = text;
    const char *line_start = p;

    while (*p && line_count < max_lines) {
        if (*p == '\\' && *(p+1) == 'n') {
            int len = p - line_start;
            if (len > 0 && len < 256) {
                int i;
                for (i = 0; i < len; i++) {
                    lines[line_count][i] = line_start[i];
                }
                lines[line_count][i] = '\0';
                line_count++;
            }
            p += 2;
            line_start = p;
        } else {
            p++;
        }
    }

    if (line_start < p && line_count < max_lines) {
        int len = p - line_start;
        if (len > 0 && len < 256) {
            int i;
            for (i = 0; i < len; i++) {
                lines[line_count][i] = line_start[i];
            }
            lines[line_count][i] = '\0';
            line_count++;
        }
    }

    return line_count;
}

int text_measure_height(const char *text, void *font_ptr, int size) {
    bmf_font_t *font = (bmf_font_t*)font_ptr;

    int seq_idx = -1;
    for (int i = 0; i < font->size_count; i++) {
        if (font->sequences[i].point_size == size) {
            seq_idx = i;
            break;
        }
    }
    int font_height = seq_idx >= 0 ? font->sequences[seq_idx].height : size;

    int line_count = 1;
    const char *p = text;
    while (*p) {
        if (*p == '\\' && *(p+1) == 'n') {
            line_count++;
            p += 2;
        } else {
            p++;
        }
    }

    return font_height + ((line_count - 1) * (font_height + 2)) + 4;
}
