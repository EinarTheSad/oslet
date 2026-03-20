#include "icon.h"
#include "bitmap.h"
#include "wm_config.h"
#include "../fonts/bmf.h"
#include "../drivers/graphics.h"
#include "../mem/heap.h"
#include <stddef.h>

extern bmf_font_t font_b, font_n;

typedef struct {
    int x, y, total_width;
    uint8_t color;
} label_draw_params_t;

static int label_wordwrap_process(const char *label, int max_line_width,
                                  label_draw_params_t *draw) {
    if (!label || !label[0] || !font_n.data) return 1;

    char line[64];
    char word[32];
    int line_idx = 0;
    int line_count = 0;
    int text_y = draw ? draw->y : 0;
    int i = 0;

    while (label[i]) {
        int word_idx = 0;
        while (label[i] && label[i] != ' ' && word_idx < 31) {
            word[word_idx++] = label[i++];
        }
        word[word_idx] = '\0';

        char test_line[64];
        if (line_idx > 0) {
            int k = 0;
            for (int j = 0; j < line_idx; j++) test_line[k++] = line[j];
            test_line[k++] = ' ';
            for (int j = 0; word[j]; j++) test_line[k++] = word[j];
            test_line[k] = '\0';
        } else {
            int k = 0;
            for (int j = 0; word[j]; j++) test_line[k++] = word[j];
            test_line[k] = '\0';
        }

        int test_w = bmf_measure_text(&font_n, 10, test_line);

        if (test_w > max_line_width && line_idx > 0) {
            line[line_idx] = '\0';
            line_count++;

            if (draw) {
                int line_w = bmf_measure_text(&font_n, 10, line);
                int line_x = draw->x + (draw->total_width - line_w) / 2;
                bmf_printf(line_x, text_y, &font_n, 10, draw->color, "%s", line);
                text_y += 11;
            }

            line_idx = 0;
            for (int j = 0; word[j]; j++) {
                line[line_idx++] = word[j];
            }
        } else {
            if (line_idx > 0) {
                line[line_idx++] = ' ';
            }
            for (int j = 0; word[j]; j++) {
                if (line_idx < 63) line[line_idx++] = word[j];
            }
        }

        while (label[i] == ' ') i++;
    }

    if (line_idx > 0) {
        line[line_idx] = '\0';
        line_count++;

        if (draw) {
            int final_w = bmf_measure_text(&font_n, 10, line);
            int final_x = draw->x + (draw->total_width - final_w) / 2;
            bmf_printf(final_x, text_y, &font_n, 10, draw->color, "%s", line);
        }
    }

    return line_count > 0 ? line_count : 1;
}

int icon_count_label_lines(const char *label, int max_line_width) {
    return label_wordwrap_process(label, max_line_width, NULL);
}

int icon_calc_total_height(int icon_size, int label_lines) {
    return icon_size + 5 + label_lines * 11 + 2;
}

void icon_draw_label_wrapped(const char *label, int x, int y, int total_width,
                             int max_line_width, uint8_t color) {
    label_draw_params_t params = { x, y, total_width, color };
    label_wordwrap_process(label, max_line_width, &params);
}
