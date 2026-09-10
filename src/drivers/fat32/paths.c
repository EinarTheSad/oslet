#include "private.h"

static int path_cluster_valid(fat32_volume_t *vol, uint32_t cluster) {
    return vol && cluster >= 2 && cluster < FAT32_EOC &&
           cluster - 2 < vol->data_clusters;
}

static int is_dot_entry(const fat32_direntry_t *entry, int parent) {
    if (!entry || !(entry->attr & FAT_ATTR_DIRECTORY)) return 0;
    if (entry->name[0] != '.' || entry->name[1] != (parent ? '.' : ' ')) return 0;
    for (int i = 2; i < 11; i++) {
        if (entry->name[i] != ' ') return 0;
    }
    return 1;
}

int parse_path(const char *path, uint8_t *drive, char *rest, size_t rest_size) {
    if (!path || !drive || !rest) return -1;

    task_t *task = task_get_current();
    char cwd[FAT32_MAX_PATH];
    if (task) strcpy_s(cwd, task->cwd, sizeof(cwd));
    else strcpy_s(cwd, current_dir, sizeof(cwd));

    size_t path_len = strlen_s(path);
    if (path_len >= FAT32_MAX_PATH || rest_size == 0) return -1;

    *drive = cwd[0];
    int explicit_drive = 0;
    if (path[0] && path[1] == ':') {
        *drive = toupper_s(path[0]);
        path += 2;
        explicit_drive = 1;
    }

    if (path[0] != '/' && path[0] != '\0') {
        char temp[FAT32_MAX_PATH];
        const char *base = cwd + 3;
        if (explicit_drive && *drive != (uint8_t)cwd[0]) base = "";
        size_t base_len = strlen_s(base);
        size_t relative_len = strlen_s(path);
        size_t separator = (base_len > 0 && base[base_len - 1] != '/') ? 1 : 0;
        if (base_len + separator + relative_len >= sizeof(temp)) return -1;

        memcpy_s(temp, base, base_len);
        size_t temp_len = base_len;
        if (separator) temp[temp_len++] = '/';
        memcpy_s(temp + temp_len, path, relative_len + 1);
        size_t total_len = temp_len + relative_len;
        if (total_len >= rest_size) return -1;
        memcpy_s(rest, temp, total_len + 1);
    } else if (path[0] == '\0' || strcmp_s(path, ".") == 0) {
        size_t cwd_len = strlen_s(cwd + 3);
        if (cwd_len >= rest_size) return -1;
        memcpy_s(rest, cwd + 3, cwd_len + 1);
    } else {
        const char *absolute = path;
        if (*absolute == '/') absolute++;
        size_t absolute_len = strlen_s(absolute);
        if (absolute_len >= rest_size) return -1;
        memcpy_s(rest, absolute, absolute_len + 1);
    }

    return 0;
}

int navigate_path(fat32_volume_t *vol, const char *path, uint32_t *out_dir_cluster, char *out_filename) {
    if (!vol || !out_dir_cluster || !out_filename) return -1;
    if (!path_cluster_valid(vol, vol->root_cluster)) return -1;
    *out_dir_cluster = vol->root_cluster;

    if (!path || path[0] == '\0' || strcmp_s(path, "/") == 0 || strcmp_s(path, ".") == 0) {
        if (out_filename) out_filename[0] = '\0';
        return 0;
    }

    char temp[FAT32_MAX_PATH];
    strcpy_s(temp, path, sizeof(temp));

    char *p = temp;
    if (*p == '/') p++;

    char *last_slash = NULL;
    for (char *s = p; *s; s++) {
        if (*s == '/') last_slash = s;
    }

    if (last_slash) {
        *last_slash = '\0';
        if (out_filename) strcpy_s(out_filename, last_slash + 1, FAT32_MAX_PATH);
    } else {
        if (out_filename) strcpy_s(out_filename, p, FAT32_MAX_PATH);
        return 0;
    }

    char *token = p;
    uint32_t traversed = 0;
    while (*token) {
        char *next = token;
        while (*next && *next != '/') next++;

        char save = *next;
        *next = '\0';

        if (token[0] != '\0') {
            if (strcmp_s(token, "..") == 0) {
                size_t cluster_size = vol->sectors_per_cluster * vol->bytes_per_sector;
                if (cluster_size < 2 * sizeof(fat32_direntry_t) ||
                    cluster_size / sizeof(fat32_direntry_t) < 2 ||
                    !path_cluster_valid(vol, *out_dir_cluster)) return -1;
                uint8_t *cluster_buf = kmalloc(cluster_size);
                if (!cluster_buf) return -1;

                if (read_cluster(vol, *out_dir_cluster, cluster_buf) != 0) {
                    kfree(cluster_buf);
                    return -1;
                }

                fat32_direntry_t *entries = (fat32_direntry_t*)cluster_buf;
                if (!is_dot_entry(&entries[1], 1)) {
                    kfree(cluster_buf);
                    return -1;
                }
                uint32_t parent = ((uint32_t)entries[1].first_cluster_high << 16) |
                                  entries[1].first_cluster_low;
                if (parent == 0) parent = vol->root_cluster;
                if (!path_cluster_valid(vol, parent)) {
                    kfree(cluster_buf);
                    return -1;
                }
                *out_dir_cluster = parent;
                kfree(cluster_buf);
            } else if (strcmp_s(token, ".") != 0) {
                if (!path_cluster_valid(vol, *out_dir_cluster)) return -1;
                fat32_direntry_t entry;
                if (find_in_dir(vol, *out_dir_cluster, token, &entry, NULL, NULL) != 0)
                    return -1;
                if (!(entry.attr & FAT_ATTR_DIRECTORY))
                    return -1;
                *out_dir_cluster = ((uint32_t)entry.first_cluster_high << 16) | entry.first_cluster_low;
                if (!path_cluster_valid(vol, *out_dir_cluster)) return -1;
            }

            if (++traversed > vol->data_clusters) return -1;
        }

        *next = save;
        token = save ? (next + 1) : next;
    }

    return 0;
}
