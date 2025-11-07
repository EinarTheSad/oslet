#include "fat32.h"
#include "ata.h"
#include "heap.h"
#include "console.h"

#define FAT32_SIGNATURE 0xAA55
#define FAT32_EOC 0x0FFFFFFF

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
} __attribute__((packed)) fat32_boot_sector_t;

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
} __attribute__((packed)) fat32_dir_entry_t;

static fat32_boot_sector_t boot_sector;
static uint32_t first_data_sector;
static uint32_t first_fat_sector;
static uint8_t *fat_cache = NULL;
static fat32_file_t open_files[FAT32_MAX_OPEN_FILES];

static void memcpy_safe(void *dst, const void *src, size_t n) {
    char *d = dst;
    const char *s = src;
    while (n--) *d++ = *s++;
}

static void memset_safe(void *dst, int val, size_t n) {
    char *d = dst;
    while (n--) *d++ = (uint8_t)val;
}

static int strcmp_safe(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *(unsigned char*)a - *(unsigned char*)b;
}

static size_t strlen_safe(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

void fat32_init(void) {
    memset_safe(open_files, 0, sizeof(open_files));
    printf("FAT32: Driver initialized\n");
}

static uint32_t cluster_to_lba(uint32_t cluster) {
    return first_data_sector + (cluster - 2) * boot_sector.sectors_per_cluster;
}

static uint32_t get_next_cluster(uint32_t cluster) {
    if (!fat_cache) return FAT32_EOC;
    
    uint32_t fat_offset = cluster * 4;
    uint32_t *fat_entry = (uint32_t*)(fat_cache + fat_offset);
    
    return (*fat_entry) & 0x0FFFFFFF;
}

static void set_next_cluster(uint32_t cluster, uint32_t value) {
    if (!fat_cache) return;
    
    uint32_t fat_offset = cluster * 4;
    uint32_t *fat_entry = (uint32_t*)(fat_cache + fat_offset);
    
    *fat_entry = (*fat_entry & 0xF0000000) | (value & 0x0FFFFFFF);
}

static uint32_t allocate_cluster(void) {
    if (!fat_cache) return 0;
    
    uint32_t total_clusters = boot_sector.total_sectors_32 / boot_sector.sectors_per_cluster;
    
    for (uint32_t i = 2; i < total_clusters; i++) {
        if (get_next_cluster(i) == 0) {
            set_next_cluster(i, FAT32_EOC);
            return i;
        }
    }
    
    return 0;
}

int fat32_mount(void) {
    uint8_t *sector = (uint8_t*)kmalloc(ATA_SECTOR_SIZE);
    if (!sector) {
        printf("FAT32: Failed to allocate sector buffer\n");
        return -1;
    }
    
    if (ata_read_sectors(0, 1, sector) != 0) {
        printf("FAT32: Failed to read boot sector\n");
        kfree(sector);
        return -1;
    }
    
    memcpy_safe(&boot_sector, sector, sizeof(fat32_boot_sector_t));
    kfree(sector);
    
    printf("FAT32: Bytes/sector = %u\n", boot_sector.bytes_per_sector);
    printf("FAT32: Sectors/cluster = %u\n", boot_sector.sectors_per_cluster);
    printf("FAT32: Reserved sectors = %u\n", boot_sector.reserved_sectors);
    printf("FAT32: Number of FATs = %u\n", boot_sector.num_fats);
    printf("FAT32: Sectors/FAT = %u\n", boot_sector.sectors_per_fat_32);
    
    if (boot_sector.bytes_per_sector != 512) {
        printf("FAT32: Unsupported sector size: %u\n", boot_sector.bytes_per_sector);
        return -1;
    }
    
    if (boot_sector.sectors_per_cluster == 0 || boot_sector.sectors_per_cluster > 128) {
        printf("FAT32: Invalid sectors per cluster: %u\n", boot_sector.sectors_per_cluster);
        return -1;
    }
    
    first_fat_sector = boot_sector.reserved_sectors;
    uint32_t root_dir_sectors = ((boot_sector.root_entries * 32) + 511) / 512;
    first_data_sector = boot_sector.reserved_sectors + 
                       (boot_sector.num_fats * boot_sector.sectors_per_fat_32) + 
                       root_dir_sectors;
    
    printf("FAT32: First FAT sector = %u\n", first_fat_sector);
    printf("FAT32: First data sector = %u\n", first_data_sector);
    printf("FAT32: Root cluster = %u\n", boot_sector.root_cluster);
    
    /* Load FAT into memory - ONLY first 128KB for safety */
    uint32_t fat_sectors_to_load = boot_sector.sectors_per_fat_32;
    if (fat_sectors_to_load > 256) fat_sectors_to_load = 256; /* Limit to 128KB */
    
    uint32_t fat_size = fat_sectors_to_load * 512;
    printf("FAT32: Allocating %u KB for FAT cache\n", fat_size / 1024);
    
    fat_cache = (uint8_t*)kmalloc(fat_size);
    if (!fat_cache) {
        printf("FAT32: Failed to allocate FAT cache\n");
        return -1;
    }
    
    printf("FAT32: Loading FAT table...\n");
    for (uint32_t i = 0; i < fat_sectors_to_load; i++) {
        if (ata_read_sectors(first_fat_sector + i, 1, fat_cache + (i * 512)) != 0) {
            printf("FAT32: Failed to read FAT sector %u\n", i);
            kfree(fat_cache);
            fat_cache = NULL;
            return -1;
        }
        
        /* Progress indicator every 32 sectors */
        if ((i & 31) == 0 && i > 0) {
            printf(".");
        }
    }
    printf("\n");
    
    printf("FAT32: Mounted successfully\n");
    return 0;
}

static void parse_filename(const char *name, char *out_name) {
    memset_safe(out_name, ' ', 11);
    
    int i = 0, j = 0;
    
    while (name[i] && name[i] != '.' && j < 8) {
        out_name[j++] = (name[i] >= 'a' && name[i] <= 'z') ? 
                        name[i] - 32 : name[i];
        i++;
    }
    
    if (name[i] == '.') {
        i++;
        j = 8;
        while (name[i] && j < 11) {
            out_name[j++] = (name[i] >= 'a' && name[i] <= 'z') ? 
                           name[i] - 32 : name[i];
            i++;
        }
    }
}

static int find_file_in_dir(uint32_t dir_cluster, const char *filename, fat32_dir_entry_t *out_entry) {
    char search_name[11];
    parse_filename(filename, search_name);
    
    uint32_t cluster = dir_cluster;
    uint8_t *cluster_buf = (uint8_t*)kmalloc(boot_sector.sectors_per_cluster * 512);
    if (!cluster_buf) return -1;
    
    while (cluster < FAT32_EOC) {
        uint32_t lba = cluster_to_lba(cluster);
        
        for (uint32_t s = 0; s < boot_sector.sectors_per_cluster; s++) {
            if (ata_read_sectors(lba + s, 1, cluster_buf + (s * 512)) != 0) {
                kfree(cluster_buf);
                return -1;
            }
        }
        
        fat32_dir_entry_t *entries = (fat32_dir_entry_t*)cluster_buf;
        uint32_t entries_per_cluster = (boot_sector.sectors_per_cluster * 512) / 32;
        
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            if (entries[i].name[0] == 0x00) {
                kfree(cluster_buf);
                return -1; /* End of directory */
            }
            
            if (entries[i].name[0] == 0xE5) continue; /* Deleted */
            if (entries[i].attr == 0x0F) continue; /* LFN */
            
            int match = 1;
            for (int j = 0; j < 11; j++) {
                if (entries[i].name[j] != search_name[j]) {
                    match = 0;
                    break;
                }
            }
            
            if (match) {
                memcpy_safe(out_entry, &entries[i], sizeof(fat32_dir_entry_t));
                kfree(cluster_buf);
                return 0;
            }
        }
        
        cluster = get_next_cluster(cluster);
    }
    
    kfree(cluster_buf);
    return -1;
}

