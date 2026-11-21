#include "ini.h"
#include "string.h"
#include "stdlib.h"
#include "../syscall.h"

void ini_init(ini_parser_t *ini, char *buffer) {
    ini->data = buffer;
    ini->ptr = buffer;
    ini->section[0] = '\0';
}

static int ini_line_name(const char *line, int len, char *name, int section) {
    while (len && (*line == ' ' || *line == '\t')) { line++; len--; }
    if (section) {
        if (!len || *line++ != '[') return 0;
        len--;
    } else if (!len || *line == ';' || *line == '#' || *line == '[') {
        return 0;
    }
    int n = 0;
    while (len && *line != (section ? ']' : '=')) {
        if (n >= 63) return -1;
        name[n++] = *line++;
        len--;
    }
    if (!len) return 0;
    while (n && (name[n - 1] == ' ' || name[n - 1] == '\t')) n--;
    name[n] = '\0';
    return n ? 1 : 0;
}

static int ini_append(char *out, int size, int *used, const char *text, int len) {
    if (len < 0 || len >= size - *used) return -1;
    memcpy(out + *used, text, len);
    *used += len;
    out[*used] = '\0';
    return 0;
}

int ini_update_section(const char *data, const char *section, const char *new_text,
                       char *outbuf, int outbuf_size) {
    struct update { const char *line; int len; int found; char name[64]; } updates[32];
    int count = 0, used = 0, in_section = 0, seen_section = 0;
    if (!data || !section || !new_text || !outbuf || outbuf_size <= 0) return -1;
    outbuf[0] = '\0';
    for (const char *p = new_text; *p;) {
        const char *end = p;
        while (*end && *end != '\n') end++;
        int len = end - p;
        char name[64];
        int key = ini_line_name(p, len, name, 0);
        if (key < 0) return -1;
        if (key) {
            if (count == 32) return -1;
            updates[count].line = p;
            updates[count].len = len;
            updates[count].found = 0;
            strcpy(updates[count].name, name);
            count++;
        }
        p = *end ? end + 1 : end;
    }
    for (const char *p = data; *p;) {
        const char *end = p;
        while (*end && *end != '\n') end++;
        const char *next = *end ? end + 1 : end;
        int len = end - p;
        char name[64];
        int is_section = ini_line_name(p, len, name, 1);
        if (is_section < 0) return -1;
        if (is_section) {
            if (in_section) {
                for (int i = 0; i < count; i++) {
                    if (updates[i].found) continue;
                    if (used && outbuf[used - 1] != '\n' &&
                        ini_append(outbuf, outbuf_size, &used, "\n", 1) != 0) return -1;
                    if (ini_append(outbuf, outbuf_size, &used, updates[i].line, updates[i].len) != 0 ||
                        ini_append(outbuf, outbuf_size, &used, "\n", 1) != 0) return -1;
                    updates[i].found = 1;
                }
            }
            in_section = strcasecmp(name, section) == 0;
            if (in_section) seen_section = 1;
        } else if (in_section) {
            int key = ini_line_name(p, len, name, 0);
            if (key < 0) return -1;
            if (key) {
                for (int i = 0; i < count; i++) {
                    if (strcasecmp(name, updates[i].name) != 0) continue;
                    if (ini_append(outbuf, outbuf_size, &used, updates[i].line, updates[i].len) != 0 ||
                        ini_append(outbuf, outbuf_size, &used, "\n", 1) != 0) return -1;
                    updates[i].found = 1;
                    p = next;
                    goto next_line;
                }
            }
        }
        if (ini_append(outbuf, outbuf_size, &used, p, next - p) != 0) return -1;
        p = next;
next_line:;
    }
    if (!seen_section) {
        if (used && outbuf[used - 1] != '\n' &&
            ini_append(outbuf, outbuf_size, &used, "\n", 1) != 0) return -1;
        if (ini_append(outbuf, outbuf_size, &used, "[", 1) != 0 ||
            ini_append(outbuf, outbuf_size, &used, section, strlen(section)) != 0 ||
            ini_append(outbuf, outbuf_size, &used, "]\n", 2) != 0) return -1;
    }
    for (int i = 0; i < count; i++) {
        if (updates[i].found) continue;
        if (used && outbuf[used - 1] != '\n' &&
            ini_append(outbuf, outbuf_size, &used, "\n", 1) != 0) return -1;
        if (ini_append(outbuf, outbuf_size, &used, updates[i].line, updates[i].len) != 0 ||
            ini_append(outbuf, outbuf_size, &used, "\n", 1) != 0) return -1;
    }
    return used;
}

