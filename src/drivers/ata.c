#include "ata.h"
#include "../irq/io.h"
#include "../console.h"

#define ATA_PRIMARY_IO      0x1F0
#define ATA_PRIMARY_CONTROL 0x3F6

#define ATA_REG_DATA       0
#define ATA_REG_ERROR      1
#define ATA_REG_SECCOUNT   2
#define ATA_REG_LBA_LO     3
#define ATA_REG_LBA_MID    4
#define ATA_REG_LBA_HI     5
#define ATA_REG_DRIVE      6
#define ATA_REG_STATUS     7
#define ATA_REG_COMMAND    7

#define ATA_CMD_READ_PIO   0x20
#define ATA_CMD_WRITE_PIO  0x30
#define ATA_CMD_IDENTIFY   0xEC

#define ATA_SR_BSY   0x80
#define ATA_SR_DRDY  0x40
#define ATA_SR_DRQ   0x08
#define ATA_SR_ERR   0x01

static inline void ata_400ns_delay(void);

static inline void ata_wait_bsy(void) {
    while (inb(ATA_PRIMARY_IO + ATA_REG_STATUS) & ATA_SR_BSY);
}

static int ata_wait_bsy_timeout(uint32_t timeout_ms) {
    uint32_t ticks = timeout_ms * 10000; // ~100us per tick
    while (ticks-- > 0) {
        uint8_t status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY)) return 0;
        for (volatile int i = 0; i < 10; i++);
    }
    return -1;
}

static inline void ata_wait_drq(void) {
    while (!(inb(ATA_PRIMARY_IO + ATA_REG_STATUS) & ATA_SR_DRQ));
}

static int ata_wait_drq_timeout(uint32_t timeout_ms) {
    uint32_t ticks = timeout_ms * 10000;
    while (ticks-- > 0) {
        uint8_t status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if (status & ATA_SR_ERR) return -2;
        if (status & ATA_SR_DRQ) return 0;
        for (volatile int i = 0; i < 10; i++);
    }
    return -1;
}

static void ata_soft_reset(void) {
    outb(ATA_PRIMARY_CONTROL, 0x04); // SRST
    ata_400ns_delay();
    outb(ATA_PRIMARY_CONTROL, 0x00);
    ata_400ns_delay();
    for (volatile int i = 0; i < 100000; i++); // Wait ~10ms
}

static inline void ata_400ns_delay(void) {
    for (int i = 0; i < 4; i++)
        inb(ATA_PRIMARY_CONTROL);
}

void ata_init(void) {
    // Soft reset
    outb(ATA_PRIMARY_CONTROL, 0x04);
    for (volatile int i = 0; i < 100000; i++); // ~10ms
    outb(ATA_PRIMARY_CONTROL, 0x00);
    for (volatile int i = 0; i < 400000; i++); // ~40ms
    
    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, 0xA0);
    for (volatile int i = 0; i < 10000; i++);
}

int ata_identify(void) {
    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, 0xA0);
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_LO, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_MID, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_HI, 0);
    ata_400ns_delay();
    
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_400ns_delay();
    
    uint8_t status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    if (status == 0) {
        return -1;
    }
    
    if (ata_wait_bsy_timeout(30000) != 0) {
        return -1;
    }
    
    uint8_t lbamid = inb(ATA_PRIMARY_IO + ATA_REG_LBA_MID);
    uint8_t lbahi = inb(ATA_PRIMARY_IO + ATA_REG_LBA_HI);
    
    if (lbamid != 0 || lbahi != 0) {
        return -1;
    }
    
    if (ata_wait_drq_timeout(30000) != 0) {
        return -1;
    }
    
    uint16_t identify[256];
    for (int i = 0; i < 256; i++) {
        identify[i] = inw(ATA_PRIMARY_IO + ATA_REG_DATA);
    }
    
    return 0;
}


int ata_read_sectors(uint32_t lba, uint8_t sector_count, void *buffer) {
    if (!buffer || sector_count == 0) {
        return -1;
    }
    
    if (ata_wait_bsy_timeout(5000) != 0) {
        return -1;
    }
    
    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    ata_400ns_delay();
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT, sector_count);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_LO, (uint8_t)lba);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_HI, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_READ_PIO);
    
    uint16_t *buf = (uint16_t*)buffer;
    
    for (int s = 0; s < sector_count; s++) {
        if (ata_wait_bsy_timeout(5000) != 0) {
            return -1;
        }
        
        int drq_result = ata_wait_drq_timeout(5000);
        if (drq_result != 0) {
            return -1;
        }
        
        for (int i = 0; i < 256; i++) {
            buf[s * 256 + i] = inw(ATA_PRIMARY_IO + ATA_REG_DATA);
        }
        
        ata_400ns_delay();
    }
    
    return 0;
}

int ata_write_sectors(uint32_t lba, uint8_t sector_count, const void *buffer) {
    if (!buffer || sector_count == 0) return -1;
    
    if (ata_wait_bsy_timeout(5000) != 0) {
        return -1;
    }
    
    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    ata_400ns_delay();
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT, sector_count);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_LO, (uint8_t)lba);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_HI, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);
    
    const uint16_t *buf = (const uint16_t*)buffer;
    
    for (int s = 0; s < sector_count; s++) {
        if (ata_wait_bsy_timeout(5000) != 0) {
            return -1;
        }
        
        int drq_result = ata_wait_drq_timeout(5000);
        if (drq_result != 0) {
            return -1;
        }
        
        for (int i = 0; i < 256; i++) {
            outw(ATA_PRIMARY_IO + ATA_REG_DATA, buf[s * 256 + i]);
        }
        
        ata_400ns_delay();
    }
    
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, 0xE7);
    if (ata_wait_bsy_timeout(5000) != 0) {
        return -1;
    }
    
    return 0;
}

int ata_is_available(void) {
    uint8_t status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    return (status != 0xFF && status != 0x00);
}

int ata_is_present(void) {
    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, 0xA0);
    for (volatile int i = 0; i < 1000; i++);
    
    uint8_t status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    
    if (status == 0xFF || status == 0x00) {
        return 0;
    }
    
    uint8_t lbamid = inb(ATA_PRIMARY_IO + ATA_REG_LBA_MID);
    uint8_t lbahi = inb(ATA_PRIMARY_IO + ATA_REG_LBA_HI);
    
    if (lbamid == 0x14 && lbahi == 0xEB) {
        return 0; // ATAPI (CD-ROM)
    }
    
    return 1;
}