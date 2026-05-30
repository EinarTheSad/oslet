#include "private.h"

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

int find_fat32_partition(uint32_t *out_start_lba) {
    if (!out_start_lba) return -1;

    uint8_t *sector = kmalloc(512);
    if (!sector) return -1;

    uint32_t boot_part = 0;

    if (boot_device != 0xFFFFFFFF) {
        uint32_t boot_drive = (boot_device >> 24) & 0xFF;
        if (boot_drive >= 0x80)
            boot_part = (boot_device >> 16) & 0xFF;
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
    if (find_fat32_partition(&start_lba) != 0) {
        return -1;
    }
    return fat32_mount_drive(drive_letter, start_lba);
}
