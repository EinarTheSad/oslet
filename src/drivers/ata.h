#pragma once
#include <stdint.h>
#include <stddef.h>

#define ATA_SECTOR_SIZE 512

void ata_init(void);
int ata_read_sectors(uint32_t lba, uint8_t sector_count, void *buffer);
int ata_write_sectors(uint32_t lba, uint8_t sector_count, const void *buffer);
int ata_identify(void);