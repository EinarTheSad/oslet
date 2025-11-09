/*  It should be obvious that I didn't write this stuff myself.
    It's Claude by Anthropic. I'm ashamed. But at least it saved
    me months of work, and still taught me something valuable...
    So, in the end, who cares?
*/

#include "fat32.h"
#include "ata.h"
#include "heap.h"
#include "console.h"
#include "string.h"

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
            temp[len] = '/';
            temp[len+1] = '\0';
        }
        
        strcpy_s(temp + strlen_s(temp), path, sizeof(temp) - strlen_s(temp));
        strcpy_s(rest, temp, rest_size);
    } else if (path[0] == '\0' || strcmp_s(path, ".") == 0) {
        strcpy_s(rest, current_dir + 3, rest_size);
    } else {
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
    val = (val & 0x0FFFFFFF);
    return val;
}

static void set_next_cluster(fat32_volume_t *vol, uint32_t cluster, uint32_t value) {
    if (!vol->fat_cache) return;
    if (cluster < 2) return;
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
    uint32_t start = 2;
    if (idx >= 0) {
        if (last_alloc[idx] < 2 || last_alloc[idx] >= entries) last_alloc[idx] = 2;
        start = last_alloc[idx] + 1;
        if (start >= entries) start = 2;
    }
    uint32_t cur = start;
    for (uint32_t pass = 0; pass < 2; pass++) {
        while (cur < entries) {
            uint32_t val;
            memcpy_s(&val, vol->fat_cache + cur * 4, 4);
            val &= 0x0FFFFFFF;
            if (val == 0) {
                uint32_t eoc = FAT32_EOC;
                uint32_t newval = (val & 0xF0000000) | (eoc & 0x0FFFFFFF);
                memcpy_s(vol->fat_cache + cur * 4, &newval, 4);
                if (idx >= 0) {
                    fat_dirty[idx] = 1;
                    last_alloc[idx] = cur;
                }
                return cur;
            }
            cur++;
        }
        cur = 2;
    }
    return 0;
}

