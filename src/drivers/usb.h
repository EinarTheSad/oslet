#pragma once
#include <stdint.h>

void usb_init(void);
int usb_ready(void);
const char *usb_status(void);
int usb_read_sectors(uint32_t lba, uint8_t sector_count, void *buffer);
int usb_write_sectors(uint32_t lba, uint8_t sector_count, const void *buffer);
