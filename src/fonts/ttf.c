#include "ttf.h"
#include "../drivers/fat32.h"
#include "../mem/heap.h"
#include "../console.h"
#include "../drivers/graphics.h"

static ttf_font_t fonts[TTF_MAX_FONTS];
static fat32_file_t* debug_log = NULL;

static void log_debug(const char* msg) {
    if (!debug_log) {
        debug_log = fat32_open("C:/ttf_debug.log", "w");
    }
    if (debug_log) {
        fat32_write(debug_log, msg, strlen_s(msg));
    }
}

static void log_close(void) {
    if (debug_log) {
        fat32_close(debug_log);
        debug_log = NULL;
    }
}

static inline uint16_t read_u16(const uint8_t* p) {
    return ((uint16_t)p[0] << 8) | p[1];
}

static inline int16_t read_i16(const uint8_t* p) {
    return (int16_t)read_u16(p);
}

static inline uint32_t read_u32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static uint8_t* find_table(uint8_t* data, uint32_t size, const char* tag) {
    if (size < 12) return NULL;
    uint32_t num_tables = read_u16(data + 4);
    
    uint8_t* table_dir = data + 12;
    for (uint32_t i = 0; i < num_tables; i++) {
        uint8_t* entry = table_dir + i * 16;
        if (entry[0] == tag[0] && entry[1] == tag[1] &&
            entry[2] == tag[2] && entry[3] == tag[3]) {
            uint32_t offset = read_u32(entry + 8);
            if (offset < size) return data + offset;
        }
    }
    return NULL;
}

static uint32_t get_glyph_index(ttf_font_t* font, uint16_t codepoint) {
    if (!font->cmap_table) return 0;
    
    uint8_t* cmap = font->cmap_table;
    uint16_t num_tables = read_u16(cmap + 2);
    
    uint8_t* subtable = NULL;
    for (uint16_t i = 0; i < num_tables; i++) {
        uint8_t* record = cmap + 4 + i * 8;
        uint16_t platform = read_u16(record);
        uint16_t encoding = read_u16(record + 2);
        
        if ((platform == 0) || (platform == 3 && encoding == 1)) {
            uint32_t offset = read_u32(record + 4);
            subtable = cmap + offset;
            break;
        }
    }
    
    if (!subtable) return 0;
    
    uint16_t format = read_u16(subtable);
    
    if (format == 4) {
        uint16_t seg_count = read_u16(subtable + 6) / 2;
        uint8_t* end_code = subtable + 14;
        uint8_t* start_code = end_code + seg_count * 2 + 2;
        uint8_t* id_delta = start_code + seg_count * 2;
        uint8_t* id_range = id_delta + seg_count * 2;
        
        for (uint16_t i = 0; i < seg_count; i++) {
            uint16_t end = read_u16(end_code + i * 2);
            uint16_t start = read_u16(start_code + i * 2);
            
            if (codepoint >= start && codepoint <= end) {
                int16_t delta = read_i16(id_delta + i * 2);
                uint16_t range = read_u16(id_range + i * 2);
                
                if (range == 0) {
                    return (codepoint + delta) & 0xFFFF;
                } else {
                    uint8_t* glyph_ptr = id_range + i * 2 + range + (codepoint - start) * 2;
                    uint16_t glyph = read_u16(glyph_ptr);
                    if (glyph != 0) return (glyph + delta) & 0xFFFF;
                }
            }
        }
    }
    
    return 0;
}

static uint8_t* get_glyph_data(ttf_font_t* font, uint32_t glyph_idx) {
    if (glyph_idx >= font->num_glyphs) return NULL;
    if (!font->loca_table || !font->glyf_table) return NULL;
    
    uint32_t offset = font->loca_table[glyph_idx];
    uint32_t next_offset = font->loca_table[glyph_idx + 1];
    
    if (offset == next_offset) return NULL;
    
    return font->glyf_table + offset;
}

typedef struct {
    int16_t x, y;
    uint8_t on_curve;
} ttf_point_t;

