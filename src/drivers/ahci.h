#pragma once
#include <stdint.h>

void ahci_init(void);
int ahci_ready(void);
const char *ahci_status(void);
int ahci_read_sectors(uint32_t lba, uint8_t sector_count, void *buffer);
int ahci_write_sectors(uint32_t lba, uint8_t sector_count, const void *buffer);
