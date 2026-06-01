#include "private.h"

int find_in_dir(fat32_volume_t *vol, uint32_t dir_cluster, const char *name, 
                       fat32_direntry_t *out, uint32_t *out_cluster, uint32_t *out_offset) {
    char search[11];
    parse_filename(name, search);
    
    size_t cluster_size = vol->sectors_per_cluster * vol->bytes_per_sector;
    uint8_t *cluster_buf = kmalloc(cluster_size);
    if (!cluster_buf) return -1;
    
    uint32_t cluster = dir_cluster;
    while (cluster >= 2 && cluster < FAT32_EOC) {
        if (read_cluster(vol, cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
            return -1;
        }
        
        fat32_direntry_t *entries = (fat32_direntry_t*)cluster_buf;
        uint32_t count = cluster_size / 32;
        char lfn_buffer[FAT32_MAX_PATH];
        uint8_t lfn_checksum_val = 0;
        int lfn_valid = 0;
        
        for (uint32_t i = 0; i < count; i++) {
            if (entries[i].name[0] == 0x00) {
                kfree(cluster_buf);
                return -1;
            }
            if ((uint8_t)entries[i].name[0] == 0xE5) {
                lfn_valid = 0;
                continue;
            }
            
            if (entries[i].attr == FAT_ATTR_LFN) {
                lfn_entry_t *lfn = (lfn_entry_t*)&entries[i];
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
            
            if (entries[i].attr & FAT_ATTR_VOLUME_ID) {
                lfn_valid = 0;
                continue;
            }
            
            if (lfn_valid && lfn_checksum(entries[i].name) == lfn_checksum_val) {
                if (strcasecmp_s(lfn_buffer, name) == 0) {
                    memcpy_s(out, &entries[i], sizeof(fat32_direntry_t));
                    if (out_cluster) *out_cluster = cluster;
                    if (out_offset) *out_offset = i * 32;
                    kfree(cluster_buf);
                    return 0;
                }
            }
            
            int match = 1;
            for (int j = 0; j < 11; j++) {
                char a = toupper_s(entries[i].name[j]);
                char b = toupper_s(search[j]);
                if (a != b) {
                    match = 0;
                    break;
                }
            }
            
            if (match) {
                memcpy_s(out, &entries[i], sizeof(fat32_direntry_t));
                if (out_cluster) *out_cluster = cluster;
                if (out_offset) *out_offset = i * 32;
                kfree(cluster_buf);
                return 0;
            }
            
            lfn_valid = 0;
        }
        
        cluster = get_next_cluster(vol, cluster);
    }
    
    kfree(cluster_buf);
    return -1;
}

int add_dir_entry(fat32_volume_t *vol, uint32_t dir_cluster, const char *name, 
                        uint32_t first_cluster, uint32_t size, uint8_t attr) {
    char short_name[11];
    parse_filename(name, short_name);
    
    int name_len = strlen_s(name);
    int needs_lfn = 0;
    
    for (int i = 0; i < name_len; i++) {
        if (name[i] == ' ' || (uint8_t)name[i] > 127) {
            needs_lfn = 1;
            break;
        }
    }
    
    int dot_count = 0;
    for (int i = 0; i < name_len; i++) {
        if (name[i] == '.') dot_count++;
    }
    if (dot_count > 1) needs_lfn = 1;
    
    if (!needs_lfn) {
        char *dot = NULL;
        for (int i = 0; name[i]; i++) {
            if (name[i] == '.') {
                dot = (char*)&name[i];
                break;
            }
        }
        
        int base_len = dot ? (int)(dot - name) : name_len;
        int ext_len = dot ? (name_len - base_len - 1) : 0;
        
        if (base_len > 8 || ext_len > 3) needs_lfn = 1;
    }
    
    int lfn_entries = needs_lfn ? ((name_len + 12) / 13) : 0;
    int total_entries = lfn_entries + 1;
    
    size_t cluster_size = vol->sectors_per_cluster * vol->bytes_per_sector;
    uint8_t *cluster_buf = kmalloc(cluster_size);
    if (!cluster_buf) return -1;
    
    uint32_t cluster = dir_cluster;
    while (cluster >= 2 && cluster < FAT32_EOC) {
            if (read_cluster(vol, cluster, cluster_buf) != 0) {
                kfree(cluster_buf);
                return -1;
            }
        
        fat32_direntry_t *entries = (fat32_direntry_t*)cluster_buf;
        uint32_t count = cluster_size / 32;
        int free_start = -1;
        int free_count = 0;
        
        for (uint32_t i = 0; i < count; i++) {
            if (entries[i].name[0] == 0x00 || (uint8_t)entries[i].name[0] == 0xE5) {
                if (free_start < 0) free_start = i;
                free_count++;
                if (free_count >= total_entries) break;
            } else {
                free_start = -1;
                free_count = 0;
            }
        }
        
        if (free_count >= total_entries) {
            uint8_t checksum = lfn_checksum(short_name);
            
            if (needs_lfn) {
                for (int lfn_idx = lfn_entries - 1; lfn_idx >= 0; lfn_idx--) {
                    lfn_entry_t *lfn = (lfn_entry_t*)&entries[free_start + (lfn_entries - 1 - lfn_idx)];
                    memset_s(lfn, 0, 32);
                    
                    lfn->order = (lfn_idx + 1) | (lfn_idx == lfn_entries - 1 ? 0x40 : 0);
                    lfn->attr = FAT_ATTR_LFN;
                    lfn->type = 0;
                    lfn->checksum = checksum;
                    lfn->first_cluster = 0;
                    
                    int base = lfn_idx * 13;
                    uint16_t temp1[5], temp2[6], temp3[2];
                    ascii_to_utf16(name + base, temp1, 5);
                    ascii_to_utf16(name + base + 5, temp2, 6);
                    ascii_to_utf16(name + base + 11, temp3, 2);
                    memcpy_s(lfn->name1, temp1, sizeof(temp1));
                    memcpy_s(lfn->name2, temp2, sizeof(temp2));
                    memcpy_s(lfn->name3, temp3, sizeof(temp3));
                }
            }
            
            fat32_direntry_t *entry = &entries[free_start + lfn_entries];
            memset_s(entry, 0, 32);
            memcpy_s(entry->name, short_name, 11);
            entry->attr = attr;
            entry->first_cluster_high = (uint16_t)(first_cluster >> 16);
            entry->first_cluster_low = (uint16_t)(first_cluster & 0xFFFF);
            entry->file_size = size;

            /* Set creation and modification timestamps */
            uint16_t fat_time, fat_date;
            get_fat_timestamp(&fat_time, &fat_date);
            entry->created_time = fat_time;
            entry->created_date = fat_date;
            entry->modified_time = fat_time;
            entry->modified_date = fat_date;
            entry->accessed_date = fat_date;
            
            if (write_cluster(vol, cluster, cluster_buf) != 0) {
                kfree(cluster_buf);
                return -1;
            }

            kfree(cluster_buf);
            return 0;
        }
        
        uint32_t next = get_next_cluster(vol, cluster);
        if (next >= FAT32_EOC) {
            uint32_t new_cluster = alloc_cluster(vol);
            if (new_cluster == 0) {
                kfree(cluster_buf);
                return -1;
            }
            
            set_next_cluster(vol, cluster, new_cluster);
            memset_s(cluster_buf, 0, cluster_size);
            if (write_cluster(vol, new_cluster, cluster_buf) != 0) {
                kfree(cluster_buf);
                return -1;
            }
            cluster = new_cluster;
        } else {
            cluster = next;
        }
    }
    
    kfree(cluster_buf);
    return -1;
}

int remove_dir_entry(fat32_volume_t *vol, uint32_t dir_cluster, const char *name) {
    fat32_direntry_t entry;
    uint32_t cluster;
    uint32_t offset;
    
    if (find_in_dir(vol, dir_cluster, name, &entry, &cluster, &offset) != 0)
        return -1;
    
    size_t cluster_size = vol->sectors_per_cluster * vol->bytes_per_sector;
    uint8_t *cluster_buf = kmalloc(cluster_size);
    if (!cluster_buf) return -1;
    
    if (read_cluster(vol, cluster, cluster_buf) != 0) {
        kfree(cluster_buf);
        return -1;
    }
    
    fat32_direntry_t *entries = (fat32_direntry_t*)cluster_buf;
    uint32_t entry_idx = offset / 32;
    uint8_t checksum = lfn_checksum(entry.name);
    
    for (int i = (int)entry_idx - 1; i >= 0; i--) {
        if (entries[i].attr == FAT_ATTR_LFN) {
            lfn_entry_t *lfn = (lfn_entry_t*)&entries[i];
            if (lfn->checksum == checksum) {
                entries[i].name[0] = (char)0xE5;
            } else {
                break;
            }
        } else {
            break;
        }
    }
    
    entries[entry_idx].name[0] = (char)0xE5;
    
    if (write_cluster(vol, cluster, cluster_buf) != 0) {
        kfree(cluster_buf);
        return -1;
    }
    
    kfree(cluster_buf);
    return 0;
}
