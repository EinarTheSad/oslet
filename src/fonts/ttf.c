#include "ttf.h"
#include "../drivers/fat32.h"
#include "../mem/heap.h"
#include "../console.h"
#include "../drivers/graphics.h"

static ttf_font_t fonts[TTF_MAX_FONTS];

static inline uint16_t read_u16(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | p[1];
}

static inline int16_t read_i16(const uint8_t *p) {
    return (int16_t)read_u16(p);
}

static inline uint32_t read_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static uint8_t *find_table(uint8_t *data, uint32_t size, const char *tag) {
    if (size < 12) return NULL;
    
    uint16_t num_tables = read_u16(data + 4);
    uint8_t *table_dir = data + 12;
    
    for (uint16_t i = 0; i < num_tables; i++) {
        uint8_t *entry = table_dir + i * 16;
        if (entry[0] == tag[0] && entry[1] == tag[1] &&
            entry[2] == tag[2] && entry[3] == tag[3]) {
            uint32_t offset = read_u32(entry + 8);
            if (offset + 4 < size) return data + offset;
        }
    }
    return NULL;
}

static uint32_t get_glyph_index(ttf_font_t *font, uint16_t codepoint) {
    if (!font->cmap_table) return 0;
    
    uint8_t *cmap = font->cmap_table;
    uint16_t num_tables = read_u16(cmap + 2);
    
    uint8_t *subtable = NULL;
    for (uint16_t i = 0; i < num_tables; i++) {
        uint8_t *record = cmap + 4 + i * 8;
        uint16_t platform = read_u16(record);
        uint16_t encoding = read_u16(record + 2);
        
        if ((platform == 0 && encoding == 4) || (platform == 3 && encoding == 1)) {
            uint32_t offset = read_u32(record + 4);
            if (offset < font->data_size) {
                subtable = cmap + offset;
                break;
            }
        }
    }
    
    if (!subtable) return 0;
    
    uint16_t format = read_u16(subtable);
    if (format != 4) return 0;
    
    uint16_t seg_count_x2 = read_u16(subtable + 6);
    uint16_t seg_count = seg_count_x2 / 2;
    
    uint8_t *end_code = subtable + 14;
    uint8_t *start_code = end_code + seg_count_x2 + 2;
    uint8_t *id_delta = start_code + seg_count_x2;
    uint8_t *id_range_ptr = id_delta + seg_count_x2;
    
    for (uint16_t i = 0; i < seg_count; i++) {
        uint16_t end = read_u16(end_code + i * 2);
        if (codepoint > end) continue;
        
        uint16_t start = read_u16(start_code + i * 2);
        if (codepoint < start) break;
        
        int16_t delta = read_i16(id_delta + i * 2);
        uint16_t range = read_u16(id_range_ptr + i * 2);
        
        if (range == 0) {
            return (codepoint + delta) & 0xFFFF;
        } else {
            uint8_t *glyph_ptr = id_range_ptr + i * 2 + range + (codepoint - start) * 2;
            uint16_t glyph_id = read_u16(glyph_ptr);
            if (glyph_id != 0) {
                return (glyph_id + delta) & 0xFFFF;
            }
        }
    }
    
    return 0;
}

static uint8_t *get_glyph_data(ttf_font_t *font, uint32_t glyph_idx) {
    if (glyph_idx >= font->num_glyphs) return NULL;
    if (!font->loca_table || !font->glyf_table) return NULL;
    
    uint32_t offset = font->loca_table[glyph_idx];
    uint32_t next_offset = font->loca_table[glyph_idx + 1];
    
    if (offset == next_offset || offset >= font->data_size) return NULL;
    
    return font->glyf_table + offset;
}

typedef struct {
    float x, y;
    uint8_t on_curve;
} ttf_point_t;

typedef struct {
    float x, y;
} vec2_t;

/* Dodaj punkt do listy segmentów */
static void add_segment_point(vec2_t *segments, int *count, int max, float x, float y) {
    if (*count < max) {
        segments[*count].x = x;
        segments[*count].y = y;
        (*count)++;
    }
}

