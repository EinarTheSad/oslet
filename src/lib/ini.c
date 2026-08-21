#include "ini.h"
#include "string.h"
#include "stdlib.h"

void ini_init(ini_parser_t *ini, char *buffer) {
    ini->data = buffer;
    ini->ptr = buffer;
    ini->section[0] = '\0';
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