fat32_file_t* fat32_open(const char *path) {
    if (!path || path[0] != '/') return NULL;
    
    /* Find free slot */
    fat32_file_t *file = NULL;
    for (int i = 0; i < FAT32_MAX_OPEN_FILES; i++) {
        if (!open_files[i].in_use) {
            file = &open_files[i];
            break;
        }
    }
    
    if (!file) return NULL;
    
    /* Parse path */
    char filename[32];
    const char *p = path + 1;
    int i = 0;
    while (*p && *p != '/' && i < 31) {
        filename[i++] = *p++;
    }
    filename[i] = '\0';
    
    fat32_dir_entry_t entry;
    if (find_file_in_dir(boot_sector.root_cluster, filename, &entry) != 0) {
        return NULL;
    }
    
    file->first_cluster = ((uint32_t)entry.first_cluster_high << 16) | 
                         entry.first_cluster_low;
    file->current_cluster = file->first_cluster;
    file->size = entry.file_size;
    file->position = 0;
    file->cluster_offset = 0;
    file->in_use = 1;
    
    size_t len = strlen_safe(path);
    if (len >= FAT32_MAX_PATH) len = FAT32_MAX_PATH - 1;
    memcpy_safe(file->path, path, len);
    file->path[len] = '\0';
    
    return file;
}