/* Interpolacja krzywej kwadratowej Béziera */
static void flatten_bezier(vec2_t *segments, int *count, int max,
                           float x0, float y0, float x1, float y1, float x2, float y2) {
    /* Dziel krzywą na proste segmenty */
    int steps = 8;
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        float t1 = 1.0f - t;
        float x = t1 * t1 * x0 + 2.0f * t1 * t * x1 + t * t * x2;
        float y = t1 * t1 * y0 + 2.0f * t1 * t * y1 + t * t * y2;
        add_segment_point(segments, count, max, x, y);
    }
}

/* Ulepszone rasteryzowanie z antyaliasingiem */
static void rasterize_glyph_aa(uint8_t *glyph_data, float scale,
                               int16_t x_min, int16_t y_min,
                               int16_t x_max, int16_t y_max,
                               ttf_glyph_t *out) {
    
    int16_t num_contours = read_i16(glyph_data);
    if (num_contours <= 0) return;
    
    int gw = (int)((x_max - x_min) * scale) + 2;
    int gh = (int)((y_max - y_min) * scale) + 2;
    
    if (gw <= 0 || gh <= 0 || gw > 256 || gh > 256) return;
    
    uint8_t *bitmap = kmalloc(gw * gh);
    if (!bitmap) return;
    memset_s(bitmap, 0, gw * gh);
    
    /* Supersampling 4x dla AA */
    int ss_w = gw * 4;
    int ss_h = gh * 4;
    uint8_t *ss_bitmap = kmalloc(ss_w * ss_h);
    if (!ss_bitmap) {
        kfree(bitmap);
        return;
    }
    memset_s(ss_bitmap, 0, ss_w * ss_h);
    float ss_scale = scale * 4.0f;
    
    /* Parse contours */
    uint8_t *ptr = glyph_data + 10;
    uint8_t *end_pts = ptr;
    
    uint16_t last_pt = 0;
    for (int16_t i = 0; i < num_contours; i++) {
        uint16_t ep = read_u16(ptr + i * 2);
        if (ep > last_pt) last_pt = ep;
    }
    uint16_t total_pts = last_pt + 1;
    
    ptr += num_contours * 2;
    uint16_t instr_len = read_u16(ptr);
    ptr += 2 + instr_len;
    
    ttf_point_t *points = kmalloc(total_pts * sizeof(ttf_point_t));
    uint8_t *flags = kmalloc(total_pts);
    
    if (!points || !flags) {
        if (points) kfree(points);
        if (flags) kfree(flags);
        kfree(ss_bitmap);
        kfree(bitmap);
        return;
    }
    
    /* Read flags */
    for (uint16_t i = 0; i < total_pts; i++) {
        uint8_t flag = *ptr++;
        flags[i] = flag;
        points[i].on_curve = (flag & 1);
        
        if (flag & 8) {
            uint8_t repeat = *ptr++;
            for (uint8_t j = 0; j < repeat && i + 1 < total_pts; j++) {
                flags[++i] = flag;
                points[i].on_curve = (flag & 1);
            }
        }
    }
    
    /* Read X coordinates */
    int16_t x = 0;
    for (uint16_t i = 0; i < total_pts; i++) {
        uint8_t flag = flags[i];
        if (flag & 2) {
            uint8_t dx = *ptr++;
            x += (flag & 16) ? dx : -dx;
        } else if (!(flag & 16)) {
            x += read_i16(ptr);
            ptr += 2;
        }
        points[i].x = (x - x_min) * ss_scale;
    }
    
    /* Read Y coordinates */
    int16_t y = 0;
    for (uint16_t i = 0; i < total_pts; i++) {
        uint8_t flag = flags[i];
        if (flag & 4) {
            uint8_t dy = *ptr++;
            y += (flag & 32) ? dy : -dy;
        } else if (!(flag & 32)) {
            y += read_i16(ptr);
            ptr += 2;
        }
        points[i].y = (y_max - y) * ss_scale;
    }
    
    /* Konwertuj kontury na liniowe segmenty (rozwiń krzywe) */
    vec2_t *segments = kmalloc(4096 * sizeof(vec2_t));
    int *segment_starts = kmalloc(num_contours * sizeof(int));
    int *segment_counts = kmalloc(num_contours * sizeof(int));
    
    if (!segments || !segment_starts || !segment_counts) {
        if (segments) kfree(segments);
        if (segment_starts) kfree(segment_starts);
        if (segment_counts) kfree(segment_counts);
        kfree(flags);
        kfree(points);
        kfree(ss_bitmap);
        kfree(bitmap);
        return;
    }
    
    int total_segments = 0;
    uint16_t pt_idx = 0;
    
    for (int16_t c = 0; c < num_contours; c++) {
        uint16_t pt_end = read_u16(end_pts + c * 2);
        uint16_t pt_count = pt_end - pt_idx + 1;
        
        segment_starts[c] = total_segments;
        int seg_count = 0;
        
        for (uint16_t i = 0; i < pt_count; i++) {
            uint16_t curr = pt_idx + i;
            uint16_t next = pt_idx + ((i + 1) % pt_count);
            
            if (points[curr].on_curve) {
                if (points[next].on_curve) {
                    /* Prosta linia: on -> on */
                    add_segment_point(segments, &total_segments, 4096,
                                    points[curr].x, points[curr].y);
                    seg_count++;
                } else {
                    /* Początek krzywej: on -> off */
                    uint16_t next2 = pt_idx + ((i + 2) % pt_count);
                    
                    float x0 = points[curr].x;
                    float y0 = points[curr].y;
                    float x1 = points[next].x;
                    float y1 = points[next].y;
                    float x2, y2;
                    
                    if (points[next2].on_curve) {
                        /* off -> on */
                        x2 = points[next2].x;
                        y2 = points[next2].y;
                    } else {
                        /* off -> off: wstaw implied on-curve point */
                        x2 = (x1 + points[next2].x) * 0.5f;
                        y2 = (y1 + points[next2].y) * 0.5f;
                    }
                    
                    int before = total_segments;
                    flatten_bezier(segments, &total_segments, 4096,
                                 x0, y0, x1, y1, x2, y2);
                    seg_count += (total_segments - before);
                }
            }
        }
        
        segment_counts[c] = seg_count;
        pt_idx = pt_end + 1;
    }
    
    /* Scanline rasterization */
    for (int py = 0; py < ss_h; py++) {
        float scan_y = py + 0.5f;
        
        float intersections[512];
        int num_intersections = 0;
        
        int seg_idx = 0;
        for (int16_t c = 0; c < num_contours; c++) {
            int start = segment_starts[c];
            int count = segment_counts[c];
            
            for (int i = 0; i < count; i++) {
                int curr = start + i;
                int next = start + ((i + 1) % count);
                
                if (curr >= 4096 || next >= 4096) continue;
                
                float y0 = segments[curr].y;
                float y1 = segments[next].y;
                
                if ((y0 <= scan_y && y1 > scan_y) || 
                    (y1 <= scan_y && y0 > scan_y)) {
                    float x0 = segments[curr].x;
                    float x1 = segments[next].x;
                    
                    float t = (scan_y - y0) / (y1 - y0);
                    float x_int = x0 + t * (x1 - x0);
                    
                    if (num_intersections < 512) {
                        intersections[num_intersections++] = x_int;
                    }
                }
            }
        }
        
        /* Sort intersections */
        for (int i = 0; i < num_intersections - 1; i++) {
            for (int j = i + 1; j < num_intersections; j++) {
                if (intersections[j] < intersections[i]) {
                    float tmp = intersections[i];
                    intersections[i] = intersections[j];
                    intersections[j] = tmp;
                }
            }
        }
        
        /* Fill (even-odd) */
        for (int i = 0; i < num_intersections - 1; i += 2) {
            int x_start = (int)(intersections[i] + 0.5f);
            int x_end = (int)(intersections[i + 1] + 0.5f);
            
            if (x_start < 0) x_start = 0;
            if (x_end > ss_w) x_end = ss_w;
            
            for (int px = x_start; px < x_end; px++) {
                ss_bitmap[py * ss_w + px] = 0xFF;
            }
        }
    }
    
    kfree(segments);
    kfree(segment_starts);
    kfree(segment_counts);
    kfree(flags);
    kfree(points);
    
    /* Downsample 4x4 -> 1x1 */
    for (int y = 0; y < gh; y++) {
        for (int x = 0; x < gw; x++) {
            int sum = 0;
            for (int sy = 0; sy < 4; sy++) {
                for (int sx = 0; sx < 4; sx++) {
                    int ss_x = x * 4 + sx;
                    int ss_y = y * 4 + sy;
                    if (ss_x < ss_w && ss_y < ss_h) {
                        sum += ss_bitmap[ss_y * ss_w + ss_x] ? 1 : 0;
                    }
                }
            }
            bitmap[y * gw + x] = (sum * 255) / 16;
        }
    }
    
    kfree(ss_bitmap);
    
    out->bitmap = bitmap;
    out->width = gw;
    out->height = gh;
}

