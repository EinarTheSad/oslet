#include "private.h"

fat32_volume_t volumes[FAT32_MAX_VOLUMES];
fat32_file_t open_files[FAT32_MAX_OPEN_FILES];
char current_dir[FAT32_MAX_PATH] = "C:/";
static volatile int fat32_lock = 0;
uint8_t fat_dirty[FAT32_MAX_VOLUMES];
uint32_t last_alloc[FAT32_MAX_VOLUMES];

void fat32_acquire(void) {
    while (__sync_lock_test_and_set(&fat32_lock, 1)) {
        task_yield();
    }
}

void fat32_release(void) {
    __sync_lock_release(&fat32_lock);
}

fat32_volume_t* get_volume(uint8_t drive) {
    for (int i = 0; i < FAT32_MAX_VOLUMES; i++) {
        if (volumes[i].mounted && volumes[i].drive_letter == drive)
            return &volumes[i];
    }
    return NULL;
}

int volume_index(fat32_volume_t *vol) {
    ptrdiff_t idx = vol - volumes;
    if (idx < 0 || idx >= FAT32_MAX_VOLUMES) return -1;
    return (int)idx;
}
