#include "private.h"

static uint32_t cluster_to_lba(fat32_volume_t *vol, uint32_t cluster) {
    return vol->first_data_sector + (cluster - 2) * vol->sectors_per_cluster;
}

uint32_t get_next_cluster(fat32_volume_t *vol, uint32_t cluster) {
    if (!vol->fat_cache) return FAT32_EOC;
    if (cluster < 2) return FAT32_EOC;
    uint32_t entries = vol->fat_cache_size / 4;
    if (cluster >= entries) return FAT32_EOC;
    uint32_t val;
    memcpy_s(&val, vol->fat_cache + cluster * 4, 4);
    return (val & 0x0FFFFFFF);
}

void set_next_cluster(fat32_volume_t *vol, uint32_t cluster, uint32_t value) {
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

uint32_t alloc_cluster(fat32_volume_t *vol) {
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
void free_cluster_chain(fat32_volume_t *vol, uint32_t start) {
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
            if (write_cluster(vol, cluster, zero_buf) != 0) {
                printf("Failed to clear cluster %u\n", cluster);
            }
        }
        
        set_next_cluster(vol, cluster, 0);
        cluster = next;
        safety++;
    }
    
    if (zero_buf) {
        kfree(zero_buf);
    }
}

int sync_fat(fat32_volume_t *vol) {
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
int read_cluster(fat32_volume_t *vol, uint32_t cluster, void *buffer) {
    if (cluster < 2 || cluster >= FAT32_EOC) return -1;
    uint32_t lba = cluster_to_lba(vol, cluster);
    for (uint32_t i = 0; i < vol->sectors_per_cluster; i++) {
        if (ata_read_sectors(lba + i, 1, (uint8_t*)buffer + i * vol->bytes_per_sector) != 0) {
            return -1;
        }
    }
    return 0;
}
int write_cluster(fat32_volume_t *vol, uint32_t cluster, const void *buffer) {
    if (cluster < 2 || cluster >= FAT32_EOC) return -1;
    uint32_t lba = cluster_to_lba(vol, cluster);

    for (uint32_t i = 0; i < vol->sectors_per_cluster; i++) {
        int attempts = 0;
        int res = -1;
        const uint8_t *buf_ptr = (const uint8_t*)buffer + i * vol->bytes_per_sector;
        while (attempts < 3) {
            res = ata_write_sectors(lba + i, 1, buf_ptr);
            if (res == 0) break;

            /* small delay before retry to allow controller to recover */
            for (volatile int d = 0; d < 50000; d++);
            attempts++;
        }

        if (res != 0) {
            return -1;
        }
    }
    return 0;
}