void ttf_init(void) {
    memset_s(fonts, 0, sizeof(fonts));
}

ttf_font_t *ttf_load(const char *path) {
    ttf_font_t *font = NULL;
    for (int i = 0; i < TTF_MAX_FONTS; i++) {
        if (!fonts[i].in_use) {
            font = &fonts[i];
            break;
        }
    }
    
    if (!font) return NULL;
    
    fat32_file_t *f = fat32_open(path, "r");
    if (!f) return NULL;
    
    uint32_t size = f->size;
    if (size < 12 || size > 10 * 1024 * 1024) {
        fat32_close(f);
        return NULL;
    }
    
    uint8_t *data = kmalloc(size);
    if (!data) {
        fat32_close(f);
        return NULL;
    }
    
    if (fat32_read(f, data, size) != (int)size) {
        kfree(data);
        fat32_close(f);
        return NULL;
    }
    
    fat32_close(f);
    
    memset_s(font, 0, sizeof(ttf_font_t));
    font->data = data;
    font->data_size = size;
    
    uint8_t *head = find_table(data, size, "head");
    if (!head) {
        kfree(data);
        return NULL;
    }
    
    font->head_table = head;
    font->units_per_em = read_u16(head + 18);
    
    uint8_t *hhea = find_table(data, size, "hhea");
    if (hhea) {
        font->ascent = read_i16(hhea + 4);
        font->descent = read_i16(hhea + 6);
        font->line_gap = read_i16(hhea + 8);
    }
    
    uint8_t *maxp = find_table(data, size, "maxp");
    if (maxp) {
        font->num_glyphs = read_u16(maxp + 4);
    }
    
    font->cmap_table = find_table(data, size, "cmap");
    font->glyf_table = find_table(data, size, "glyf");
    font->hmtx_table = find_table(data, size, "hmtx");
    
    uint8_t *loca = find_table(data, size, "loca");
    if (loca && font->num_glyphs > 0) {
        int16_t format = read_i16(head + 50);
        font->loca_table = kmalloc((font->num_glyphs + 1) * sizeof(uint32_t));
        
        if (font->loca_table) {
            if (format == 0) {
                for (uint32_t i = 0; i <= font->num_glyphs; i++) {
                    font->loca_table[i] = read_u16(loca + i * 2) * 2;
                }
            } else {
                for (uint32_t i = 0; i <= font->num_glyphs; i++) {
                    font->loca_table[i] = read_u32(loca + i * 4);
                }
            }
        }
    }
    
    strcpy_s(font->name, path, sizeof(font->name));
    font->in_use = 1;
    
    return font;
}

