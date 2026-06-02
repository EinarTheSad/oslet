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
    int max_lines;
} label_draw_params_t;

static int label_len(const char *s) {
    int len = 0;
    if (!s) return 0;
    while (s[len]) len++;
    return len;
}

static void label_draw_line(const char *line, int y, label_draw_params_t *draw) {
    int line_w;
    int line_x;

    if (!draw || !line || !line[0] || draw->total_width <= 0) return;
    line_w = bmf_measure_text(&font_n, 10, line);
    line_x = draw->x + (draw->total_width - line_w) / 2;
    if (line_x < draw->x) line_x = draw->x;
    bmf_printf(line_x, y, &font_n, 10, draw->color, "%s", line);
}

static void label_add_ellipsis(char *line, int max_line_width) {
    int len;
    char test[64];
    const char *dots = "...";

    if (!line) return;
    len = label_len(line);

    while (len > 0) {
        int pos = 0;
        for (int i = 0; i < len && pos < 60; i++)
            test[pos++] = line[i];
        for (int i = 0; dots[i] && pos < 63; i++)
            test[pos++] = dots[i];
        test[pos] = '\0';

        if (bmf_measure_text(&font_n, 10, test) <= max_line_width) {
            for (int i = 0; i <= pos; i++)
                line[i] = test[i];
            return;
        }
        len--;
    }

    if (bmf_measure_text(&font_n, 10, dots) <= max_line_width) {
        line[0] = '.';
        line[1] = '.';
        line[2] = '.';
        line[3] = '\0';
    } else {
        line[0] = '\0';
    }
}