char *ini_read_file(const char *path) {
    const int capacity = 16384;
    char *data = malloc(capacity);
    if (!data) return NULL;
    int fd = sys_open(path, "r");
    if (fd < 0) {
        free(data);
        return NULL;
    }
    int used = 0, n = 0;
    while (used < capacity - 1) {
        n = sys_read(fd, data + used, capacity - 1 - used);
        if (n <= 0) break;
        used += n;
    }
    if (used == capacity - 1) {
        char extra;
        n = sys_read(fd, &extra, 1);
        if (n != 0) n = -1;
    }
    int closed = sys_close(fd);
    if (n < 0 || closed != 0) {
        free(data);
        return NULL;
    }
    for (int i = 0; i < used; i++) {
        if (!data[i]) {
            free(data);
            return NULL;
        }
    }
    data[used] = '\0';
    return data;
}

int ini_save_section(const char *path, const char *section, const char *new_text) {
    const int capacity = 16384;
    char *data = ini_read_file(path);
    char *out = malloc(capacity);
    if (!data || !out) {
        if (data) free(data);
        if (out) free(out);
        return -1;
    }
    int result = -1;
    int length = ini_update_section(data, section, new_text, out, capacity);
    if (length < 0) goto done;
    int fd = sys_open(path, "w");
    if (fd < 0) goto done;
    int n = sys_write_file(fd, out, length);
    int closed = sys_close(fd);
    if (n == length && closed == 0) result = 0;
done:
    free(data);
    free(out);
    return result;
}