void ttf_free(ttf_font_t *font) {
    if (!font || !font->in_use) return;
    
    for (uint32_t i = 0; i < TTF_GLYPH_CACHE_SIZE; i++) {
        if (font->cache[i].glyph.bitmap) {
            kfree(font->cache[i].glyph.bitmap);
        }
    }
    
    if (font->loca_table) kfree(font->loca_table);
    if (font->data) kfree(font->data);
    
    memset_s(font, 0, sizeof(ttf_font_t));
}

ttf_glyph_t *ttf_render_glyph(ttf_font_t *font, uint16_t codepoint, uint8_t size) {
    if (!font || !font->in_use) return NULL;
    
    /* Check cache */
    for (uint32_t i = 0; i < TTF_GLYPH_CACHE_SIZE; i++) {
        if (font->cache[i].codepoint == codepoint &&
            font->cache[i].size == size &&
            font->cache[i].glyph.bitmap) {
            return &font->cache[i].glyph;
        }
    }
    
    uint32_t glyph_idx = get_glyph_index(font, codepoint);
    uint8_t *glyph_data = get_glyph_data(font, glyph_idx);
    
    ttf_cache_entry_t *entry = &font->cache[font->cache_head];
    font->cache_head = (font->cache_head + 1) % TTF_GLYPH_CACHE_SIZE;
    
    if (entry->glyph.bitmap) {
        kfree(entry->glyph.bitmap);
    }
    
    memset_s(&entry->glyph, 0, sizeof(ttf_glyph_t));
    entry->codepoint = codepoint;
    entry->size = size;
    
    float scale = (float)size / (float)font->units_per_em;
    
    if (!glyph_data) {
        entry->glyph.advance = (int)(size * 0.5f);
        return &entry->glyph;
    }
    
    int16_t num_contours = read_i16(glyph_data);
    int16_t x_min = read_i16(glyph_data + 2);
    int16_t y_min = read_i16(glyph_data + 4);
    int16_t x_max = read_i16(glyph_data + 6);
    int16_t y_max = read_i16(glyph_data + 8);
    
    entry->glyph.bearing_x = (int)(x_min * scale + 0.5f);
    entry->glyph.bearing_y = (int)(y_max * scale + 0.5f);
    
    if (font->hmtx_table && glyph_idx < font->num_glyphs) {
        uint16_t advance = read_u16(font->hmtx_table + glyph_idx * 4);
        entry->glyph.advance = (int)(advance * scale + 0.5f);
    } else {
        entry->glyph.advance = (int)((x_max - x_min) * scale + 0.5f);
    }
    
    if (num_contours > 0) {
        rasterize_glyph_aa(glyph_data, scale, x_min, y_min, x_max, y_max,
                          &entry->glyph);
    }
    
    return &entry->glyph;
}

