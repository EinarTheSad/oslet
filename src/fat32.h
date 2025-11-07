#pragma once
#include <stdint.h>
#include <stddef.h>

#define FAT32_MAX_PATH 256
#define FAT32_MAX_OPEN_FILES 16

typedef struct {
    char name[12];
    uint32_t size;
    uint32_t first_cluster;
    uint8_t is_directory;
} fat32_dirent_t;

typedef struct {
    uint32_t position;
    uint32_t size;
    uint32_t first_cluster;
    uint32_t current_cluster;
    uint32_t cluster_offset;
    uint8_t in_use;
    char path[FAT32_MAX_PATH];
} fat32_file_t;

void fat32_init(void);
int fat32_mount(void);

fat32_file_t* fat32_open(const char *path);
int fat32_read(fat32_file_t *file, void *buffer, size_t size);
int fat32_write(fat32_file_t *file, const void *buffer, size_t size);
int fat32_seek(fat32_file_t *file, uint32_t offset);
void fat32_close(fat32_file_t *file);

int fat32_list_dir(const char *path, fat32_dirent_t *entries, int max_entries);
int fat32_create_file(const char *path);
int fat32_delete_file(const char *path);