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

/* Runtime flag: whether the controller supports cache-flush (0xE7).
 * Some emulators (PCem) and older controllers report ERR for flush even
 * though data was written. We detect that and disable flush at runtime
 * to avoid constant ERR loops. */
static int ata_flush_supported = 1;
static inline void ata_wait_bsy(void) {
    while (inb(ATA_PRIMARY_IO + ATA_REG_STATUS) & ATA_SR_BSY);
}

static int ata_wait_bsy_timeout(uint32_t timeout_ms) {
    uint32_t ticks = timeout_ms * 10000; // ~100us per tick
    while (ticks-- > 0) {
        uint8_t status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        // Wait for BSY to clear and DRDY to be set for better compatibility with some controllers (e.g., PCem)
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRDY)) return 0;
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

static inline void ata_400ns_delay(void) {
    for (int i = 0; i < 4; i++)
        inb(ATA_PRIMARY_CONTROL);
}

void ata_init(void) {
    // Disable interrupts (bit 1 = nIEN)
    outb(ATA_PRIMARY_CONTROL, 0x02);
    ata_400ns_delay();

    // Always do soft reset for compatibility - some controllers need it
    outb(ATA_PRIMARY_CONTROL, 0x06); // nIEN + SRST
    for (volatile int i = 0; i < 100000; i++); // ~10ms
    outb(ATA_PRIMARY_CONTROL, 0x02); // Clear SRST, keep nIEN

    // Wait for reset to complete - longer wait for real hardware
    for (volatile int i = 0; i < 4000000; i++); // ~400ms

    // Select master drive
    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, 0xA0);
    ata_400ns_delay();

    // Wait for drive to be ready
    for (volatile int i = 0; i < 100000; i++);
}

int ata_identify(void) {
    // Select master drive
    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, 0xA0);
    ata_400ns_delay();

    // Give drive time to respond to selection
    for (volatile int i = 0; i < 10000; i++);

    // Wait for BSY to clear and DRDY to set before sending command
    if (ata_wait_bsy_timeout(5000) != 0) {
        // Timeout waiting for drive to be ready
        return -1;
    }

    // Set up registers
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_LO, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_MID, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_HI, 0);

    // Send IDENTIFY command
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_400ns_delay();

    // Check if drive exists
    uint8_t status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    if (status == 0 || status == 0xFF) {
        return -1; // No drive
    }

    // Wait for BSY to clear
    if (ata_wait_bsy_timeout(30000) != 0) {
        return -1;
    }

    // Check LBA mid/hi to detect non-ATA devices (ATAPI, SATA, etc.)
    uint8_t lbamid = inb(ATA_PRIMARY_IO + ATA_REG_LBA_MID);
    uint8_t lbahi = inb(ATA_PRIMARY_IO + ATA_REG_LBA_HI);

    if (lbamid != 0 || lbahi != 0) {
        return -1; // Not an ATA device
    }

    // Wait for DRQ or ERR
    if (ata_wait_drq_timeout(30000) != 0) {
        return -1;
    }

    // Read identification data
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

    // Basic sanity check - reject obviously invalid LBAs
    if (lba > 0x0FFFFFFF) {
        return -1;
    }

    if (ata_wait_bsy_timeout(5000) != 0) {
        return -1;
    }

    // Select drive and wait for it to be ready
    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    ata_400ns_delay();

    // Wait for drive selection to take effect
    for (volatile int i = 0; i < 1000; i++);

    // Wait for BSY to clear after drive selection
    if (ata_wait_bsy_timeout(5000) != 0) {
        return -1;
    }

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
        /* brief backoff and one retry */
        for (volatile int d = 0; d < 50000; d++);
        if (ata_wait_bsy_timeout(1000) != 0) {
            return -1;
        }
    }

    // Safer drive select sequence: select master then reselect with LBA bits (improves compatibility with some controllers/emulators)
    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, 0xA0);
    ata_400ns_delay();
    for (volatile int i = 0; i < 10000; i++);

    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    ata_400ns_delay();

    // Wait for drive selection to take effect
    for (volatile int i = 0; i < 5000; i++);

    // Wait for BSY to clear after drive selection; allow a backoff/retry
    if (ata_wait_bsy_timeout(10000) != 0) {
        for (volatile int d = 0; d < 100000; d++);
        if (ata_wait_bsy_timeout(2000) != 0) {
            return -1;
        }
    }

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

        /* Read status to detect errors and ensure controller accepted the sector */
        uint8_t status_after = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if (status_after & ATA_SR_ERR) {
            return -1;
        }

        /* Some controllers (notably in PCem) may require waiting for DRQ to clear before continuing */
        uint32_t inner_ticks = 50000;
        while (inner_ticks-- > 0) {
            uint8_t st = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
            if (!(st & ATA_SR_DRQ)) break;
            for (volatile int ii = 0; ii < 10; ii++);
        }

        /* As an added safety, ensure BSY cleared before next sector/command */
        if (ata_wait_bsy_timeout(5000) != 0) {
            return -1;
        }
    }

    /* Only attempt cache flush if we believe it's supported. If we detect
     * that the controller returns ERR for flush (common in PCem), mark it
     * unsupported for the rest of the boot to avoid noisy loops. */
    if (ata_flush_supported) {
        outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, 0xE7);
        if (ata_wait_bsy_timeout(5000) != 0) {
        /* small delay and retry flush once */
        for (volatile int d = 0; d < 50000; d++);
        outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, 0xE7);
        if (ata_wait_bsy_timeout(2000) != 0) {
            /* Mark flush unsupported to avoid future failures */
            ata_flush_supported = 0;
        }
    }

        /* Check for errors after cache flush. If we get ERR, disable flush
         * going forward (but don't fail the write) because some emulators
         * report ERR while data is actually written. */
        if (inb(ATA_PRIMARY_IO + ATA_REG_STATUS) & ATA_SR_ERR) {
            /* Disable flush going forward for compatibility */
            ata_flush_supported = 0;
        }
    }

    /* Don't treat flush errors as fatal; assume sector writes succeeded if
     * ata_write_sectors returned success earlier. */
    return 0;
}

int ata_is_available(void) {
    uint8_t status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    return (status != 0xFF && status != 0x00);
}

int ata_is_present(void) {
    // Select master drive
    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, 0xA0);
    ata_400ns_delay();

    // Wait longer for drive to respond
    for (volatile int i = 0; i < 10000; i++);

    // Check status register
    uint8_t status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);

    if (status == 0xFF || status == 0x00) {
        return 0; // No drive present
    }

    // Check signature registers
    uint8_t lbamid = inb(ATA_PRIMARY_IO + ATA_REG_LBA_MID);
    uint8_t lbahi = inb(ATA_PRIMARY_IO + ATA_REG_LBA_HI);

    if (lbamid == 0x14 && lbahi == 0xEB) {
        return 0; // ATAPI (CD-ROM)
    }
    if (lbamid == 0x3C && lbahi == 0xC3) {
        return 0; // SATA
    }

    return 1;
}