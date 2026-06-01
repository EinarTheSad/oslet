#include "private.h"

int parse_path(const char *path, uint8_t *drive, char *rest, size_t rest_size) {
    if (!path || !drive || !rest) return -1;
    
    *drive = current_dir[0];
    
    if (path[0] && path[1] == ':') {
        *drive = toupper_s(path[0]);
        path += 2;
    }
    
    if (path[0] != '/' && path[0] != '\0') {
        char temp[FAT32_MAX_PATH];
        strcpy_s(temp, current_dir + 3, sizeof(temp));
        
        size_t len = strlen_s(temp);
        if (len > 0 && temp[len-1] != '/') {
            if (len < FAT32_MAX_PATH - 1) {
                temp[len] = '/';
                temp[len+1] = '\0';
            }
        }
        
        size_t temp_len = strlen_s(temp);
        size_t path_len = strlen_s(path);
        if (temp_len + path_len < sizeof(temp)) {
            strcpy_s(temp + temp_len, path, sizeof(temp) - temp_len);
        }
        strcpy_s(rest, temp, rest_size);
    } else if (path[0] == '\0' || strcmp_s(path, ".") == 0) {
        strcpy_s(rest, current_dir + 3, rest_size);
    } else {
        if (path[0] == '/') path++;
        strcpy_s(rest, path, rest_size);
    }
    
    return 0;
}

int navigate_path(fat32_volume_t *vol, const char *path, uint32_t *out_dir_cluster, char *out_filename) {
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
    while (*token) {
        char *next = token;
        while (*next && *next != '/') next++;

        char save = *next;
        *next = '\0';

        if (token[0] != '\0') {
            if (strcmp_s(token, "..") == 0) {
                size_t cluster_size = vol->sectors_per_cluster * vol->bytes_per_sector;
                uint8_t *cluster_buf = kmalloc(cluster_size);
                if (!cluster_buf) return -1;

                if (read_cluster(vol, *out_dir_cluster, cluster_buf) != 0) {
                    kfree(cluster_buf);
                    return -1;
                }

                fat32_direntry_t *entries = (fat32_direntry_t*)cluster_buf;
                *out_dir_cluster = ((uint32_t)entries[1].first_cluster_high << 16) | entries[1].first_cluster_low;
                if (*out_dir_cluster == 0) *out_dir_cluster = vol->root_cluster;
                kfree(cluster_buf);
            } else if (strcmp_s(token, ".") != 0) {
                fat32_direntry_t entry;
                if (find_in_dir(vol, *out_dir_cluster, token, &entry, NULL, NULL) != 0)
                    return -1;
                if (!(entry.attr & FAT_ATTR_DIRECTORY))
                    return -1;
                *out_dir_cluster = ((uint32_t)entry.first_cluster_high << 16) | entry.first_cluster_low;
            }
        }

        *next = save;
        token = save ? (next + 1) : next;
    }

    return 0;
}