/* Replace or insert a section while preserving the rest of the file. */
int ini_replace_section(const char *data, const char *section, const char *new_text, char *outbuf, int outbuf_size) {
    const char *src = data ? data : "";
    const char *p = src;
    const char *found_start = NULL;
    const char *found_end = NULL;

    while (*p) {
        const char *line = p;
        while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n') line++;
        if (*line == '[') {
            const char *q = line + 1;
            char name[128];
            int ni = 0;
            while (*q && *q != ']' && ni < (int)sizeof(name)-1) {
                name[ni++] = *q++;
            }
            name[ni] = '\0';
            if (*q == ']' && strcasecmp(name, section) == 0) {
                found_start = line;
                const char *r = q + 1;
                while (*r && *r != '\n') r++;
                if (*r == '\n') r++;
                while (*r) {
                    const char *s = r;
                    while (*s == ' ' || *s == '\t' || *s == '\r') s++;
                    if (*s == '[') {
                        found_end = r;
                        break;
                    }
                    while (*r && *r != '\n') r++;
                    if (*r == '\n') r++;
                }
                if (!found_end) found_end = r;
                break;
            }
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }

    int written = 0;
    int rem = outbuf_size;
    outbuf[0] = '\0';

    if (found_start) {
        int head_len = (int)(found_start - src);
        if (head_len >= rem) return -1;
        memcpy(outbuf + written, src, head_len);
        written += head_len;
        rem -= head_len;
    } else {
        int src_len = strlen(src);
        if (src_len >= rem) return -1;
        memcpy(outbuf + written, src, src_len);
        written += src_len;
        rem -= src_len;
    }

    if (written > 0) {
        if (outbuf[written-1] != '\n') {
            if (rem <= 1) return -1;
            outbuf[written++] = '\n';
            rem--;
        }
    }

    int new_len = strlen(new_text);
    if (new_len >= rem) return -1;
    memcpy(outbuf + written, new_text, new_len);
    written += new_len;
    rem -= new_len;

    if (found_start) {
        const char *tail = found_end;
        int tail_len = (int)strlen(tail);
        if (tail_len >= rem) return -1;
        memcpy(outbuf + written, tail, tail_len);
        written += tail_len;
        rem -= tail_len;
    }

    if (rem <= 0) return -1;
    outbuf[written] = '\0';

    /* Keep saved INI files as plain LF text so editors do not show CR bytes. */
    int clean = 0;
    for (int i = 0; i < written; i++) {
        if (outbuf[i] != '\r') {
            outbuf[clean++] = outbuf[i];
        }
    }
    outbuf[clean] = '\0';
    written = clean;

    return written;
}

static char* read_line(char *buf, char *line, int max_len, int *truncated) {
    int i = 0;
    if (truncated) *truncated = 0;
    while (*buf && *buf != '\n' && i < max_len - 1) {
        line[i++] = *buf++;
    }
    if (*buf && *buf != '\n' && truncated) *truncated = 1;
    line[i] = '\0';
    str_trim(line);

    if (*buf == '\n') buf++;
    return *buf ? buf : NULL;
}

int ini_next(ini_parser_t *ini, char *section, size_t section_size,
             char *key, size_t key_size, char *value, size_t value_size) {
    char line[INI_MAX_LINE];

    if (!ini || !section || !key || !value || section_size == 0 ||
        key_size == 0 || value_size == 0) return -1;
    while (ini->ptr) {
        int truncated = 0;
        ini->ptr = read_line(ini->ptr, line, sizeof(line), &truncated);
        if (truncated) return -1;

        if (line[0] == '\0' || line[0] == ';' || line[0] == '#')
            continue;

        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (end) {
                *end = '\0';
                size_t section_len = strlen(line + 1);
                if (section_len >= sizeof(ini->section)) return -1;
                memcpy(ini->section, line + 1, section_len + 1);
            }
            continue;
        }

        char *eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            char *k = line;
            char *v = eq + 1;
            str_trim(k);

            size_t section_len = strlen(ini->section);
            size_t key_len = strlen(k);
            size_t value_len = strlen(v);
            if (section_len >= section_size || key_len >= key_size || value_len >= value_size)
                return -1;
            memcpy(section, ini->section, section_len + 1);
            memcpy(key, k, key_len + 1);
            memcpy(value, v, value_len + 1);
            return 1;
        }
    }

    return 0;
}

int ini_get_into(ini_parser_t *ini, const char *section, const char *key,
                 char *value, size_t value_size) {
    char sect[64], k[64], v[INI_MAX_LINE];
    if (!ini || !section || !key || !value || value_size == 0) return -1;
    ini->ptr = ini->data;
    ini->section[0] = '\0';
    while (1) {
        int result = ini_next(ini, sect, sizeof(sect), k, sizeof(k), v, sizeof(v));
        if (result <= 0) return -1;
        if (strcasecmp(sect, section) == 0 && strcasecmp(k, key) == 0) {
            size_t len = strlen(v);
            if (len >= value_size) return -1;
            memcpy(value, v, len + 1);
            return 0;
        }
    }
}

const char* ini_get(ini_parser_t *ini, const char *section, const char *key) {
    static char result[INI_MAX_LINE];
    return ini_get_into(ini, section, key, result, sizeof(result)) == 0 ? result : NULL;
}

int ini_get_int(ini_parser_t *ini, const char *section, const char *key, int default_val) {
    const char *val = ini_get(ini, section, key);
    if (!val) return default_val;
    return atoi(val);
}

int ini_get_color(ini_parser_t *ini, const char *section, const char *key, int default_val) {
    const char *val = ini_get(ini, section, key);
    if (!val) return default_val;
    int c = atoi(val);
    if (c < 0 || c > 15) return default_val;
    return c;
}
