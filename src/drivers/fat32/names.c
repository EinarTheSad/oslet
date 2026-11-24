#include "private.h"

static char fat32_short_name_char(unsigned char ch) {
    if (ch == 0 || ch == ' ' || ch == '.' || ch == '/' || ch == '\\' ||
        ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' ||
        ch == '>' || ch == '|' || ch == '+' || ch == ',' || ch == ';' ||
        ch == '=' || ch == '[' || ch == ']' || ch == '\t' || ch == '\r' ||
        ch == '\n') {
        return '_';
    }
    return (char)toupper_s(ch);
}

uint8_t lfn_checksum(const char *short_name) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = ((sum & 1) << 7) + (sum >> 1) + (uint8_t)short_name[i];
    }
    return sum;
}

void lfn_add(lfn_state_t *state, const lfn_entry_t *entry) {
    int order = entry->order & 0x3F;
    if (entry->order & 0x40) {
        memset_s(state, 0, sizeof(*state));
        state->valid = 1;
        state->next = order;
        state->checksum = entry->checksum;
    }
    if (!state->valid || (entry->order & 0x80) || order < 1 || order > 20 ||
        order != state->next || entry->checksum != state->checksum ||
        entry->type != 0 || entry->first_cluster != 0) {
        state->valid = 0;
        return;
    }

    uint16_t chars[13];
    memcpy_s(chars, entry->name1, sizeof(entry->name1));
    memcpy_s(chars + 5, entry->name2, sizeof(entry->name2));
    memcpy_s(chars + 11, entry->name3, sizeof(entry->name3));
    int base = (order - 1) * 13;
    for (int i = 0; i < 13; i++) {
        if (chars[i] == 0 || chars[i] == 0xFFFF) break;
        if (base + i >= FAT32_MAX_PATH - 1) {
            state->valid = 0;
            return;
        }
        state->name[base + i] = chars[i] < 128 ? (char)chars[i] : '?';
    }
    state->next--;
}

const char *lfn_name(const lfn_state_t *state, const char *short_name) {
    if (!state->valid || state->next != 0 || !state->name[0] ||
        state->checksum != lfn_checksum(short_name)) return NULL;
    return state->name;
}

void parse_filename(const char *name, char *out_name) {
    memset_s(out_name, ' ', 11);
    if (!name || !out_name) return;

    const char *dot = NULL;
    for (const char *p = name; *p; p++) {
        if (*p == '.') {
            dot = p;
            break;
        }
    }

    size_t base_len = dot ? (size_t)(dot - name) : strlen_s(name);
    size_t ext_len = dot ? strlen_s(dot + 1) : 0;

    if (base_len == 0 && dot && dot == name) {
        base_len = 0;
    }

    if (base_len > 8 || ext_len > 3) {
        size_t copy_len = base_len > 8 ? 6 : base_len;
        for (size_t i = 0; i < copy_len && i < 8; i++) {
            out_name[i] = fat32_short_name_char((unsigned char)name[i]);
        }

        if (base_len > 8) {
            out_name[6] = '~';
            out_name[7] = '1';
        }

        size_t ext_copy = ext_len > 3 ? 3 : ext_len;
        for (size_t i = 0; i < ext_copy; i++) {
            out_name[8 + i] = fat32_short_name_char((unsigned char)dot[1 + i]);
        }
        return;
    }

    size_t i = 0;
    while (name[i] && name[i] != '.' && i < 8) {
        out_name[i] = fat32_short_name_char((unsigned char)name[i]);
        i++;
    }

    if (dot && dot[1]) {
        size_t j = 8;
        for (size_t k = 1; dot[k] && j < 11; k++) {
            out_name[j++] = fat32_short_name_char((unsigned char)dot[k]);
        }
    }
}
