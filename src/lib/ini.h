#pragma once

#define INI_MAX_LINE 256

typedef struct {
    char *data;
    char *ptr;
    char section[64];
} ini_parser_t;

/* Initialize parser with file contents buffer */
void ini_init(ini_parser_t *ini, char *buffer);

/* Read next key-value pair. Returns 1 if found, 0 if end of file.
 * section, key, value are filled with current values. */
int ini_next(ini_parser_t *ini, char *section, char *key, char *value);

/* Get value for key in section. Returns NULL if not found.
 * Restarts parsing from beginning. */
const char* ini_get(ini_parser_t *ini, const char *section, const char *key);
