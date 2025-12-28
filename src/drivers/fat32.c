#include "fat32.h"
#include "ata.h"
#include "../mem/heap.h"
#include "../console.h"

#define FAT32_EOC 0x0FFFFFFF
#define FAT32_BAD 0x0FFFFFF7

typedef struct {
    uint8_t jump[3];
    char oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint8_t media_type;
    uint16_t sectors_per_fat_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t sectors_per_fat_32;
    uint16_t flags;
    uint16_t version;
    uint32_t root_cluster;
    uint16_t fsinfo_sector;
    uint16_t backup_boot_sector;
    uint8_t reserved[12];
    uint8_t drive_number;
    uint8_t reserved1;
    uint8_t boot_signature;
    uint32_t volume_id;
    char volume_label[11];
    char fs_type[8];
} __attribute__((packed)) fat32_bpb_t;

typedef struct {
    char name[11];
    uint8_t attr;
    uint8_t nt_reserved;
    uint8_t created_time_tenth;
    uint16_t created_time;
    uint16_t created_date;
    uint16_t accessed_date;
    uint16_t first_cluster_high;
    uint16_t modified_time;
    uint16_t modified_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} __attribute__((packed)) fat32_direntry_t;

typedef struct {
    uint8_t order;
    uint16_t name1[5];
    uint8_t attr;
    uint8_t type;
    uint8_t checksum;
    uint16_t name2[6];
    uint16_t first_cluster;
    uint16_t name3[2];
} __attribute__((packed)) lfn_entry_t;

typedef struct {
    uint8_t  status;
    uint8_t  chs_first[3];
    uint8_t  type;
    uint8_t  chs_last[3];
    uint32_t lba_first;
    uint32_t sectors_total;
} __attribute__((packed)) mbr_partition_t;

static fat32_volume_t volumes[FAT32_MAX_VOLUMES];
static fat32_file_t open_files[FAT32_MAX_OPEN_FILES];
static char current_dir[FAT32_MAX_PATH] = "C:/";

static uint8_t fat_dirty[FAT32_MAX_VOLUMES];
static uint32_t last_alloc[FAT32_MAX_VOLUMES];

static fat32_volume_t* get_volume(uint8_t drive) {
    for (int i = 0; i < FAT32_MAX_VOLUMES; i++) {
        if (volumes[i].mounted && volumes[i].drive_letter == drive)
            return &volumes[i];
    }
    return NULL;
}

static int volume_index(fat32_volume_t *vol) {
    ptrdiff_t idx = vol - volumes;
    if (idx < 0 || idx >= FAT32_MAX_VOLUMES) return -1;
    return (int)idx;
}

static int write_cluster(fat32_volume_t *vol, uint32_t cluster, const void *buffer);

static uint8_t lfn_checksum(const char *short_name) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = ((sum & 1) << 7) + (sum >> 1) + (uint8_t)short_name[i];
    }
    return sum;
}

static void utf16_to_ascii(const uint16_t *src, char *dst, int max_chars) {
    for (int i = 0; i < max_chars && src[i] != 0 && src[i] != 0xFFFF; i++) {
        dst[i] = (src[i] < 128) ? (char)src[i] : '?';
    }
}

static void ascii_to_utf16(const char *src, uint16_t *dst, int max_chars) {
    int i;
    for (i = 0; i < max_chars && src[i]; i++) {
        dst[i] = (uint16_t)(uint8_t)src[i];
    }
    for (; i < max_chars; i++) {
        dst[i] = 0xFFFF;
    }
}