static void free_cluster_chain(fat32_volume_t *vol, uint32_t start) {
    if (!vol->fat_cache) return;
    uint32_t entries = vol->fat_cache_size / 4;
    if (entries <= 2) return;
    uint32_t cluster = start;
    int idx = volume_index(vol);
    uint32_t safety = 0;
    while (cluster >= 2 && cluster < FAT32_EOC && safety < entries) {
        uint32_t next;
        memcpy_s(&next, vol->fat_cache + cluster * 4, 4);
        next &= 0x0FFFFFFF;
        uint32_t zero = 0;
        memcpy_s(vol->fat_cache + cluster * 4, &zero, 4);
        if (idx >= 0) fat_dirty[idx] = 1;
        cluster = next;
        safety++;
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
            uint8_t *ptr = vol->fat_cache + i * bps;
            if (ata_write_sectors(base + i, 1, ptr) != 0)
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
    uint32_t lba = cluster_to_lba(vol, cluster);
    for (uint32_t i = 0; i < vol->sectors_per_cluster; i++) {
        if (ata_read_sectors(lba + i, 1, (uint8_t*)buffer + i * vol->bytes_per_sector) != 0)
            return -1;
    }
    return 0;
}

static int write_cluster(fat32_volume_t *vol, uint32_t cluster, const void *buffer) {
    uint32_t lba = cluster_to_lba(vol, cluster);
    for (uint32_t i = 0; i < vol->sectors_per_cluster; i++) {
        if (ata_write_sectors(lba + i, 1, (const uint8_t*)buffer + i * vol->bytes_per_sector) != 0)
            return -1;
    }
    return 0;
}

static int find_in_dir(fat32_volume_t *vol, uint32_t dir_cluster, const char *name, fat32_direntry_t *out) {
    char search[11];
    parse_filename(name, search);
    uint8_t *cluster_buf = kmalloc(vol->sectors_per_cluster * vol->bytes_per_sector);
    if (!cluster_buf) return -1;
    uint32_t cluster = dir_cluster;
    while (cluster >= 2 && cluster < FAT32_EOC) {
        if (read_cluster(vol, cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
            return -1;
        }
        fat32_direntry_t *entries = (fat32_direntry_t*)cluster_buf;
        uint32_t count = (vol->sectors_per_cluster * vol->bytes_per_sector) / 32;
        for (uint32_t i = 0; i < count; i++) {
            if (entries[i].name[0] == 0x00) {
                kfree(cluster_buf);
                return -1;
            }
            if ((uint8_t)entries[i].name[0] == 0xE5) continue;
            if (entries[i].attr == FAT_ATTR_LFN) continue;
            int match = 1;
            for (int j = 0; j < 11; j++) {
                if (entries[i].name[j] != search[j]) {
                    match = 0;
                    break;
                }
            }
            if (match) {
                memcpy_s(out, &entries[i], sizeof(fat32_direntry_t));
                kfree(cluster_buf);
                return 0;
            }
        }
        cluster = get_next_cluster(vol, cluster);
    }
    kfree(cluster_buf);
    return -1;
}

static int navigate_path(fat32_volume_t *vol, const char *path, uint32_t *out_dir_cluster, char *out_filename) {
    *out_dir_cluster = vol->root_cluster;
    if (!path || path[0] == '\0' || path[0] == '.' || (path[0] == '/' && path[1] == '\0')) {
        if (out_filename) out_filename[0] = '\0';
        return 0;
    }
    char temp[FAT32_MAX_PATH];
    strcpy_s(temp, path, sizeof(temp));
    char *p = temp;
    if (*p == '/') p++;
    char *token = p;
    char *next_slash;
    while (*p) {
        next_slash = NULL;
        for (char *s = p; *s; s++) {
            if (*s == '/') {
                next_slash = s;
                break;
            }
        }
        if (next_slash) {
            *next_slash = '\0';
            if (token[0] != '\0') {
                if (strcmp_s(token, "..") == 0) {
                    uint8_t *cluster_buf = kmalloc(vol->sectors_per_cluster * vol->bytes_per_sector);
                    if (!cluster_buf) return -1;
                    if (read_cluster(vol, *out_dir_cluster, cluster_buf) != 0) {
                        kfree(cluster_buf);
                        return -1;
                    }
                    fat32_direntry_t *entries = (fat32_direntry_t*)cluster_buf;
                    *out_dir_cluster = ((uint32_t)entries[1].first_cluster_high << 16) | entries[1].first_cluster_low;
                    if (*out_dir_cluster == 0)
                        *out_dir_cluster = vol->root_cluster;
                    kfree(cluster_buf);
                } else if (strcmp_s(token, ".") != 0) {
                    fat32_direntry_t entry;
                    if (find_in_dir(vol, *out_dir_cluster, token, &entry) != 0)
                        return -1;
                    if (!(entry.attr & FAT_ATTR_DIRECTORY))
                        return -1;
                    *out_dir_cluster = ((uint32_t)entry.first_cluster_high << 16) | entry.first_cluster_low;
                }
            }
            token = next_slash + 1;
            p = token;
        } else {
            if (out_filename) {
                strcpy_s(out_filename, token, FAT32_MAX_PATH);
            }
            break;
        }
    }
    return 0;
}

static int add_dir_entry(fat32_volume_t *vol, uint32_t dir_cluster, const char *name, uint32_t first_cluster, uint32_t size, uint8_t attr) {
    char fname[11];
    parse_filename(name, fname);
    uint8_t *cluster_buf = kmalloc(vol->sectors_per_cluster * vol->bytes_per_sector);
    if (!cluster_buf) return -1;
    uint32_t cluster = dir_cluster;
    while (cluster >= 2 && cluster < FAT32_EOC) {
        if (read_cluster(vol, cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
            return -1;
        }
        fat32_direntry_t *entries = (fat32_direntry_t*)cluster_buf;
        uint32_t count = (vol->sectors_per_cluster * vol->bytes_per_sector) / 32;
        for (uint32_t i = 0; i < count; i++) {
            if (entries[i].name[0] == 0x00 || (uint8_t)entries[i].name[0] == 0xE5) {
                memcpy_s(entries[i].name, fname, 11);
                entries[i].attr = attr;
                entries[i].nt_reserved = 0;
                entries[i].created_time_tenth = 0;
                entries[i].created_time = 0;
                entries[i].created_date = 0;
                entries[i].accessed_date = 0;
                entries[i].first_cluster_high = (uint16_t)(first_cluster >> 16);
                entries[i].modified_time = 0;
                entries[i].modified_date = 0;
                entries[i].first_cluster_low = (uint16_t)(first_cluster & 0xFFFF);
                entries[i].file_size = size;
                if (write_cluster(vol, cluster, cluster_buf) != 0) {
                    kfree(cluster_buf);
                    return -1;
                }
                kfree(cluster_buf);
                return 0;
            }
        }
        uint32_t next = get_next_cluster(vol, cluster);
        if (next >= FAT32_EOC) {
            uint32_t new_cluster = alloc_cluster(vol);
            if (new_cluster == 0) {
                kfree(cluster_buf);
                return -1;
            }
            set_next_cluster(vol, cluster, new_cluster);
            memset_s(cluster_buf, 0, vol->sectors_per_cluster * vol->bytes_per_sector);
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
    char fname[11];
    parse_filename(name, fname);
    uint8_t *cluster_buf = kmalloc(vol->sectors_per_cluster * vol->bytes_per_sector);
    if (!cluster_buf) return -1;
    uint32_t cluster = dir_cluster;
    while (cluster >= 2 && cluster < FAT32_EOC) {
        if (read_cluster(vol, cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
            return -1;
        }
        fat32_direntry_t *entries = (fat32_direntry_t*)cluster_buf;
        uint32_t count = (vol->sectors_per_cluster * vol->bytes_per_sector) / 32;
        for (uint32_t i = 0; i < count; i++) {
            if (entries[i].name[0] == 0x00) break;
            if ((uint8_t)entries[i].name[0] == 0xE5) continue;
            int match = 1;
            for (int j = 0; j < 11; j++) {
                if (entries[i].name[j] != fname[j]) {
                    match = 0;
                    break;
                }
            }
            if (match) {
                entries[i].name[0] = (char)0xE5;
                if (write_cluster(vol, cluster, cluster_buf) != 0) {
                    kfree(cluster_buf);
                    return -1;
                }
                kfree(cluster_buf);
                return 0;
            }
        }
        cluster = get_next_cluster(vol, cluster);
    }
    kfree(cluster_buf);
    return -1;
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
    if (!vol) {
        printf("No free volume slots\n");
        return -1;
    }
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
    if (!path) return NULL;
    uint8_t drive;
    char rest[FAT32_MAX_PATH];
    if (parse_path(path, &drive, rest, sizeof(rest)) != 0)
        return NULL;
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
    if (navigate_path(vol, rest, &dir_cluster, filename) != 0)
        return NULL;
    fat32_direntry_t entry;
    int found = find_in_dir(vol, dir_cluster, filename, &entry);
    if (mode[0] == 'r' && found != 0) return NULL;
    if (mode[0] == 'w' && found != 0) {
        uint32_t new_cluster = alloc_cluster(vol);
        if (new_cluster == 0) return NULL;
        if (add_dir_entry(vol, dir_cluster, filename, new_cluster, 0, FAT_ATTR_ARCHIVE) != 0) {
            free_cluster_chain(vol, new_cluster);
            return NULL;
        }
        sync_fat(vol);
        file->first_cluster = new_cluster;
        file->size = 0;
    } else if (found == 0) {
        file->first_cluster = ((uint32_t)entry.first_cluster_high << 16) | entry.first_cluster_low;
        file->size = entry.file_size;
        if (mode[0] == 'w') {
            uint32_t first = file->first_cluster;
            uint32_t next = get_next_cluster(vol, first);
            if (next < FAT32_EOC) {
                free_cluster_chain(vol, next);
                set_next_cluster(vol, first, FAT32_EOC);
            }
            file->size = 0;
            sync_fat(vol);
        }
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
    uint8_t *cluster_buf = kmalloc(vol->sectors_per_cluster * vol->bytes_per_sector);
    if (!cluster_buf) return -1;
    while (to_read > 0 && file->current_cluster >= 2 && file->current_cluster < FAT32_EOC) {
        if (read_cluster(vol, file->current_cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
            return -1;
        }
        uint32_t cluster_size = vol->sectors_per_cluster * vol->bytes_per_sector;
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
    uint8_t *cluster_buf = kmalloc(vol->sectors_per_cluster * vol->bytes_per_sector);
    if (!cluster_buf) return -1;
    while (size > 0) {
        if (file->current_cluster >= FAT32_EOC || file->current_cluster < 2) {
            uint32_t new_cluster = alloc_cluster(vol);
            if (new_cluster == 0) break;
            if (file->current_cluster >= 2 && file->current_cluster < FAT32_EOC)
                set_next_cluster(vol, file->current_cluster, new_cluster);
            else if (file->first_cluster == 0) {
                file->first_cluster = new_cluster;
            }
            file->current_cluster = new_cluster;
            memset_s(cluster_buf, 0, vol->sectors_per_cluster * vol->bytes_per_sector);
            if (write_cluster(vol, new_cluster, cluster_buf) != 0) {
                kfree(cluster_buf);
                return -1;
            }
        }
        uint32_t cluster_size = vol->sectors_per_cluster * vol->bytes_per_sector;
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
                    uint8_t *cluster_buf = kmalloc(vol->sectors_per_cluster * vol->bytes_per_sector);
                    if (cluster_buf) {
                        uint32_t cluster = dir_cluster;
                        char fname[11];
                        parse_filename(filename, fname);
                        while (cluster >= 2 && cluster < FAT32_EOC) {
                            if (read_cluster(vol, cluster, cluster_buf) == 0) {
                                fat32_direntry_t *entries = (fat32_direntry_t*)cluster_buf;
                                uint32_t count = (vol->sectors_per_cluster * vol->bytes_per_sector) / 32;
                                for (uint32_t i = 0; i < count; i++) {
                                    if (entries[i].name[0] == 0x00) break;
                                    if ((uint8_t)entries[i].name[0] == 0xE5) continue;
                                    int match = 1;
                                    for (int j = 0; j < 11; j++) {
                                        if (entries[i].name[j] != fname[j]) {
                                            match = 0;
                                            break;
                                        }
                                    }
                                    if (match) {
                                        entries[i].file_size = file->size;
                                        write_cluster(vol, cluster, cluster_buf);
                                        goto done_update;
                                    }
                                }
                            }
                            cluster = get_next_cluster(vol, cluster);
                        }
done_update:
                        kfree(cluster_buf);
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
    if (parse_path(path, &drive, rest, sizeof(rest)) != 0)
        return -1;
    fat32_volume_t *vol = get_volume(drive);
    if (!vol) return -1;
    uint32_t dir_cluster;
    char filename[FAT32_MAX_PATH];
    if (navigate_path(vol, rest, &dir_cluster, filename) != 0)
        return -1;
    if (filename[0] != '\0') {
        fat32_direntry_t entry;
        if (find_in_dir(vol, dir_cluster, filename, &entry) != 0)
            return -1;
        if (!(entry.attr & FAT_ATTR_DIRECTORY))
            return -1;
        dir_cluster = ((uint32_t)entry.first_cluster_high << 16) | entry.first_cluster_low;
    }
    uint8_t *cluster_buf = kmalloc(vol->sectors_per_cluster * vol->bytes_per_sector);
    if (!cluster_buf) return -1;
    int count = 0;
    uint32_t cluster = dir_cluster;
    while (cluster >= 2 && cluster < FAT32_EOC && count < max_entries) {
        if (read_cluster(vol, cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
            return -1;
        }
        fat32_direntry_t *dir_entries = (fat32_direntry_t*)cluster_buf;
        uint32_t entries_count = (vol->sectors_per_cluster * vol->bytes_per_sector) / 32;
        for (uint32_t i = 0; i < entries_count && count < max_entries; i++) {
            if (dir_entries[i].name[0] == 0x00) {
                kfree(cluster_buf);
                return count;
            }
            if ((uint8_t)dir_entries[i].name[0] == 0xE5) continue;
            if (dir_entries[i].attr == FAT_ATTR_LFN) continue;
            if (dir_entries[i].attr & FAT_ATTR_VOLUME_ID) continue;
            if (dir_entries[i].name[0] == '.' && (dir_entries[i].name[1] == ' ' || dir_entries[i].name[1] == '.'))
                continue;
            int j = 0;
            for (int k = 0; k < 8 && dir_entries[i].name[k] != ' '; k++)
                entries[count].name[j++] = dir_entries[i].name[k];
            if (dir_entries[i].name[8] != ' ') {
                entries[count].name[j++] = '.';
                for (int k = 8; k < 11 && dir_entries[i].name[k] != ' '; k++)
                    entries[count].name[j++] = dir_entries[i].name[k];
            }
            entries[count].name[j] = '\0';
            entries[count].size = dir_entries[i].file_size;
            entries[count].first_cluster = ((uint32_t)dir_entries[i].first_cluster_high << 16) | dir_entries[i].first_cluster_low;
            entries[count].is_directory = (dir_entries[i].attr & FAT_ATTR_DIRECTORY) ? 1 : 0;
            entries[count].attr = dir_entries[i].attr;
            count++;
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
    if (parse_path(path, &drive, rest, sizeof(rest)) != 0)
        return -1;
    fat32_volume_t *vol = get_volume(drive);
    if (!vol) return -1;
    uint32_t dir_cluster;
    char dirname[FAT32_MAX_PATH];
    if (navigate_path(vol, rest, &dir_cluster, dirname) != 0)
        return -1;
    if (dirname[0] == '\0') return -1;
    fat32_direntry_t tmp;
    if (find_in_dir(vol, dir_cluster, dirname, &tmp) == 0)
        return -1;
    uint32_t new_cluster = alloc_cluster(vol);
    if (new_cluster == 0) return -1;
    if (add_dir_entry(vol, dir_cluster, dirname, new_cluster, 0, FAT_ATTR_DIRECTORY) != 0) {
        free_cluster_chain(vol, new_cluster);
        return -1;
    }
    uint8_t *cluster_buf = kmalloc(vol->sectors_per_cluster * vol->bytes_per_sector);
    if (!cluster_buf) {
        free_cluster_chain(vol, new_cluster);
        return -1;
    }
    memset_s(cluster_buf, 0, vol->sectors_per_cluster * vol->bytes_per_sector);
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
    if (parse_path(path, &drive, rest, sizeof(rest)) != 0)
        return -1;
    fat32_volume_t *vol = get_volume(drive);
    if (!vol) return -1;
    uint32_t dir_cluster;
    char dirname[FAT32_MAX_PATH];
    if (navigate_path(vol, rest, &dir_cluster, dirname) != 0)
        return -1;
    if (dirname[0] == '\0') return -1;
    fat32_direntry_t entry;
    if (find_in_dir(vol, dir_cluster, dirname, &entry) != 0)
        return -1;
    if (!(entry.attr & FAT_ATTR_DIRECTORY))
        return -1;
    uint32_t target_cluster = ((uint32_t)entry.first_cluster_high << 16) | entry.first_cluster_low;
    uint8_t *cluster_buf = kmalloc(vol->sectors_per_cluster * vol->bytes_per_sector);
    if (!cluster_buf) return -1;
    if (read_cluster(vol, target_cluster, cluster_buf) != 0) {
        kfree(cluster_buf);
        return -1;
    }
    fat32_direntry_t *entries = (fat32_direntry_t*)cluster_buf;
    uint32_t count = (vol->sectors_per_cluster * vol->bytes_per_sector) / 32;
    for (uint32_t i = 2; i < count; i++) {
        if (entries[i].name[0] == 0x00) break;
        if ((uint8_t)entries[i].name[0] != 0xE5) {
            kfree(cluster_buf);
            return -1;
        }
    }
    kfree(cluster_buf);
    if (remove_dir_entry(vol, dir_cluster, dirname) != 0)
        return -1;
    free_cluster_chain(vol, target_cluster);
    sync_fat(vol);
    return 0;
}

int fat32_unlink(const char *path) {
    if (!path) return -1;
    uint8_t drive;
    char rest[FAT32_MAX_PATH];
    if (parse_path(path, &drive, rest, sizeof(rest)) != 0)
        return -1;
    fat32_volume_t *vol = get_volume(drive);
    if (!vol) return -1;
    uint32_t dir_cluster;
    char filename[FAT32_MAX_PATH];
    if (navigate_path(vol, rest, &dir_cluster, filename) != 0)
        return -1;
    if (filename[0] == '\0') return -1;
    fat32_direntry_t entry;
    if (find_in_dir(vol, dir_cluster, filename, &entry) != 0)
        return -1;
    if (entry.attr & FAT_ATTR_DIRECTORY)
        return -1;
    uint32_t first_cluster = ((uint32_t)entry.first_cluster_high << 16) | entry.first_cluster_low;
    if (remove_dir_entry(vol, dir_cluster, filename) != 0)
        return -1;
    if (first_cluster >= 2)
        free_cluster_chain(vol, first_cluster);
    sync_fat(vol);
    return 0;
}

int fat32_stat(const char *path, fat32_dirent_t *entry) {
    if (!path || !entry) return -1;
    uint8_t drive;
    char rest[FAT32_MAX_PATH];
    if (parse_path(path, &drive, rest, sizeof(rest)) != 0)
        return -1;
    fat32_volume_t *vol = get_volume(drive);
    if (!vol) return -1;
    uint32_t dir_cluster;
    char filename[FAT32_MAX_PATH];
    if (navigate_path(vol, rest, &dir_cluster, filename) != 0)
        return -1;
    if (filename[0] == '\0') {
        strcpy_s(entry->name, "/", sizeof(entry->name));
        entry->size = 0;
        entry->first_cluster = vol->root_cluster;
        entry->is_directory = 1;
        entry->attr = FAT_ATTR_DIRECTORY;
        return 0;
    }
    fat32_direntry_t raw_entry;
    if (find_in_dir(vol, dir_cluster, filename, &raw_entry) != 0)
        return -1;
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
    char original_dir[FAT32_MAX_PATH];
    strcpy_s(original_dir, current_dir, sizeof(original_dir));
    if (parse_path(path, &drive, rest, sizeof(rest)) != 0)
        return -1;
    fat32_volume_t *vol = get_volume(drive);
    if (!vol) return -1;
    uint32_t dir_cluster;
    char dirname[FAT32_MAX_PATH];
    if (navigate_path(vol, rest, &dir_cluster, dirname) != 0)
        return -1;
    if (dirname[0] != '\0') {
        fat32_direntry_t entry;
        if (find_in_dir(vol, dir_cluster, dirname, &entry) != 0)
            return -1;
        if (!(entry.attr & FAT_ATTR_DIRECTORY))
            return -1;
    }
    current_dir[0] = drive;
    current_dir[1] = ':';
    current_dir[2] = '/';
    if (rest[0] == '\0' || (rest[0] == '/' && rest[1] == '\0')) {
        current_dir[3] = '\0';
    } else {
        strcpy_s(current_dir + 3, rest, sizeof(current_dir) - 3);
        size_t len = strlen_s(current_dir);
        if (len > 0 && current_dir[len-1] != '/') {
            current_dir[len] = '/';
            current_dir[len+1] = '\0';
        }
    }
    return 0;
}