int fat32_read(fat32_file_t *file, void *buffer, size_t size) {
    if (!file || !file->in_use || !buffer) return -1;
    if (file->position >= file->size) return 0;
    
    size_t bytes_read = 0;
    size_t to_read = size;
    
    if (file->position + to_read > file->size)
        to_read = file->size - file->position;
    
    uint8_t *cluster_buf = (uint8_t*)kmalloc(boot_sector.sectors_per_cluster * 512);
    if (!cluster_buf) return -1;
    
    while (to_read > 0 && file->current_cluster < FAT32_EOC) {
        uint32_t lba = cluster_to_lba(file->current_cluster);
        
        for (uint32_t s = 0; s < boot_sector.sectors_per_cluster; s++) {
            if (ata_read_sectors(lba + s, 1, cluster_buf + (s * 512)) != 0) {
                kfree(cluster_buf);
                return -1;
            }
        }
        
        uint32_t cluster_size = boot_sector.sectors_per_cluster * 512;
        uint32_t available = cluster_size - file->cluster_offset;
        uint32_t chunk = (to_read < available) ? to_read : available;
        
        memcpy_safe((uint8_t*)buffer + bytes_read, 
                   cluster_buf + file->cluster_offset, chunk);
        
        bytes_read += chunk;
        to_read -= chunk;
        file->position += chunk;
        file->cluster_offset += chunk;
        
        if (file->cluster_offset >= cluster_size) {
            file->current_cluster = get_next_cluster(file->current_cluster);
            file->cluster_offset = 0;
        }
    }
    
    kfree(cluster_buf);
    return bytes_read;
}

int fat32_write(fat32_file_t *file, const void *buffer, size_t size) {
    if (!file || !file->in_use || !buffer) return -1;
    
    size_t bytes_written = 0;
    uint8_t *cluster_buf = (uint8_t*)kmalloc(boot_sector.sectors_per_cluster * 512);
    if (!cluster_buf) return -1;
    
    while (size > 0) {
        if (file->current_cluster >= FAT32_EOC) {
            uint32_t new_cluster = allocate_cluster();
            if (new_cluster == 0) break;
            
            set_next_cluster(file->current_cluster, new_cluster);
            file->current_cluster = new_cluster;
        }
        
        uint32_t lba = cluster_to_lba(file->current_cluster);
        uint32_t cluster_size = boot_sector.sectors_per_cluster * 512;
        
        /* Read-modify-write */
        for (uint32_t s = 0; s < boot_sector.sectors_per_cluster; s++) {
            if (ata_read_sectors(lba + s, 1, cluster_buf + (s * 512)) != 0) {
                kfree(cluster_buf);
                return -1;
            }
        }
        
        uint32_t available = cluster_size - file->cluster_offset;
        uint32_t chunk = (size < available) ? size : available;
        
        memcpy_safe(cluster_buf + file->cluster_offset,
                   (const uint8_t*)buffer + bytes_written, chunk);
        
        for (uint32_t s = 0; s < boot_sector.sectors_per_cluster; s++) {
            if (ata_write_sectors(lba + s, 1, cluster_buf + (s * 512)) != 0) {
                kfree(cluster_buf);
                return -1;
            }
        }
        
        bytes_written += chunk;
        size -= chunk;
        file->position += chunk;
        file->cluster_offset += chunk;
        
        if (file->cluster_offset >= cluster_size) {
            file->current_cluster = get_next_cluster(file->current_cluster);
            file->cluster_offset = 0;
        }
    }
    
    if (file->position > file->size)
        file->size = file->position;
    
    kfree(cluster_buf);
    return bytes_written;
}

