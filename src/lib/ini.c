#include "ini.h"
#include "string.h"
#include "stdlib.h"

void ini_init(ini_parser_t *ini, char *buffer) {
    ini->data = buffer;
    ini->ptr = buffer;
    ini->section[0] = '\0';
}

/* Replace or insert a section in the INI data.
 * See declaration in ini.h for semantics.
 */
int ini_replace_section(const char *data, const char *section, const char *new_text, char *outbuf, int outbuf_size) {
    const char *src = data ? data : "";
    const char *p = src;
    const char *found_start = NULL;
    const char *found_end = NULL;

    /* Find section start: look for lines that begin with '[' (optionally preceded by whitespace)
     * and whose name matches 'section' (case-insensitive). */
    while (*p) {
        /* advance to start of next line */
        const char *line = p;
        /* skip leading whitespace */
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
                /* found header; set found_start to beginning of this line in source */
                found_start = line;
                /* find end of this section: go to next line after header, then scan until next header or EOF */
                const char *r = q + 1; /* after ']' */
                /* advance to end of this header line */
                while (*r && *r != '\n') r++;
                if (*r == '\n') r++;
                /* now scan for next header at start of a line */
                while (*r) {
                    const char *s = r;
                    /* skip whitespace at start of line */
                    while (*s == ' ' || *s == '\t' || *s == '\r') s++;
                    if (*s == '[') {
                        /* candidate header - treat this as start of next section */
                        found_end = r;
                        break;
                    }
                    /* advance to next line */
                    while (*r && *r != '\n') r++;
                    if (*r == '\n') r++;
                }
                if (!found_end) found_end = r; /* up to EOF */
                break;
            }
        }
        /* advance p to next line */
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }

    /* Compose output: copy before found_start, then new_text, then remainder after found_end */
    int written = 0;
    int rem = outbuf_size;
    outbuf[0] = '\0';

    /* copy head */
    if (found_start) {
        int head_len = (int)(found_start - src);
        if (head_len >= rem) return -1;
        memcpy(outbuf + written, src, head_len);
        written += head_len;
        rem -= head_len;
    } else {
        /* no existing section; copy all of src */
        int src_len = strlen(src);
        if (src_len >= rem) return -1;
        memcpy(outbuf + written, src, src_len);
        written += src_len;
        rem -= src_len;
    }

    /* Ensure there's a newline before appending if needed */
    if (written > 0) {
        /* if last char isn't '\n', add one (use '\n' since file can contain \r already) */
        if (outbuf[written-1] != '\n') {
            if (rem <= 1) return -1;
            outbuf[written++] = '\n';
            rem--;
        }
    }

    /* append new_text */
    int new_len = strlen(new_text);
    if (new_len >= rem) return -1;
    memcpy(outbuf + written, new_text, new_len);
    written += new_len;
    rem -= new_len;

    /* append remainder (if section existed) */
    if (found_start) {
        const char *tail = found_end;
        int tail_len = (int)strlen(tail);
        if (tail_len >= rem) return -1;
        memcpy(outbuf + written, tail, tail_len);
        written += tail_len;
        rem -= tail_len;
    }

    /* Null-terminate */
    if (rem <= 0) return -1;
    outbuf[written] = '\0';
    return written;
}

static char* read_line(char *buf, char *line, int max_len) {
    int i = 0;
    while (*buf && *buf != '\n' && i < max_len - 1) {
        line[i++] = *buf++;
    }
    line[i] = '\0';
    str_trim(line);

    if (*buf == '\n') buf++;
    return *buf ? buf : NULL;
}

int ini_next(ini_parser_t *ini, char *section, char *key, char *value) {
    char line[INI_MAX_LINE];

    while (ini->ptr) {
        ini->ptr = read_line(ini->ptr, line, sizeof(line));

        /* Skip empty lines and comments */
        if (line[0] == '\0' || line[0] == ';' || line[0] == '#')
            continue;

        /* Section header */
        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (end) {
                *end = '\0';
                strncpy(ini->section, line + 1, sizeof(ini->section) - 1);
                ini->section[sizeof(ini->section) - 1] = '\0';
            }
            continue;
        }

        /* Key=Value */
        char *eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            char *k = line;
            char *v = eq + 1;
            str_trim(k);

            strcpy(section, ini->section);
            strcpy(key, k);
            strcpy(value, v);
            return 1;
        }
    }

    return 0;
}

const char* ini_get(ini_parser_t *ini, const char *section, const char *key) {
    static char result[INI_MAX_LINE];
    char sect[64], k[64], v[INI_MAX_LINE];

    /* Reset parser to start */
    ini->ptr = ini->data;
    ini->section[0] = '\0';

    while (ini_next(ini, sect, k, v)) {
        if (strcasecmp(sect, section) == 0 && strcasecmp(k, key) == 0) {
            strcpy(result, v);
            return result;
        }
    }

    return NULL;
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
