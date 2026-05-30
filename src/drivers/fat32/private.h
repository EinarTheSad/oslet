#pragma once

#include "../fat32.h"
#include "../ata.h"
#include "../../console.h"
#include "../../mem/heap.h"
#include "../../rtc.h"
#include "../../task/task.h"

#define FAT32_EOC 0x0FFFFFF8
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

extern fat32_volume_t volumes[FAT32_MAX_VOLUMES];
extern fat32_file_t open_files[FAT32_MAX_OPEN_FILES];
extern char current_dir[FAT32_MAX_PATH];
extern uint8_t fat_dirty[FAT32_MAX_VOLUMES];
extern uint32_t last_alloc[FAT32_MAX_VOLUMES];
extern uint32_t boot_device;

void fat32_acquire(void);
void fat32_release(void);
fat32_volume_t* get_volume(uint8_t drive);
int volume_index(fat32_volume_t *vol);

uint16_t rtc_to_fat_time(const rtc_time_t *rtc);
uint16_t rtc_to_fat_date(const rtc_time_t *rtc);
void get_fat_timestamp(uint16_t *time, uint16_t *date);
uint8_t lfn_checksum(const char *short_name);
void utf16_to_ascii(const uint16_t *src, char *dst, int max_chars);
void ascii_to_utf16(const char *src, uint16_t *dst, int max_chars);
int parse_path(const char *path, uint8_t *drive, char *rest, size_t rest_size);
void parse_filename(const char *name, char *out_name);

uint32_t get_next_cluster(fat32_volume_t *vol, uint32_t cluster);
void set_next_cluster(fat32_volume_t *vol, uint32_t cluster, uint32_t value);
uint32_t alloc_cluster(fat32_volume_t *vol);
void free_cluster_chain(fat32_volume_t *vol, uint32_t start);
int sync_fat(fat32_volume_t *vol);
int read_cluster(fat32_volume_t *vol, uint32_t cluster, void *buffer);
int write_cluster(fat32_volume_t *vol, uint32_t cluster, const void *buffer);

int find_in_dir(fat32_volume_t *vol, uint32_t dir_cluster, const char *name,
                fat32_direntry_t *out, uint32_t *out_cluster, uint32_t *out_offset);
int navigate_path(fat32_volume_t *vol, const char *path, uint32_t *out_dir_cluster,
                  char *out_filename);
int add_dir_entry(fat32_volume_t *vol, uint32_t dir_cluster, const char *name,
                  uint32_t first_cluster, uint32_t size, uint8_t attr);
int remove_dir_entry(fat32_volume_t *vol, uint32_t dir_cluster, const char *name);
