#include "private.h"

fat32_file_t* fat32_open(const char *path, const char *mode) {
    if (!path || !mode) return NULL;

    fat32_acquire();

    uint8_t drive;
    char rest[FAT32_MAX_PATH];
    if (parse_path(path, &drive, rest, sizeof(rest)) != 0) {
        /* parse_path failed */
        fat32_release();
        return NULL;
    }
    (void)drive; (void)rest;

    fat32_volume_t *vol = get_volume(drive);
    if (!vol) {
        /* volume not found */
        fat32_release();
        return NULL;
    }

    fat32_file_t *file = NULL;
    for (int i = 0; i < FAT32_MAX_OPEN_FILES; i++) {
        if (!open_files[i].in_use) {
            file = &open_files[i];
            break;
        }
    }
    if (!file) {
        fat32_release();
        return NULL;
    }
    /* allocated open_files slot */

    uint32_t dir_cluster;
    char filename[FAT32_MAX_PATH];
    if (navigate_path(vol, rest, &dir_cluster, filename) != 0) {
        /* navigate_path failed */
        fat32_release();
        return NULL;
    }
    (void)dir_cluster; (void)filename;

    if (filename[0] == '\0') {
        fat32_release();
        return NULL;
    }

    fat32_direntry_t entry;
    int found = find_in_dir(vol, dir_cluster, filename, &entry, NULL, NULL);

    if (mode[0] == 'r' && found != 0) {
        fat32_release();
        return NULL;
    }

    if (mode[0] == 'w') {
        if (found != 0) {
            uint32_t new_cluster = alloc_cluster(vol);
            if (new_cluster == 0) {
                fat32_release();
                return NULL;
            }
            if (add_dir_entry(vol, dir_cluster, filename, new_cluster, 0, FAT_ATTR_ARCHIVE) != 0) {
                free_cluster_chain(vol, new_cluster);
                fat32_release();
                return NULL;
            }
            sync_fat(vol);
            file->first_cluster = new_cluster;
            file->size = 0;
        } else {
            uint32_t first = ((uint32_t)entry.first_cluster_high << 16) | entry.first_cluster_low;
            uint32_t next = get_next_cluster(vol, first);
            if (next < FAT32_EOC) {
                free_cluster_chain(vol, next);
                set_next_cluster(vol, first, FAT32_EOC);
            }
            sync_fat(vol);
            file->first_cluster = first;
            file->size = 0;
        }
    } else {
        file->first_cluster = ((uint32_t)entry.first_cluster_high << 16) | entry.first_cluster_low;
        file->size = entry.file_size;
    }

    file->current_cluster = file->first_cluster;
    file->position = 0;
    file->cluster_offset = 0;
    file->in_use = 1;
    file->drive = drive;
    file->mode = mode[0];
    strcpy_s(file->path, path, sizeof(file->path));

    fat32_release();
    return file;
}