static void rasterize_simple_glyph(uint8_t* glyph_data, float scale, 
                                   int16_t bbox_x_min, int16_t bbox_y_min,
                                   int16_t bbox_x_max, int16_t bbox_y_max,
                                   ttf_glyph_t* out) {
    
    char buf[128];
    
    int16_t num_contours = read_i16(glyph_data);
    if (num_contours <= 0) {
        log_debug("    raster: no contours\n");
        return;
    }
    
    int gw = (int)((bbox_x_max - bbox_x_min) * scale) + 4;
    int gh = (int)((bbox_y_max - bbox_y_min) * scale) + 4;
    
    snprintf(buf, sizeof(buf), "    raster: target=%dx%d\n", gw, gh);
    log_debug(buf);
    
    if (gw <= 0 || gh <= 0 || gw > 128 || gh > 128) {
        log_debug("    raster: invalid size\n");
        return;
    }
    
    out->bitmap = kmalloc(gw * gh);
    if (!out->bitmap) {
        log_debug("    raster: kmalloc FAILED\n");
        return;
    }
    
    memset_s(out->bitmap, 0, gw * gh);
    out->width = gw;
    out->height = gh;
    
    log_debug("    raster: bitmap allocated\n");
    
    uint8_t* ptr = glyph_data + 10;
    uint8_t* end_pts_data = ptr;
    
    uint16_t last_pt = 0;
    for (int16_t i = 0; i < num_contours; i++) {
        uint16_t ep = read_u16(ptr + i * 2);
        if (ep > last_pt) last_pt = ep;
    }
    uint16_t total_pts = last_pt + 1;
    
    ptr += num_contours * 2;
    uint16_t instr_len = read_u16(ptr);
    ptr += 2 + instr_len;
    
    ttf_point_t* points = kmalloc(total_pts * sizeof(ttf_point_t));
    if (!points) {
        kfree(out->bitmap);
        out->bitmap = NULL;
        return;
    }
    
    uint8_t* flags = kmalloc(total_pts);
    if (!flags) {
        kfree(points);
        kfree(out->bitmap);
        out->bitmap = NULL;
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
    int16_t x_coord = 0;
    for (uint16_t i = 0; i < total_pts; i++) {
        uint8_t flag = flags[i];
        if (flag & 2) {
            uint8_t dx = *ptr++;
            x_coord += (flag & 16) ? dx : -dx;
        } else if (!(flag & 16)) {
            x_coord += read_i16(ptr);
            ptr += 2;
        }
        points[i].x = (int16_t)((x_coord - bbox_x_min) * scale + 2);
    }
    
    /* Read Y coordinates */
    int16_t y_coord = 0;
    for (uint16_t i = 0; i < total_pts; i++) {
        uint8_t flag = flags[i];
        if (flag & 4) {
            uint8_t dy = *ptr++;
            y_coord += (flag & 32) ? dy : -dy;
        } else if (!(flag & 32)) {
            y_coord += read_i16(ptr);
            ptr += 2;
        }
        points[i].y = (int16_t)((bbox_y_max - y_coord) * scale + 2);
    }
    
    /* Scanline fill */
    for (int py = 0; py < gh; py++) {
        uint16_t pt_start = 0;
        
        for (int16_t c = 0; c < num_contours; c++) {
            uint16_t pt_end = read_u16(end_pts_data + c * 2);
            
            for (uint16_t i = pt_start; i <= pt_end; i++) {
                uint16_t j = (i == pt_end) ? pt_start : (i + 1);
                
                int16_t y0 = points[i].y;
                int16_t y1 = points[j].y;
                int16_t x0 = points[i].x;
                int16_t x1 = points[j].x;
                
                if ((y0 <= py && y1 > py) || (y1 <= py && y0 > py)) {
                    if (y1 != y0) {
                        int x_cross = x0 + ((py - y0) * (x1 - x0)) / (y1 - y0);
                        
                        if (x_cross >= 0 && x_cross < gw) {
                            for (int px = x_cross; px < gw; px++) {
                                out->bitmap[py * gw + px] ^= 0xFF;
                            }
                        }
                    }
                }
            }
            
            pt_start = pt_end + 1;
        }
    }
    
    snprintf(buf, sizeof(buf), "    raster: fill complete\n");
    log_debug(buf);
    
    kfree(flags);
    kfree(points);
}

void ttf_init(void) {
    memset_s(fonts, 0, sizeof(fonts));
}

ttf_font_t* ttf_load(const char* path) {
    ttf_font_t* font = NULL;
    for (int i = 0; i < TTF_MAX_FONTS; i++) {
        if (!fonts[i].in_use) {
            font = &fonts[i];
            break;
        }
    }
    
    if (!font) return NULL;
    
    fat32_file_t* f = fat32_open(path, "r");
    if (!f) return NULL;
    
    uint32_t size = f->size;
    if (size < 12 || size > 10 * 1024 * 1024) {
        fat32_close(f);
        return NULL;
    }
    
    uint8_t* data = kmalloc(size);
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
    
    uint8_t* head = find_table(data, size, "head");
    if (!head) {
        kfree(data);
        return NULL;
    }
    
    font->units_per_em = read_u16(head + 18);
    
    uint8_t* hhea = find_table(data, size, "hhea");
    if (hhea) {
        font->ascent = read_i16(hhea + 4);
        font->descent = read_i16(hhea + 6);
        font->line_gap = read_i16(hhea + 8);
    }
    
    uint8_t* maxp = find_table(data, size, "maxp");
    if (maxp) {
        font->num_glyphs = read_u16(maxp + 4);
    }
    
    font->cmap_table = find_table(data, size, "cmap");
    font->glyf_table = find_table(data, size, "glyf");
    font->hmtx_table = find_table(data, size, "hmtx");
    
    uint8_t* loca = find_table(data, size, "loca");
    if (loca && maxp) {
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

void ttf_free(ttf_font_t* font) {
    if (!font) return;
    
    for (uint32_t i = 0; i < TTF_GLYPH_CACHE_SIZE; i++) {
        if (font->cache[i].glyph.bitmap) {
            kfree(font->cache[i].glyph.bitmap);
        }
    }
    
    if (font->loca_table) kfree(font->loca_table);
    if (font->data) kfree(font->data);
    
    memset_s(font, 0, sizeof(ttf_font_t));
}

ttf_glyph_t* ttf_render_glyph(ttf_font_t* font, uint16_t codepoint, uint8_t size) {
    if (!font || !font->in_use) {
        log_debug("ERROR: font invalid\n");
        return NULL;
    }
    
    /* Check cache */
    for (uint32_t i = 0; i < TTF_GLYPH_CACHE_SIZE; i++) {
        if (font->cache[i].codepoint == codepoint && 
            font->cache[i].size == size &&
            font->cache[i].glyph.bitmap) {
            return &font->cache[i].glyph;
        }
    }
    
    char buf[128];
    snprintf(buf, sizeof(buf), "Rendering U+%04X size=%d\n", codepoint, size);
    log_debug(buf);
    
    uint32_t glyph_idx = get_glyph_index(font, codepoint);
    snprintf(buf, sizeof(buf), "  glyph_idx=%u\n", glyph_idx);
    log_debug(buf);
    
    uint8_t* glyph_data = get_glyph_data(font, glyph_idx);
    if (!glyph_data) {
        log_debug("  ERROR: no glyph data\n");
    }
    
    ttf_cache_entry_t* entry = &font->cache[font->cache_head];
    font->cache_head = (font->cache_head + 1) % TTF_GLYPH_CACHE_SIZE;
    
    if (entry->glyph.bitmap) {
        kfree(entry->glyph.bitmap);
        memset_s(&entry->glyph, 0, sizeof(ttf_glyph_t));
    }
    
    entry->codepoint = codepoint;
    entry->size = size;
    
    if (!glyph_data) {
        entry->glyph.advance = size / 2;
        snprintf(buf, sizeof(buf), "  fallback advance=%d\n", entry->glyph.advance);
        log_debug(buf);
        return &entry->glyph;
    }
    
    int16_t num_contours = read_i16(glyph_data);
    int16_t glyph_x_min = read_i16(glyph_data + 2);
    int16_t glyph_y_min = read_i16(glyph_data + 4);
    int16_t glyph_x_max = read_i16(glyph_data + 6);
    int16_t glyph_y_max = read_i16(glyph_data + 8);
    
    snprintf(buf, sizeof(buf), "  contours=%d bbox=(%d,%d,%d,%d)\n", 
             num_contours, glyph_x_min, glyph_y_min, glyph_x_max, glyph_y_max);
    log_debug(buf);
    
    float scale = (float)size / (float)font->units_per_em;
    
    entry->glyph.bearing_x = (int)(glyph_x_min * scale);
    entry->glyph.bearing_y = (int)(glyph_y_max * scale);
    
    if (font->hmtx_table && glyph_idx < font->num_glyphs) {
        uint16_t advance = read_u16(font->hmtx_table + glyph_idx * 4);
        entry->glyph.advance = (int)(advance * scale);
    } else {
        entry->glyph.advance = (int)((glyph_x_max - glyph_x_min) * scale);
    }
    
    snprintf(buf, sizeof(buf), "  bearing=(%d,%d) advance=%d\n",
             entry->glyph.bearing_x, entry->glyph.bearing_y, entry->glyph.advance);
    log_debug(buf);
    
    if (num_contours > 0) {
        rasterize_simple_glyph(glyph_data, scale, 
                              glyph_x_min, glyph_y_min,
                              glyph_x_max, glyph_y_max,
                              &entry->glyph);
        
        if (entry->glyph.bitmap) {
            snprintf(buf, sizeof(buf), "  SUCCESS: %dx%d bitmap\n", 
                     entry->glyph.width, entry->glyph.height);
            log_debug(buf);
        } else {
            log_debug("  ERROR: rasterization failed\n");
        }
    }
    
    log_close();
    return &entry->glyph;
}

int ttf_get_kerning(ttf_font_t* font, uint16_t left, uint16_t right, uint8_t size) {
    (void)font; (void)left; (void)right; (void)size;
    return 0;
}

void ttf_print(int x, int y, const char* text, ttf_font_t* font, uint8_t size, uint8_t color) {
    if (!text) return;
    
    char buf[128];
    snprintf(buf, sizeof(buf), "ttf_print: x=%d y=%d size=%d color=%d\n", x, y, size, color);
    log_debug(buf);
    
    uint8_t* backbuf = gfx_get_backbuffer();
    snprintf(buf, sizeof(buf), "ttf_print: backbuffer=%p\n", backbuf);
    log_debug(buf);
    
    if (!backbuf) {
        log_debug("ttf_print: ERROR - no backbuffer!\n");
        log_close();
        return;
    }
    
    if (!font || !font->in_use) {
        log_debug("ttf_print: font invalid, using fallback\n");
        if (gfx_is_active()) {
            gfx_print(x, y, text, color);
        }
        return;
    }
    
    if (!gfx_is_active()) {
        log_debug("ttf_print: graphics not active\n");
        return;
    }
    
    int cx = x;
    int cy = y;
    
    int baseline_offset = (int)(font->ascent * size / font->units_per_em);
    snprintf(buf, sizeof(buf), "ttf_print: baseline_offset=%d (ascent=%d, upm=%d)\n",
             baseline_offset, font->ascent, font->units_per_em);
    log_debug(buf);
    
    while (*text) {
        if (*text == '\n') {
            cx = x;
            cy += size + 4;
            text++;
            continue;
        }
        
        if (*text == ' ') {
            cx += size / 2;
            text++;
            continue;
        }
        
        uint16_t codepoint = (uint16_t)(uint8_t)*text;
        ttf_glyph_t* glyph = ttf_render_glyph(font, codepoint, size);
        
        if (!glyph) {
            log_debug("ttf_print: glyph is NULL\n");
            cx += size / 2;
            text++;
            continue;
        }
        
        snprintf(buf, sizeof(buf), "ttf_print: char '%c' bitmap=%p width=%d height=%d\n",
                 *text, glyph->bitmap, glyph->width, glyph->height);
        log_debug(buf);
        
        if (glyph->bitmap && glyph->width > 0 && glyph->height > 0) {
            int draw_x = cx + glyph->bearing_x;
            int draw_y = cy + baseline_offset - glyph->bearing_y;
            
            snprintf(buf, sizeof(buf), "ttf_print: drawing at (%d,%d) bearing=(%d,%d)\n",
                     draw_x, draw_y, glyph->bearing_x, glyph->bearing_y);
            log_debug(buf);
            
            int pixels_drawn = 0;
            
            /* Direct backbuffer write */
            for (int gy = 0; gy < glyph->height; gy++) {
                for (int gx = 0; gx < glyph->width; gx++) {
                    uint8_t pixel_val = glyph->bitmap[gy * glyph->width + gx];
                    
                    int px = draw_x + gx;
                    int py = draw_y + gy;
                    
                    if (px >= 0 && px < GFX_WIDTH && py >= 0 && py < GFX_HEIGHT && pixel_val) {
                        /* Direct write to backbuffer - bypass gfx_putpixel */
                        uint32_t offset = py * (GFX_WIDTH / 2) + (px / 2);
                        
                        if (px & 1) {
                            backbuf[offset] = (backbuf[offset] & 0xF0) | (color & 0x0F);
                        } else {
                            backbuf[offset] = (backbuf[offset] & 0x0F) | ((color & 0x0F) << 4);
                        }
                        
                        pixels_drawn++;
                    }
                }
            }
            
            snprintf(buf, sizeof(buf), "ttf_print: drew %d pixels directly to backbuffer\n", 
                     pixels_drawn);
            log_debug(buf);
        } else {
            log_debug("ttf_print: bitmap is NULL or zero size\n");
        }
        
        cx += glyph->advance;
        text++;
    }
    
    log_close();
}