static int parse_path(const char *path, uint8_t *drive, char *rest, size_t rest_size) {
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

static uint32_t cluster_to_lba(fat32_volume_t *vol, uint32_t cluster) {
    return vol->first_data_sector + (cluster - 2) * vol->sectors_per_cluster;
}

static uint32_t get_next_cluster(fat32_volume_t *vol, uint32_t cluster) {
    if (!vol->fat_cache) return FAT32_EOC;
    if (cluster < 2) return FAT32_EOC;
    uint32_t entries = vol->fat_cache_size / 4;
    if (cluster >= entries) return FAT32_EOC;
    uint32_t val;
    memcpy_s(&val, vol->fat_cache + cluster * 4, 4);
    return (val & 0x0FFFFFFF);
}

static void set_next_cluster(fat32_volume_t *vol, uint32_t cluster, uint32_t value) {
    if (!vol->fat_cache || cluster < 2) return;
    uint32_t entries = vol->fat_cache_size / 4;
    if (cluster >= entries) return;
    
    uint32_t cur;
    memcpy_s(&cur, vol->fat_cache + cluster * 4, 4);
    uint32_t newval = (cur & 0xF0000000) | (value & 0x0FFFFFFF);
    memcpy_s(vol->fat_cache + cluster * 4, &newval, 4);
    
    int idx = volume_index(vol);
    if (idx >= 0) fat_dirty[idx] = 1;
}

static uint32_t alloc_cluster(fat32_volume_t *vol) {
    if (!vol->fat_cache) return 0;
    uint32_t entries = vol->fat_cache_size / 4;
    if (entries <= 2) return 0;
    
    int idx = volume_index(vol);
    uint32_t start = (idx >= 0 && last_alloc[idx] >= 2 && last_alloc[idx] < entries) 
                     ? last_alloc[idx] + 1 : 2;
    if (start >= entries) start = 2;
    
    for (uint32_t pass = 0; pass < 2; pass++) {
        for (uint32_t cur = start; cur < entries; cur++) {
            uint32_t val;
            memcpy_s(&val, vol->fat_cache + cur * 4, 4);
            if ((val & 0x0FFFFFFF) == 0) {
                set_next_cluster(vol, cur, FAT32_EOC);
                if (idx >= 0) last_alloc[idx] = cur;
                return cur;
            }
        }
        start = 2;
    }
    return 0;
}

static void free_cluster_chain(fat32_volume_t *vol, uint32_t start) {
    if (!vol->fat_cache || start < 2) return;
    uint32_t entries = vol->fat_cache_size / 4;
    uint32_t cluster = start;
    uint32_t safety = 0;
    
    size_t cluster_size = vol->sectors_per_cluster * vol->bytes_per_sector;
    uint8_t *zero_buf = kmalloc(cluster_size);
    if (zero_buf) {
        memset_s(zero_buf, 0, cluster_size);
    }
    
    while (cluster >= 2 && cluster < FAT32_EOC && safety < entries) {
        uint32_t next = get_next_cluster(vol, cluster);
        
        if (zero_buf) {
            write_cluster(vol, cluster, zero_buf);
        }
        
        set_next_cluster(vol, cluster, 0);
        cluster = next;
        safety++;
    }
    
    if (zero_buf) {
        kfree(zero_buf);
    }
}

static int sync_fat(fat32_volume_t *vol) {
    if (!vol->fat_cache) return -1;
    int idx = volume_index(vol);
    if (idx >= 0 && !fat_dirty[idx]) return 0;
    
    uint32_t sectors = vol->sectors_per_fat;
    uint32_t bps = vol->bytes_per_sector;
    if (bps == 0) return -1;
    
    for (uint8_t copy = 0; copy < vol->num_fats; copy++) {
        uint32_t base = vol->first_fat_sector + copy * vol->sectors_per_fat;
        for (uint32_t i = 0; i < sectors; i++) {
            if (ata_write_sectors(base + i, 1, vol->fat_cache + i * bps) != 0)
                return -1;
        }
    }
    
    if (idx >= 0) fat_dirty[idx] = 0;
    return 0;
}

static void parse_filename(const char *name, char *out_name) {
    memset_s(out_name, ' ', 11);
    int i = 0, j = 0;
    
    while (name[i] && name[i] != '.' && j < 8) {
        out_name[j++] = toupper_s(name[i++]);
    }
    
    if (name[i] == '.') {
        i++;
        j = 8;
        while (name[i] && j < 11) {
            out_name[j++] = toupper_s(name[i++]);
        }
    }
}

static int read_cluster(fat32_volume_t *vol, uint32_t cluster, void *buffer) {
    if (cluster < 2 || cluster >= FAT32_EOC) return -1;
    uint32_t lba = cluster_to_lba(vol, cluster);
    for (uint32_t i = 0; i < vol->sectors_per_cluster; i++) {
        if (ata_read_sectors(lba + i, 1, (uint8_t*)buffer + i * vol->bytes_per_sector) != 0)
            return -1;
    }
    return 0;
}

static int write_cluster(fat32_volume_t *vol, uint32_t cluster, const void *buffer) {
    if (cluster < 2 || cluster >= FAT32_EOC) return -1;
    uint32_t lba = cluster_to_lba(vol, cluster);
    for (uint32_t i = 0; i < vol->sectors_per_cluster; i++) {
        if (ata_write_sectors(lba + i, 1, (const uint8_t*)buffer + i * vol->bytes_per_sector) != 0)
            return -1;
    }
    return 0;
}

static int find_in_dir(fat32_volume_t *vol, uint32_t dir_cluster, const char *name, 
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

static int navigate_path(fat32_volume_t *vol, const char *path, uint32_t *out_dir_cluster, char *out_filename) {
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

static int add_dir_entry(fat32_volume_t *vol, uint32_t dir_cluster, const char *name, 
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

static int remove_dir_entry(fat32_volume_t *vol, uint32_t dir_cluster, const char *name) {
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

void fat32_init(void) {
    memset_s(volumes, 0, sizeof(volumes));
    memset_s(open_files, 0, sizeof(open_files));
    memset_s(fat_dirty, 0, sizeof(fat_dirty));
    for (int i = 0; i < FAT32_MAX_VOLUMES; i++) last_alloc[i] = 2;
}

int fat32_mount_drive(uint8_t drive_letter, uint32_t start_lba) {
    drive_letter = toupper_s(drive_letter);
    fat32_volume_t *vol = NULL;
    
    for (int i = 0; i < FAT32_MAX_VOLUMES; i++) {
        if (!volumes[i].mounted) {
            vol = &volumes[i];
            break;
        }
    }
    if (!vol) return -1;
    
    uint8_t *sector = kmalloc(512);
    if (!sector) return -1;
    
    if (ata_read_sectors(start_lba, 1, sector) != 0) {
        kfree(sector);
        return -1;
    }
    
    fat32_bpb_t *bpb = (fat32_bpb_t*)sector;
    
    vol->drive_letter = drive_letter;
    vol->bytes_per_sector = bpb->bytes_per_sector;
    vol->sectors_per_cluster = bpb->sectors_per_cluster;
    vol->num_fats = bpb->num_fats;
    vol->sectors_per_fat = bpb->sectors_per_fat_32;
    vol->root_cluster = bpb->root_cluster;
    vol->first_fat_sector = start_lba + bpb->reserved_sectors;
    
    uint32_t root_dir_sectors = ((bpb->root_entries * 32) + (vol->bytes_per_sector - 1)) / vol->bytes_per_sector;
    vol->first_data_sector = vol->first_fat_sector + (vol->num_fats * vol->sectors_per_fat) + root_dir_sectors;
    
    kfree(sector);
    
    if (vol->sectors_per_fat == 0 || vol->bytes_per_sector == 0) return -1;
    
    vol->fat_cache_size = vol->sectors_per_fat * vol->bytes_per_sector;
    vol->fat_cache = kmalloc(vol->fat_cache_size);
    if (!vol->fat_cache) return -1;
    
    for (uint32_t i = 0; i < vol->sectors_per_fat; i++) {
        if (ata_read_sectors(vol->first_fat_sector + i, 1, vol->fat_cache + i * vol->bytes_per_sector) != 0) {
            kfree(vol->fat_cache);
            vol->fat_cache = NULL;
            return -1;
        }
    }
    
    int idx = volume_index(vol);
    if (idx >= 0) {
        fat_dirty[idx] = 0;
        last_alloc[idx] = 2;
    }
    
    vol->mounted = 1;
    return 0;
}

int fat32_unmount_drive(uint8_t drive_letter) {
    fat32_volume_t *vol = get_volume(toupper_s(drive_letter));
    if (!vol) return -1;
    
    sync_fat(vol);
    
    if (vol->fat_cache) {
        kfree(vol->fat_cache);
        vol->fat_cache = NULL;
    }
    
    vol->mounted = 0;
    int idx = volume_index(vol);
    if (idx >= 0) {
        fat_dirty[idx] = 0;
        last_alloc[idx] = 2;
    }
    return 0;
}

fat32_file_t* fat32_open(const char *path, const char *mode) {
    if (!path || !mode) return NULL;
    
    uint8_t drive;
    char rest[FAT32_MAX_PATH];
    if (parse_path(path, &drive, rest, sizeof(rest)) != 0) return NULL;
    
    fat32_volume_t *vol = get_volume(drive);
    if (!vol) return NULL;
    
    fat32_file_t *file = NULL;
    for (int i = 0; i < FAT32_MAX_OPEN_FILES; i++) {
        if (!open_files[i].in_use) {
            file = &open_files[i];
            break;
        }
    }
    if (!file) return NULL;
    
    uint32_t dir_cluster;
    char filename[FAT32_MAX_PATH];
    if (navigate_path(vol, rest, &dir_cluster, filename) != 0) return NULL;
    
    if (filename[0] == '\0') return NULL;
    
    fat32_direntry_t entry;
    int found = find_in_dir(vol, dir_cluster, filename, &entry, NULL, NULL);
    
    if (mode[0] == 'r' && found != 0) return NULL;
    
    if (mode[0] == 'w') {
        if (found != 0) {
            uint32_t new_cluster = alloc_cluster(vol);
            if (new_cluster == 0) return NULL;
            
            if (add_dir_entry(vol, dir_cluster, filename, new_cluster, 0, FAT_ATTR_ARCHIVE) != 0) {
                free_cluster_chain(vol, new_cluster);
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
    
    return file;
}

int fat32_read(fat32_file_t *file, void *buffer, size_t size) {
    if (!file || !file->in_use || !buffer) return -1;
    if (file->position >= file->size) return 0;
    
    fat32_volume_t *vol = get_volume(file->drive);
    if (!vol) return -1;
    
    size_t bytes_read = 0;
    size_t to_read = size;
    if (file->position + to_read > file->size)
        to_read = file->size - file->position;
    
    size_t cluster_size = vol->sectors_per_cluster * vol->bytes_per_sector;
    uint8_t *cluster_buf = kmalloc(cluster_size);
    if (!cluster_buf) return -1;
    
    while (to_read > 0 && file->current_cluster >= 2 && file->current_cluster < FAT32_EOC) {
        if (read_cluster(vol, file->current_cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
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
    return (int)bytes_read;
}

int fat32_write(fat32_file_t *file, const void *buffer, size_t size) {
    if (!file || !file->in_use || !buffer) return -1;
    if (file->mode != 'w' && file->mode != 'a') return -1;
    
    fat32_volume_t *vol = get_volume(file->drive);
    if (!vol) return -1;
    
    size_t bytes_written = 0;
    size_t cluster_size = vol->sectors_per_cluster * vol->bytes_per_sector;
    uint8_t *cluster_buf = kmalloc(cluster_size);
    if (!cluster_buf) return -1;
    
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
                return -1;
            }
        }
        
        uint32_t available = cluster_size - file->cluster_offset;
        uint32_t chunk = (size < available) ? (uint32_t)size : available;
        
        memcpy_s(cluster_buf + file->cluster_offset, (const uint8_t*)buffer + bytes_written, chunk);
        
        if (write_cluster(vol, file->current_cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
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
    return (int)bytes_written;
}

int fat32_seek(fat32_file_t *file, uint32_t offset) {
    if (!file || !file->in_use) return -1;
    
    fat32_volume_t *vol = get_volume(file->drive);
    if (!vol) return -1;
    
    file->position = offset;
    file->current_cluster = file->first_cluster;
    file->cluster_offset = 0;
    
    uint32_t cluster_size = vol->sectors_per_cluster * vol->bytes_per_sector;
    while (offset >= cluster_size && file->current_cluster < FAT32_EOC) {
        file->current_cluster = get_next_cluster(vol, file->current_cluster);
        offset -= cluster_size;
    }
    
    file->cluster_offset = offset;
    return 0;
}

uint32_t fat32_tell(fat32_file_t *file) {
    return file ? file->position : 0;
}

void fat32_close(fat32_file_t *file) {
    if (!file || !file->in_use) return;
    
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
                                write_cluster(vol, cluster, cluster_buf);
                            }
                            kfree(cluster_buf);
                        }
                    }
                }
            }
        }
    }
    
    file->in_use = 0;
}

int fat32_list_dir(const char *path, fat32_dirent_t *entries, int max_entries) {
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

int fat32_mkdir(const char *path) {
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

int fat32_rmdir(const char *path) {
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

int fat32_unlink(const char *path) {
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

int fat32_stat(const char *path, fat32_dirent_t *entry) {
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
        entry->is_directory = 1;
        entry->attr = FAT_ATTR_DIRECTORY;
        return 0;
    }
    
    fat32_direntry_t raw_entry;
    if (find_in_dir(vol, dir_cluster, filename, &raw_entry, NULL, NULL) != 0) return -1;
    
    strcpy_s(entry->name, filename, sizeof(entry->name));
    entry->size = raw_entry.file_size;
    entry->first_cluster = ((uint32_t)raw_entry.first_cluster_high << 16) | raw_entry.first_cluster_low;
    entry->is_directory = (raw_entry.attr & FAT_ATTR_DIRECTORY) ? 1 : 0;
    entry->attr = raw_entry.attr;
    return 0;
}

char* fat32_getcwd(char *buf, size_t size) {
    if (!buf || size == 0) return NULL;
    strcpy_s(buf, current_dir, size);
    return buf;
}

int fat32_chdir(const char *path) {
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

static int is_valid_fat32_bpb(const uint8_t *sector) {
    if (!sector) return 0;

    if (sector[510] != 0x55 || sector[511] != 0xAA)
        return 0;

    if (sector[0] != 0xEB && sector[0] != 0xE9)
        return 0;

    const fat32_bpb_t *bpb = (const fat32_bpb_t*)sector;

    if (bpb->bytes_per_sector == 0)
        return 0;
    if (bpb->sectors_per_cluster == 0)
        return 0;
    if (bpb->sectors_per_fat_32 == 0)
        return 0;
    if (bpb->root_cluster < 2)
        return 0;

    return 1;
}

static int find_fat32_start_lba(uint32_t *out_start_lba) {
    if (!out_start_lba) return -1;

    uint8_t *sector = kmalloc(512);
    if (!sector) return -1;

    uint32_t boot_part = 0;

    if (boot_device != 0xFFFFFFFF) {
        uint32_t boot_drive = (boot_device >> 24) & 0xFF;
        boot_part = (boot_device >> 16) & 0xFF;

        // Accept any BIOS hard drive (0x80-0xFF)
        // We only support primary ATA anyway, so just verify it's a hard drive
        if (boot_drive < 0x80) {
            kfree(sector);
            return -1;
        }
    }

    // Try reading the MBR/boot sector
    if (ata_read_sectors(0, 1, sector) != 0) {
        kfree(sector);
        return -1;
    }

    if (is_valid_fat32_bpb(sector)) {
        *out_start_lba = 0;
        kfree(sector);
        return 0;
    }

    if (sector[510] != 0x55 || sector[511] != 0xAA) {
        kfree(sector);
        return -1;
    }

    mbr_partition_t *parts = (mbr_partition_t *)(sector + 446);

    if (boot_part > 0 && boot_part <= 4) {
        int idx = boot_part - 1;
        uint32_t p_lba = parts[idx].lba_first;
        uint32_t p_sectors = parts[idx].sectors_total;
        
        if (p_lba != 0 && p_sectors != 0) {
            if (ata_read_sectors(p_lba, 1, sector) == 0) {
                if (is_valid_fat32_bpb(sector)) {
                    *out_start_lba = p_lba;
                    kfree(sector);
                    return 0;
                }
            }
        }
    }

    for (int i = 0; i < 4; i++) {
        uint32_t p_lba = parts[i].lba_first;
        uint32_t p_sectors = parts[i].sectors_total;

        if (p_lba == 0 || p_sectors == 0)
            continue;

        if (ata_read_sectors(p_lba, 1, sector) != 0)
            continue;

        if (is_valid_fat32_bpb(sector)) {
            *out_start_lba = p_lba;
            kfree(sector);
            return 0;
        }
    }

    kfree(sector);
    return -1;
}

int fat32_mount_auto(uint8_t drive_letter) {
    uint32_t start_lba;
    if (find_fat32_start_lba(&start_lba) != 0) {
        return -1;
    }
    return fat32_mount_drive(drive_letter, start_lba);
}