int ttf_get_kerning(ttf_font_t *font, uint16_t left, uint16_t right, uint8_t size) {
    (void)font; (void)left; (void)right; (void)size;
    return 0;
}

void ttf_print(int x, int y, const char *text, ttf_font_t *font, uint8_t size, uint8_t color) {
    if (!text || !font || !font->in_use) return;
    
    if (!gfx_is_active()) return;
    
    uint8_t *backbuf = gfx_get_backbuffer();
    if (!backbuf) return;
    
    int baseline = (int)(font->ascent * size / font->units_per_em + 0.5f);
    int cx = x;
    int cy = y;
    
    while (*text) {
        if (*text == '\n') {
            cx = x;
            cy += size + 2;
            text++;
            continue;
        }
        
        if (*text == ' ') {
            cx += size / 2;
            text++;
            continue;
        }
        
        uint16_t codepoint = (uint8_t)*text;
        ttf_glyph_t *glyph = ttf_render_glyph(font, codepoint, size);
        
        if (glyph && glyph->bitmap && glyph->width > 0 && glyph->height > 0) {
            int draw_x = cx + glyph->bearing_x;
            int draw_y = cy + baseline - glyph->bearing_y;
            
            for (int gy = 0; gy < glyph->height; gy++) {
                for (int gx = 0; gx < glyph->width; gx++) {
                    uint8_t alpha = glyph->bitmap[gy * glyph->width + gx];
                    
                    if (alpha < 32) continue;
                    
                    int px = draw_x + gx;
                    int py = draw_y + gy;
                    
                    if (px >= 0 && px < GFX_WIDTH && py >= 0 && py < GFX_HEIGHT) {
                        uint32_t offset = py * (GFX_WIDTH / 2) + (px / 2);
                        
                        if (alpha > 223) {
                            /* Pełne pokrycie - po prostu narysuj */
                            if (px & 1) {
                                backbuf[offset] = (backbuf[offset] & 0xF0) | (color & 0x0F);
                            } else {
                                backbuf[offset] = (backbuf[offset] & 0x0F) | ((color & 0x0F) << 4);
                            }
                        } else {
                            /* Alpha blending */
                            uint8_t old_pixel;
                            if (px & 1) {
                                old_pixel = backbuf[offset] & 0x0F;
                            } else {
                                old_pixel = (backbuf[offset] >> 4) & 0x0F;
                            }
                            
                            uint8_t blend = ((color & 0x0F) * alpha + old_pixel * (255 - alpha)) / 255;
                            
                            if (px & 1) {
                                backbuf[offset] = (backbuf[offset] & 0xF0) | (blend & 0x0F);
                            } else {
                                backbuf[offset] = (backbuf[offset] & 0x0F) | ((blend & 0x0F) << 4);
                            }
                        }
                    }
                }
            }
        }
        
        if (glyph) cx += glyph->advance;
        text++;
    }
}