static void label_copy(char *dst, int dst_size, const char *src) {
    int i = 0;
    if (!dst || dst_size <= 0) return;
    dst[0] = '\0';
    if (!src) return;
    while (src[i] && i < dst_size - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void label_copy_n(char *dst, int dst_size, const char *src, int len) {
    int i = 0;
    if (!dst || dst_size <= 0) return;
    dst[0] = '\0';
    if (!src || len <= 0) return;
    while (src[i] && i < len && i < dst_size - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void label_clip_to_width(char *dst, int dst_size, const char *src,
                                int max_line_width, int add_dots) {
    if (!dst || dst_size <= 0) return;
    label_copy(dst, dst_size, src);

    while (dst[0] && bmf_measure_text(&font_n, 10, dst) > max_line_width) {
        int len = label_len(dst);
        dst[len - 1] = '\0';
    }

    if (add_dots && src && src[0] && src[label_len(dst)])
        label_add_ellipsis(dst, max_line_width);
}

static void label_trim_end(char *s) {
    int len = label_len(s);
    while (len > 0 && s[len - 1] == ' ') {
        s[len - 1] = '\0';
        len--;
    }
}

static void label_trim_start(char *s) {
    int i = 0;
    if (!s) return;
    while (s[i] == ' ')
        i++;
    if (i > 0) {
        int j = 0;
        while (s[i])
            s[j++] = s[i++];
        s[j] = '\0';
    }
}

static int label_prepare_two_lines(const char *label, int max_line_width,
                                   char lines[2][64]) {
    int len;
    int dot = -1;

    lines[0][0] = '\0';
    lines[1][0] = '\0';

    if (!label || !label[0] || !font_n.data)
        return 1;

    if (bmf_measure_text(&font_n, 10, label) <= max_line_width) {
        label_copy(lines[0], 64, label);
        return 1;
    }

    len = label_len(label);
    for (int i = len - 1; i > 0; i--) {
        if (label[i] == '.') {
            dot = i;
            break;
        }
    }

    if (dot > 0 && dot < len - 1) {
        char base[64];
        char ext[64];
        label_copy_n(base, sizeof(base), label, dot);
        label_copy(ext, sizeof(ext), label + dot);

        label_clip_to_width(lines[0], 64, base, max_line_width, 1);
        label_clip_to_width(lines[1], 64, ext, max_line_width, 0);

        if (!lines[0][0]) {
            label_copy(lines[0], 64, lines[1]);
            lines[1][0] = '\0';
            return 1;
        }
        return lines[1][0] ? 2 : 1;
    }

    int split = -1;
    for (int i = 1; label[i]; i++) {
        if (label[i] == ' ') {
            char test[64];
            label_copy_n(test, sizeof(test), label, i);
            label_trim_end(test);
            if (test[0] && bmf_measure_text(&font_n, 10, test) <= max_line_width)
                split = i;
        }
    }

    if (split > 0) {
        char first[64];
        char second[64];

        label_copy_n(first, sizeof(first), label, split);
        label_trim_end(first);
        label_clip_to_width(lines[0], 64, first, max_line_width, 0);

        label_copy(second, sizeof(second), label + split + 1);
        label_trim_start(second);
        label_clip_to_width(lines[1], 64, second, max_line_width, 1);

        if (!lines[1][0])
            return 1;
        return 2;
    }

    label_clip_to_width(lines[0], 64, label, max_line_width, 1);
    return 1;
}

static int label_wordwrap_process(const char *label, int max_line_width,
                                  label_draw_params_t *draw) {
    if (!label || !label[0] || !font_n.data) return 1;

    char line[64];
    int line_idx = 0;
    int line_count = 0;
    int text_y = draw ? draw->y : 0;
    int max_lines = draw ? draw->max_lines : 0;
    int i = 0;

    line[0] = '\0';

    while (label[i]) {
        char test_line[64];
        char c = label[i];
        int test_idx;
        int test_w;

        if (line_idx == 0 && c == ' ') {
            i++;
            continue;
        }

        test_idx = 0;
        for (int j = 0; j < line_idx && test_idx < 63; j++)
            test_line[test_idx++] = line[j];
        if (test_idx < 63)
            test_line[test_idx++] = c;
        test_line[test_idx] = '\0';

        test_w = bmf_measure_text(&font_n, 10, test_line);

        if (test_w > max_line_width && line_idx > 0) {
            int split_at = -1;
            line[line_idx] = '\0';

            if (max_lines > 0 && line_count == max_lines - 1) {
                label_add_ellipsis(line, max_line_width);
                label_draw_line(line, text_y, draw);
                return max_lines;
            }

            for (int j = line_idx - 1; j > 0; j--) {
                if (line[j] == ' ') {
                    split_at = j;
                    break;
                }
            }

            if (split_at > 0) {
                char rest[64];
                int rest_idx = 0;

                line[split_at] = '\0';
                for (int j = split_at + 1; j < line_idx && rest_idx < 63; j++)
                    rest[rest_idx++] = line[j];
                rest[rest_idx] = '\0';

                line_count++;
                label_draw_line(line, text_y, draw);
                text_y += 11;

                line_idx = 0;
                for (int j = 0; rest[j] && line_idx < 63; j++)
                    line[line_idx++] = rest[j];
                continue;
            }

            line_count++;
            label_draw_line(line, text_y, draw);
            text_y += 11;
            line_idx = 0;
            continue;
        }

        if (line_idx < 63)
            line[line_idx++] = c;
        i++;
    }

    if (line_idx > 0) {
        line[line_idx] = '\0';
        line_count++;
        label_draw_line(line, text_y, draw);
    }

    return line_count > 0 ? line_count : 1;
}

int icon_count_label_lines(const char *label, int max_line_width) {
    return label_wordwrap_process(label, max_line_width, NULL);
}

int icon_count_label_lines_limited(const char *label, int max_line_width,
                                   int max_lines) {
    char lines[2][64];
    if (max_lines != 2)
        return icon_count_label_lines(label, max_line_width);
    return label_prepare_two_lines(label, max_line_width, lines);
}

int icon_calc_total_height(int icon_size, int label_lines) {
    return icon_size + 5 + label_lines * 11 + 2;
}

void icon_draw_label_wrapped(const char *label, int x, int y, int total_width,
                             int max_line_width, uint8_t color) {
    label_draw_params_t params = { x, y, total_width, color, 0 };
    label_wordwrap_process(label, max_line_width, &params);
}

void icon_draw_label_wrapped_limit(const char *label, int x, int y,
                                   int total_width, int max_line_width,
                                   uint8_t color, int max_lines) {
    if (max_lines == 2) {
        char lines[2][64];
        int count = label_prepare_two_lines(label, max_line_width, lines);
        label_draw_params_t params = { x, y, total_width, color, max_lines };
        label_draw_line(lines[0], y, &params);
        if (count > 1)
            label_draw_line(lines[1], y + 11, &params);
    } else {
        label_draw_params_t params = { x, y, total_width, color, max_lines };
        label_wordwrap_process(label, max_line_width, &params);
    }
}
