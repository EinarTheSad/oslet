#include "ini.h"
#include "string.h"
#include "stdlib.h"

void ini_init(ini_parser_t *ini, char *buffer) {
    ini->data = buffer;
    ini->ptr = buffer;
    ini->section[0] = '\0';
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
