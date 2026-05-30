#include "private.h"

static int fat32_list_dir_unlocked(const char *path, fat32_dirent_t *entries, int max_entries) {
    if (!entries || max_entries <= 0) return -1;

    uint8_t drive;
    char rest[FAT32_MAX_PATH];
    if (parse_path(path, &drive, rest, sizeof(rest)) != 0) return -1;

    fat32_volume_t *vol = get_volume(drive);
    if (!vol) return -1;

    uint32_t dir_cluster;
    char filename[FAT32_MAX_PATH];
    if (navigate_path(vol, rest, &dir_cluster, filename) != 0) return -1;

    if (filename[0] != '\0') {
        fat32_direntry_t entry;
        if (find_in_dir(vol, dir_cluster, filename, &entry, NULL, NULL) != 0) return -1;
        if (!(entry.attr & FAT_ATTR_DIRECTORY)) return -1;
        dir_cluster = ((uint32_t)entry.first_cluster_high << 16) | entry.first_cluster_low;
        if (dir_cluster == 0) dir_cluster = vol->root_cluster;
    }

    size_t cluster_size = vol->sectors_per_cluster * vol->bytes_per_sector;
    uint8_t *cluster_buf = kmalloc(cluster_size);
    if (!cluster_buf) return -1;

    int count = 0;
    uint32_t cluster = dir_cluster;
    
    while (cluster >= 2 && cluster < FAT32_EOC && count < max_entries) {
        if (read_cluster(vol, cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
            return -1;
        }
        
        fat32_direntry_t *dir_entries = (fat32_direntry_t*)cluster_buf;
        uint32_t entries_count = cluster_size / 32;
        
        char lfn_buffer[FAT32_MAX_PATH];
        int lfn_valid = 0;
        uint8_t lfn_checksum_val = 0;
        
        for (uint32_t i = 0; i < entries_count && count < max_entries; i++) {
            if (count >= max_entries) break;

            if (dir_entries[i].name[0] == 0x00) {
                break;
            }
            
            if ((uint8_t)dir_entries[i].name[0] == 0xE5) {
                lfn_valid = 0;
                continue;
            }
            
            if (dir_entries[i].attr == FAT_ATTR_LFN) {
                lfn_entry_t *lfn = (lfn_entry_t*)&dir_entries[i];
                int order = lfn->order & 0x3F;
                int is_last = (lfn->order & 0x40) != 0;
                
                if (is_last) {
                    lfn_checksum_val = lfn->checksum;
                    lfn_valid = 1;
                    memset_s(lfn_buffer, 0, sizeof(lfn_buffer));
                }
                
                if (lfn_valid && order > 0 && order <= 20) {
                    int base = (order - 1) * 13;
                    uint16_t temp1[5], temp2[6], temp3[2];
                    memcpy_s(temp1, lfn->name1, sizeof(temp1));
                    memcpy_s(temp2, lfn->name2, sizeof(temp2));
                    memcpy_s(temp3, lfn->name3, sizeof(temp3));
                    utf16_to_ascii(temp1, lfn_buffer + base, 5);
                    utf16_to_ascii(temp2, lfn_buffer + base + 5, 6);
                    utf16_to_ascii(temp3, lfn_buffer + base + 11, 2);
                }
                continue;
            }
            
            if (dir_entries[i].attr & FAT_ATTR_VOLUME_ID) {
                lfn_valid = 0;
                continue;
            }
            
            if (dir_entries[i].name[0] == '.' && 
                (dir_entries[i].name[1] == ' ' || dir_entries[i].name[1] == '.')) {
                lfn_valid = 0;
                continue;
            }
            
            memset_s(entries[count].name, 0, sizeof(entries[count].name));
            
            if (lfn_valid && lfn_checksum(dir_entries[i].name) == lfn_checksum_val) {
                strcpy_s(entries[count].name, lfn_buffer, sizeof(entries[count].name));
            } else {
                int j = 0;
                int name_end = 7;
                while (name_end >= 0 && dir_entries[i].name[name_end] == ' ') name_end--;
                
                for (int k = 0; k <= name_end; k++) {
                    entries[count].name[j++] = dir_entries[i].name[k];
                }
                
                if (dir_entries[i].name[8] != ' ') {
                    entries[count].name[j++] = '.';
                    int ext_end = 10;
                    while (ext_end >= 8 && dir_entries[i].name[ext_end] == ' ') ext_end--;
                    
                    for (int k = 8; k <= ext_end; k++) {
                        entries[count].name[j++] = dir_entries[i].name[k];
                    }
                }
                entries[count].name[j] = '\0';
            }
            
            entries[count].size = dir_entries[i].file_size;
            entries[count].first_cluster = ((uint32_t)dir_entries[i].first_cluster_high << 16) |
                                          dir_entries[i].first_cluster_low;
            entries[count].mtime = dir_entries[i].modified_time;
            entries[count].mdate = dir_entries[i].modified_date;
            entries[count].is_directory = (dir_entries[i].attr & FAT_ATTR_DIRECTORY) ? 1 : 0;
            entries[count].attr = dir_entries[i].attr;
            count++;
            lfn_valid = 0;
        }
        
        cluster = get_next_cluster(vol, cluster);
    }
    
    kfree(cluster_buf);
    return count;
}

static int fat32_mkdir_unlocked(const char *path) {
    if (!path) return -1;
    
    uint8_t drive;
    char rest[FAT32_MAX_PATH];
    if (parse_path(path, &drive, rest, sizeof(rest)) != 0) return -1;
    
    fat32_volume_t *vol = get_volume(drive);
    if (!vol) return -1;
    
    uint32_t dir_cluster;
    char dirname[FAT32_MAX_PATH];
    if (navigate_path(vol, rest, &dir_cluster, dirname) != 0) return -1;
    
    if (dirname[0] == '\0') return -1;
    
    fat32_direntry_t tmp;
    if (find_in_dir(vol, dir_cluster, dirname, &tmp, NULL, NULL) == 0) return -1;
    
    uint32_t new_cluster = alloc_cluster(vol);
    if (new_cluster == 0) return -1;
    
    if (add_dir_entry(vol, dir_cluster, dirname, new_cluster, 0, FAT_ATTR_DIRECTORY) != 0) {
        free_cluster_chain(vol, new_cluster);
        return -1;
    }
    
    size_t cluster_size = vol->sectors_per_cluster * vol->bytes_per_sector;
    uint8_t *cluster_buf = kmalloc(cluster_size);
    if (!cluster_buf) {
        free_cluster_chain(vol, new_cluster);
        return -1;
    }
    
    memset_s(cluster_buf, 0, cluster_size);
    fat32_direntry_t *entries = (fat32_direntry_t*)cluster_buf;
    
    memset_s(entries[0].name, ' ', 11);
    entries[0].name[0] = '.';
    entries[0].attr = FAT_ATTR_DIRECTORY;
    entries[0].first_cluster_high = (uint16_t)(new_cluster >> 16);
    entries[0].first_cluster_low = (uint16_t)(new_cluster & 0xFFFF);
    entries[0].file_size = 0;
    
    memset_s(entries[1].name, ' ', 11);
    entries[1].name[0] = '.';
    entries[1].name[1] = '.';
    entries[1].attr = FAT_ATTR_DIRECTORY;
    entries[1].first_cluster_high = (uint16_t)(dir_cluster >> 16);
    entries[1].first_cluster_low = (uint16_t)(dir_cluster & 0xFFFF);
    entries[1].file_size = 0;
    
    if (write_cluster(vol, new_cluster, cluster_buf) != 0) {
        kfree(cluster_buf);
        free_cluster_chain(vol, new_cluster);
        return -1;
    }
    
    kfree(cluster_buf);
    sync_fat(vol);
    return 0;
}

static int fat32_rmdir_unlocked(const char *path) {
    if (!path) return -1;
    
    uint8_t drive;
    char rest[FAT32_MAX_PATH];
    if (parse_path(path, &drive, rest, sizeof(rest)) != 0) return -1;
    
    fat32_volume_t *vol = get_volume(drive);
    if (!vol) return -1;
    
    uint32_t dir_cluster;
    char dirname[FAT32_MAX_PATH];
    if (navigate_path(vol, rest, &dir_cluster, dirname) != 0) return -1;
    
    if (dirname[0] == '\0') return -1;
    
    fat32_direntry_t entry;
    if (find_in_dir(vol, dir_cluster, dirname, &entry, NULL, NULL) != 0) return -1;
    if (!(entry.attr & FAT_ATTR_DIRECTORY)) return -1;
    
    uint32_t target_cluster = ((uint32_t)entry.first_cluster_high << 16) | entry.first_cluster_low;
    
    size_t cluster_size = vol->sectors_per_cluster * vol->bytes_per_sector;
    uint8_t *cluster_buf = kmalloc(cluster_size);
    if (!cluster_buf) return -1;
    
    if (read_cluster(vol, target_cluster, cluster_buf) != 0) {
        kfree(cluster_buf);
        return -1;
    }
    
    fat32_direntry_t *entries = (fat32_direntry_t*)cluster_buf;
    uint32_t count = cluster_size / 32;
    
    for (uint32_t i = 2; i < count; i++) {
        if (entries[i].name[0] == 0x00) break;
        if ((uint8_t)entries[i].name[0] != 0xE5 && entries[i].attr != FAT_ATTR_LFN) {
            kfree(cluster_buf);
            return -1;
        }
    }
    
    kfree(cluster_buf);
    
    if (remove_dir_entry(vol, dir_cluster, dirname) != 0) return -1;
    
    free_cluster_chain(vol, target_cluster);
    sync_fat(vol);
    return 0;
}

static int fat32_unlink_unlocked(const char *path) {
    if (!path) return -1;
    
    uint8_t drive;
    char rest[FAT32_MAX_PATH];
    if (parse_path(path, &drive, rest, sizeof(rest)) != 0) return -1;
    
    fat32_volume_t *vol = get_volume(drive);
    if (!vol) return -1;
    
    uint32_t dir_cluster;
    char filename[FAT32_MAX_PATH];
    if (navigate_path(vol, rest, &dir_cluster, filename) != 0) return -1;
    
    if (filename[0] == '\0') return -1;
    
    fat32_direntry_t entry;
    if (find_in_dir(vol, dir_cluster, filename, &entry, NULL, NULL) != 0) return -1;
    if (entry.attr & FAT_ATTR_DIRECTORY) return -1;
    
    uint32_t first_cluster = ((uint32_t)entry.first_cluster_high << 16) | entry.first_cluster_low;
    
    if (remove_dir_entry(vol, dir_cluster, filename) != 0) return -1;
    
    if (first_cluster >= 2) free_cluster_chain(vol, first_cluster);
    
    sync_fat(vol);
    return 0;
}

static int fat32_rename_unlocked(const char *oldpath, const char *newpath) {
    if (!oldpath || !newpath) return -1;
    
    uint8_t old_drive, new_drive;
    char old_rest[FAT32_MAX_PATH], new_rest[FAT32_MAX_PATH];
    
    if (parse_path(oldpath, &old_drive, old_rest, sizeof(old_rest)) != 0) return -1;
    if (parse_path(newpath, &new_drive, new_rest, sizeof(new_rest)) != 0) return -1;
    
    /* Both paths must be on the same drive */
    if (old_drive != new_drive) return -1;
    
    fat32_volume_t *vol = get_volume(old_drive);
    if (!vol) return -1;
    
    /* Parse old path to get directory and filename */
    uint32_t old_dir_cluster;
    char old_filename[FAT32_MAX_PATH];
    if (navigate_path(vol, old_rest, &old_dir_cluster, old_filename) != 0) return -1;
    if (old_filename[0] == '\0') return -1;
    
    /* Parse new path to get directory and filename */
    uint32_t new_dir_cluster;
    char new_filename[FAT32_MAX_PATH];
    if (navigate_path(vol, new_rest, &new_dir_cluster, new_filename) != 0) return -1;
    if (new_filename[0] == '\0') return -1;
    
    /* Find old entry and get all its information */
    fat32_direntry_t old_entry;
    uint32_t entry_cluster, entry_offset;
    if (find_in_dir(vol, old_dir_cluster, old_filename, &old_entry, &entry_cluster, &entry_offset) != 0) return -1;
    
    /* Get file/directory information from old entry */
    uint32_t first_cluster = ((uint32_t)old_entry.first_cluster_high << 16) | old_entry.first_cluster_low;
    uint32_t file_size = old_entry.file_size;
    uint8_t attr = old_entry.attr;
    
    /* Check if destination already exists */
    fat32_direntry_t dummy;
    if (find_in_dir(vol, new_dir_cluster, new_filename, &dummy, NULL, NULL) == 0) {
        return -1;  /* Destination already exists */
    }
    
    /* Case 1: Same directory rename - update entry in place */
    if (old_dir_cluster == new_dir_cluster) {
        size_t cluster_size = vol->sectors_per_cluster * vol->bytes_per_sector;
        uint8_t *cluster_buf = kmalloc(cluster_size);
        if (!cluster_buf) return -1;
        
        if (read_cluster(vol, entry_cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
            return -1;
        }
        
        fat32_direntry_t *entries = (fat32_direntry_t*)cluster_buf;
        uint32_t entry_idx = entry_offset / 32;
        
        /* Determine if new filename requires LFN entries */
        char new_83name[11];
        parse_filename(new_filename, new_83name);
        
        int new_needs_lfn = 0;
        int name_len = strlen_s(new_filename);
        for (int i = 0; i < name_len; i++) {
            if (new_filename[i] == ' ' || (uint8_t)new_filename[i] > 127) {
                new_needs_lfn = 1;
                break;
            }
        }
        
        if (!new_needs_lfn) {
            char *dot = NULL;
            for (int i = 0; new_filename[i]; i++) {
                if (new_filename[i] == '.') {
                    dot = (char*)&new_filename[i];
                    break;
                }
            }
            int base_len = dot ? (int)(dot - new_filename) : name_len;
            int ext_len = dot ? (name_len - base_len - 1) : 0;
            if (base_len > 8 || ext_len > 3) new_needs_lfn = 1;
        }
        
        if (new_needs_lfn) {
            /* Complex case: need to remove old entry and add new with LFN */
            kfree(cluster_buf);
            
            /* Remove old entry */
            if (remove_dir_entry(vol, old_dir_cluster, old_filename) != 0) return -1;
            
            /* Add new entry with same cluster and size */
            if (add_dir_entry(vol, new_dir_cluster, new_filename, first_cluster, file_size, attr) != 0) {
                /* Try to restore old entry on failure */
                add_dir_entry(vol, old_dir_cluster, old_filename, first_cluster, file_size, attr);
                return -1;
            }
            
            sync_fat(vol);
            return 0;
        }
        
        /* Simple 8.3 rename: just update the name in place */
        memcpy_s(entries[entry_idx].name, new_83name, 11);
        
        /* Update modification time */
        rtc_time_t rtc;
        rtc_read_time(&rtc);
        entries[entry_idx].modified_time = rtc_to_fat_time(&rtc);
        entries[entry_idx].modified_date = rtc_to_fat_date(&rtc);
        
        if (write_cluster(vol, entry_cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
            return -1;
        }
        
        kfree(cluster_buf);
        sync_fat(vol);
        return 0;
    }
    
    /* Case 2: Different directory - move entry */
    /* Create new entry in destination directory */
    if (add_dir_entry(vol, new_dir_cluster, new_filename, first_cluster, file_size, attr) != 0) {
        return -1;
    }
    
    /* Remove old entry */
    if (remove_dir_entry(vol, old_dir_cluster, old_filename) != 0) {
        /* Try to remove the newly created entry if we fail to remove old one */
        remove_dir_entry(vol, new_dir_cluster, new_filename);
        return -1;
    }
    
    sync_fat(vol);
    return 0;
}

static int fat32_stat_unlocked(const char *path, fat32_dirent_t *entry) {
    if (!path || !entry) return -1;
    
    uint8_t drive;
    char rest[FAT32_MAX_PATH];
    if (parse_path(path, &drive, rest, sizeof(rest)) != 0) return -1;
    
    fat32_volume_t *vol = get_volume(drive);
    if (!vol) return -1;
    
    uint32_t dir_cluster;
    char filename[FAT32_MAX_PATH];
    if (navigate_path(vol, rest, &dir_cluster, filename) != 0) return -1;
    
    if (filename[0] == '\0') {
        strcpy_s(entry->name, "/", sizeof(entry->name));
        entry->size = 0;
        entry->first_cluster = vol->root_cluster;
        entry->mtime = 0;
        entry->mdate = 0;
        entry->is_directory = 1;
        entry->attr = FAT_ATTR_DIRECTORY;
        return 0;
    }
    
    fat32_direntry_t raw_entry;
    if (find_in_dir(vol, dir_cluster, filename, &raw_entry, NULL, NULL) != 0) return -1;
    
    strcpy_s(entry->name, filename, sizeof(entry->name));
    entry->size = raw_entry.file_size;
    entry->first_cluster = ((uint32_t)raw_entry.first_cluster_high << 16) | raw_entry.first_cluster_low;
    entry->mtime = raw_entry.modified_time;
    entry->mdate = raw_entry.modified_date;
    entry->is_directory = (raw_entry.attr & FAT_ATTR_DIRECTORY) ? 1 : 0;
    entry->attr = raw_entry.attr;
    return 0;
}

static char* fat32_getcwd_unlocked(char *buf, size_t size) {
    if (!buf || size == 0) return NULL;
    strcpy_s(buf, current_dir, size);
    return buf;
}

static int fat32_chdir_unlocked(const char *path) {
    if (!path) return -1;
    
    uint8_t drive;
    char rest[FAT32_MAX_PATH];
    if (parse_path(path, &drive, rest, sizeof(rest)) != 0) return -1;
    
    fat32_volume_t *vol = get_volume(drive);
    if (!vol) return -1;
    
    uint32_t dir_cluster;
    char dirname[FAT32_MAX_PATH];
    if (navigate_path(vol, rest, &dir_cluster, dirname) != 0) return -1;
    
    if (dirname[0] != '\0') {
        fat32_direntry_t entry;
        if (find_in_dir(vol, dir_cluster, dirname, &entry, NULL, NULL) != 0) return -1;
        if (!(entry.attr & FAT_ATTR_DIRECTORY)) return -1;
    }
    
    current_dir[0] = drive;
    current_dir[1] = ':';
    current_dir[2] = '/';
    
    if (rest[0] == '\0' || strcmp_s(rest, "/") == 0) {
        current_dir[3] = '\0';
    } else {
        strcpy_s(current_dir + 3, rest, sizeof(current_dir) - 3);
        size_t len = strlen_s(current_dir);
        if (len > 0 && current_dir[len-1] != '/') {
            if (len < sizeof(current_dir) - 1) {
                current_dir[len] = '/';
                current_dir[len+1] = '\0';
            }
        }
    }
    
    return 0;
}

int fat32_list_dir(const char *path, fat32_dirent_t *entries, int max_entries) {
    fat32_acquire();
    int ret = fat32_list_dir_unlocked(path, entries, max_entries);
    fat32_release();
    return ret;
}

int fat32_mkdir(const char *path) {
    fat32_acquire();
    int ret = fat32_mkdir_unlocked(path);
    fat32_release();
    return ret;
}

int fat32_rmdir(const char *path) {
    fat32_acquire();
    int ret = fat32_rmdir_unlocked(path);
    fat32_release();
    return ret;
}

int fat32_unlink(const char *path) {
    fat32_acquire();
    int ret = fat32_unlink_unlocked(path);
    fat32_release();
    return ret;
}

int fat32_rename(const char *oldpath, const char *newpath) {
    fat32_acquire();
    int ret = fat32_rename_unlocked(oldpath, newpath);
    fat32_release();
    return ret;
}

int fat32_stat(const char *path, fat32_dirent_t *entry) {
    fat32_acquire();
    int ret = fat32_stat_unlocked(path, entry);
    fat32_release();
    return ret;
}

char* fat32_getcwd(char *buf, size_t size) {
    fat32_acquire();
    char *ret = fat32_getcwd_unlocked(buf, size);
    fat32_release();
    return ret;
}

int fat32_chdir(const char *path) {
    fat32_acquire();
    int ret = fat32_chdir_unlocked(path);
    fat32_release();
    return ret;
}