int fat32_read(fat32_file_t *file, void *buffer, size_t size) {
    if (!file || !file->in_use || !buffer) return -1;
    if (file->position >= file->size) return 0;
    
    fat32_acquire();
    
    fat32_volume_t *vol = get_volume(file->drive);
    if (!vol) {
        fat32_release();
        return -1;
    }
    
    size_t bytes_read = 0;
    size_t to_read = size;
    if (file->position + to_read > file->size)
        to_read = file->size - file->position;
    
    size_t cluster_size = vol->sectors_per_cluster * vol->bytes_per_sector;
    uint8_t *cluster_buf = kmalloc(cluster_size);
    if (!cluster_buf) {
        fat32_release();
        return -1;
    }
    
    while (to_read > 0 && file->current_cluster >= 2 && file->current_cluster < FAT32_EOC) {
        if (read_cluster(vol, file->current_cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
            fat32_release();
            return -1;
        }
        
        uint32_t available = cluster_size - file->cluster_offset;
        uint32_t chunk = (to_read < available) ? (uint32_t)to_read : available;
        
        memcpy_s((uint8_t*)buffer + bytes_read, cluster_buf + file->cluster_offset, chunk);
        bytes_read += chunk;
        to_read -= chunk;
        file->position += chunk;
        file->cluster_offset += chunk;
        
        if (file->cluster_offset >= cluster_size) {
            file->current_cluster = get_next_cluster(vol, file->current_cluster);
            file->cluster_offset = 0;
        }
    }
    
    kfree(cluster_buf);
    fat32_release();
    return (int)bytes_read;
}

int fat32_write(fat32_file_t *file, const void *buffer, size_t size) {
    if (!file || !file->in_use || !buffer) return -1;
    if (file->mode != 'w' && file->mode != 'a') return -1;
    
    fat32_acquire();
    
    fat32_volume_t *vol = get_volume(file->drive);
    if (!vol) {
        fat32_release();
        return -1;
    }
    
    size_t bytes_written = 0;
    size_t cluster_size = vol->sectors_per_cluster * vol->bytes_per_sector;
    uint8_t *cluster_buf = kmalloc(cluster_size);
    if (!cluster_buf) {
        fat32_release();
        return -1;
    }
    
    while (size > 0) {
        if (file->current_cluster >= FAT32_EOC || file->current_cluster < 2) {
            uint32_t new_cluster = alloc_cluster(vol);
            if (new_cluster == 0) break;
            
            if (file->first_cluster == 0) {
                file->first_cluster = new_cluster;
                file->current_cluster = new_cluster;
            } else {
                uint32_t last = file->first_cluster;
                uint32_t next;
                while ((next = get_next_cluster(vol, last)) < FAT32_EOC) {
                    last = next;
                }
                set_next_cluster(vol, last, new_cluster);
                file->current_cluster = new_cluster;
            }
            
            memset_s(cluster_buf, 0, cluster_size);
            if (write_cluster(vol, new_cluster, cluster_buf) != 0) {
                kfree(cluster_buf);
                return -1;
            }
        }
        
        if (file->cluster_offset != 0 || size < cluster_size) {
            if (read_cluster(vol, file->current_cluster, cluster_buf) != 0) {
                kfree(cluster_buf);
                fat32_release();
                return -1;
            }
        }
        
        uint32_t available = cluster_size - file->cluster_offset;
        uint32_t chunk = (size < available) ? (uint32_t)size : available;
        
        memcpy_s(cluster_buf + file->cluster_offset, (const uint8_t*)buffer + bytes_written, chunk);
        
        if (write_cluster(vol, file->current_cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
            fat32_release();
            return -1;
        }
        
        bytes_written += chunk;
        size -= chunk;
        file->position += chunk;
        file->cluster_offset += chunk;
        
        if (file->cluster_offset >= cluster_size) {
            file->current_cluster = get_next_cluster(vol, file->current_cluster);
            file->cluster_offset = 0;
        }
    }
    
    if (file->position > file->size)
        file->size = file->position;
    
    kfree(cluster_buf);
    fat32_release();
    return (int)bytes_written;
}

int fat32_seek(fat32_file_t *file, uint32_t offset) {
    if (!file || !file->in_use) return -1;
    
    fat32_acquire();
    
    fat32_volume_t *vol = get_volume(file->drive);
    if (!vol) {
        fat32_release();
        return -1;
    }
    
    file->position = offset;
    file->current_cluster = file->first_cluster;
    file->cluster_offset = 0;
    
    uint32_t cluster_size = vol->sectors_per_cluster * vol->bytes_per_sector;
    while (offset >= cluster_size && file->current_cluster < FAT32_EOC) {
        file->current_cluster = get_next_cluster(vol, file->current_cluster);
        offset -= cluster_size;
    }
    
    file->cluster_offset = offset;
    fat32_release();
    return 0;
}

uint32_t fat32_tell(fat32_file_t *file) {
    return file ? file->position : 0;
}

void fat32_close(fat32_file_t *file) {
    if (!file || !file->in_use) return;
    
    fat32_acquire();
    
    if (file->mode == 'w' || file->mode == 'a') {
        fat32_volume_t *vol = get_volume(file->drive);
        if (vol) {
            sync_fat(vol);
            
            uint8_t drive;
            char rest[FAT32_MAX_PATH];
            if (parse_path(file->path, &drive, rest, sizeof(rest)) == 0) {
                uint32_t dir_cluster;
                char filename[FAT32_MAX_PATH];
                if (navigate_path(vol, rest, &dir_cluster, filename) == 0) {
                    fat32_direntry_t entry;
                    uint32_t cluster, offset;
                    
                    if (find_in_dir(vol, dir_cluster, filename, &entry, &cluster, &offset) == 0) {
                        size_t cluster_size = vol->sectors_per_cluster * vol->bytes_per_sector;
                        uint8_t *cluster_buf = kmalloc(cluster_size);
                        if (cluster_buf) {
                            if (read_cluster(vol, cluster, cluster_buf) == 0) {
                                fat32_direntry_t *entries = (fat32_direntry_t*)cluster_buf;
                                uint32_t idx = offset / 32;
                                entries[idx].file_size = file->size;

                                /* Update modification timestamp */
                                uint16_t fat_time, fat_date;
                                get_fat_timestamp(&fat_time, &fat_date);
                                entries[idx].modified_time = fat_time;
                                entries[idx].modified_date = fat_date;
                                entries[idx].accessed_date = fat_date;

                                if (write_cluster(vol, cluster, cluster_buf) != 0) {
                                    printf("Failed to write directory entry\n");
                                }
                            }
                            kfree(cluster_buf);
                        }
                    }
                }
            }
        }
    }
    
    file->in_use = 0;
    fat32_release();
}
