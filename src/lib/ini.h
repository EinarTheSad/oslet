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

/* Get integer value. Returns default_val if not found or invalid. */
int ini_get_int(ini_parser_t *ini, const char *section, const char *key, int default_val);

/* Get color value (0-15). Returns default_val if not found or out of range. */
int ini_get_color(ini_parser_t *ini, const char *section, const char *key, int default_val);

/* Replace (or insert) a section in an INI file buffer.
 * data: original file contents (null-terminated). If NULL treat as empty.
 * section: section name to replace (case-insensitive).
 * new_text: full section text to insert (including header like "[SECTION]\r\n...").
 * outbuf: destination buffer to receive resulting contents.
 * outbuf_size: size of outbuf in bytes.
 * Returns number of bytes written to outbuf (excluding terminating NUL), or -1 on overflow.
 */
int ini_replace_section(const char *data, const char *section, const char *new_text, char *outbuf, int outbuf_size);
