#pragma once
#include <stdint.h>
#include <stddef.h>

#define FAT32_MAX_PATH 256
#define FAT32_MAX_OPEN_FILES 32
#define FAT32_MAX_VOLUMES 4

#define FAT_ATTR_READ_ONLY 0x01
#define FAT_ATTR_HIDDEN    0x02
#define FAT_ATTR_SYSTEM    0x04
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_ARCHIVE   0x20
#define FAT_ATTR_LFN       0x0F

typedef struct {
    char name[13];
    uint32_t size;
    uint32_t first_cluster;
    uint8_t is_directory;
    uint8_t attr;
} fat32_dirent_t;

typedef struct {
    uint32_t position;
    uint32_t size;
    uint32_t first_cluster;
    uint32_t current_cluster;
    uint32_t cluster_offset;
    uint8_t in_use;
    uint8_t drive;
    uint8_t mode;
    char path[FAT32_MAX_PATH];
} fat32_file_t;

typedef struct {
    uint8_t drive_letter;
    uint8_t mounted;
    uint32_t first_fat_sector;
    uint32_t first_data_sector;
    uint32_t root_cluster;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_fat;
    uint8_t num_fats;
    uint8_t *fat_cache;
    uint32_t fat_cache_size;
} fat32_volume_t;

void fat32_init(void);
int fat32_mount_drive(uint8_t drive_letter, uint32_t start_lba);
int fat32_unmount_drive(uint8_t drive_letter);

fat32_file_t* fat32_open(const char *path, const char *mode);
int fat32_read(fat32_file_t *file, void *buffer, size_t size);
int fat32_write(fat32_file_t *file, const void *buffer, size_t size);
int fat32_seek(fat32_file_t *file, uint32_t offset);
uint32_t fat32_tell(fat32_file_t *file);
void fat32_close(fat32_file_t *file);

int fat32_list_dir(const char *path, fat32_dirent_t *entries, int max_entries);
int fat32_mkdir(const char *path);
int fat32_rmdir(const char *path);
int fat32_unlink(const char *path);
int fat32_stat(const char *path, fat32_dirent_t *entry);

char* fat32_getcwd(char *buf, size_t size);
int fat32_chdir(const char *path);