int fat32_seek(fat32_file_t *file, uint32_t offset) {
    if (!file || !file->in_use) return -1;
    
    file->position = offset;
    file->current_cluster = file->first_cluster;
    file->cluster_offset = 0;
    
    uint32_t cluster_size = boot_sector.sectors_per_cluster * 512;
    
    while (offset >= cluster_size && file->current_cluster < FAT32_EOC) {
        file->current_cluster = get_next_cluster(file->current_cluster);
        offset -= cluster_size;
    }
    
    file->cluster_offset = offset;
    return 0;
}

void fat32_close(fat32_file_t *file) {
    if (!file) return;
    file->in_use = 0;
}

int fat32_list_dir(const char *path, fat32_dirent_t *entries, int max_entries) {
    if (!entries || max_entries <= 0) return -1;
    if (!fat_cache) {
        printf("FAT32: Not mounted\n");
        return -1;
    }
    
    uint32_t dir_cluster = boot_sector.root_cluster;
    
    if (dir_cluster < 2) {
        printf("FAT32: Invalid root cluster: %u\n", dir_cluster);
        return -1;
    }
    
    uint32_t cluster_size = boot_sector.sectors_per_cluster * 512;
    /* printf("FAT32: Listing cluster %u (size=%u bytes)\n", dir_cluster, cluster_size);*/
    
    uint8_t *cluster_buf = (uint8_t*)kmalloc(cluster_size);
    if (!cluster_buf) {
        printf("FAT32: Failed to allocate cluster buffer\n");
        return -1;
    }
    
    int count = 0;
    int iterations = 0;
    
    while (dir_cluster >= 2 && dir_cluster < FAT32_EOC && count < max_entries) {
        if (iterations++ > 100) {
            printf("FAT32: Too many iterations, breaking\n");
            break;
        }
        
        uint32_t lba = cluster_to_lba(dir_cluster);
        /* printf("FAT32: Reading cluster %u at LBA %u\n", dir_cluster, lba); */
        
        /* Read entire cluster */
        for (uint32_t s = 0; s < boot_sector.sectors_per_cluster; s++) {
            if (ata_read_sectors(lba + s, 1, cluster_buf + (s * 512)) != 0) {
                printf("FAT32: Failed to read sector %u\n", lba + s);
                kfree(cluster_buf);
                return -1;
            }
        }
        
        fat32_dir_entry_t *dir_entries = (fat32_dir_entry_t*)cluster_buf;
        uint32_t entries_per_cluster = cluster_size / 32;
        
        /* printf("FAT32: Processing %u directory entries\n", entries_per_cluster); */
        
        for (uint32_t i = 0; i < entries_per_cluster && count < max_entries; i++) {
            uint8_t first_byte = dir_entries[i].name[0];
            
            if (first_byte == 0x00) {
                kfree(cluster_buf);
                return count;
            }
            
            if (first_byte == 0xE5) continue; /* Deleted */
            if (dir_entries[i].attr == 0x0F) continue; /* LFN */
            if (dir_entries[i].attr & 0x08) continue; /* Volume label */
            
            /* Copy name (8.3 format) */
            int j = 0;
            for (int k = 0; k < 8 && dir_entries[i].name[k] != ' '; k++) {
                entries[count].name[j++] = dir_entries[i].name[k];
            }
            
            if (dir_entries[i].name[8] != ' ') {
                entries[count].name[j++] = '.';
                for (int k = 8; k < 11 && dir_entries[i].name[k] != ' '; k++) {
                    entries[count].name[j++] = dir_entries[i].name[k];
                }
            }
            entries[count].name[j] = '\0';
            
            entries[count].size = dir_entries[i].file_size;
            entries[count].first_cluster = ((uint32_t)dir_entries[i].first_cluster_high << 16) | dir_entries[i].first_cluster_low;
            entries[count].is_directory = (dir_entries[i].attr & 0x10) ? 1 : 0;
                      
            count++;
        }
        
        /* Get next cluster */
        uint32_t next = get_next_cluster(dir_cluster);
        /* printf("FAT32: Next cluster = 0x%08X\n", next); */
        
        if (next == dir_cluster) {
            printf("FAT32: Cluster loop detected!\n");
            break;
        }
        
        dir_cluster = next;
    }
    
    kfree(cluster_buf);
    /* printf("FAT32: Found %d entries\n", count); */
    return count;
}

int fat32_create_file(const char *path) {
    /* Simplified: would need to add directory entry */
    return -1;
}

int fat32_delete_file(const char *path) {
    /* Simplified: would need to mark directory entry as deleted */
    return -1;
}