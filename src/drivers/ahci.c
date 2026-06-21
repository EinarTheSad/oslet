#include "ahci.h"
#include "../console.h"
#include "../irq/io.h"
#include "../mem/paging.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define AHCI_GHC_AE 0x80000000u

#define AHCI_PORT_CLB   0x00
#define AHCI_PORT_CLBU  0x04
#define AHCI_PORT_FB    0x08
#define AHCI_PORT_FBU   0x0C
#define AHCI_PORT_IS    0x10
#define AHCI_PORT_IE    0x14
#define AHCI_PORT_CMD   0x18
#define AHCI_PORT_TFD   0x20
#define AHCI_PORT_SIG   0x24
#define AHCI_PORT_SSTS  0x28
#define AHCI_PORT_SCTL  0x2C
#define AHCI_PORT_SERR  0x30
#define AHCI_PORT_SACT  0x34
#define AHCI_PORT_CI    0x38

#define AHCI_CMD_ST  0x0001u
#define AHCI_CMD_FRE 0x0010u
#define AHCI_CMD_FR  0x4000u
#define AHCI_CMD_CR  0x8000u

#define AHCI_TFD_ERR 0x01u
#define AHCI_TFD_DRQ 0x08u
#define AHCI_TFD_BSY 0x80u

#define AHCI_FIS_REG_H2D 0x27
#define AHCI_ATA_READ_DMA_EXT  0x25
#define AHCI_ATA_WRITE_DMA_EXT 0x35
#define AHCI_ATA_FLUSH_EXT     0xEA

#define AHCI_SIG_ATA   0x00000101u
#define AHCI_SIG_ATAPI 0xEB140101u

typedef struct {
    uint8_t cfl;
    uint8_t flags;
    uint16_t prdtl;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t reserved[4];
} __attribute__((packed)) ahci_cmd_header_t;

typedef struct {
    uint32_t dba;
    uint32_t dbau;
    uint32_t reserved;
    uint32_t dbc_i;
} __attribute__((packed)) ahci_prdt_t;

typedef struct {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t reserved[48];
    ahci_prdt_t prdt[1];
} __attribute__((packed)) ahci_cmd_table_t;

static volatile uint32_t *ahci_base;
static int ahci_port = -1;
static int ahci_is_ready = 0;
static int ahci_flush_supported = 1;
static const char *ahci_status_msg = "not initialised";

static ahci_cmd_header_t ahci_cmd_list[32] __attribute__((aligned(1024)));
static uint8_t ahci_fis_area[256] __attribute__((aligned(256)));
static ahci_cmd_table_t ahci_cmd_table __attribute__((aligned(128)));
static uint8_t ahci_sector_buf[512] __attribute__((aligned(4096)));

static void ahci_memset(void *dst, uint8_t value, uint32_t size) {
    uint8_t *p = (uint8_t *)dst;
    while (size--)
        *p++ = value;
}

static void ahci_memcpy(void *dst, const void *src, uint32_t size) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (size--)
        *d++ = *s++;
}

static uint32_t pci_addr(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg) {
    return 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
           ((uint32_t)fn << 8) | (reg & 0xFC);
}

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg) {
    outl(PCI_CONFIG_ADDR, pci_addr(bus, dev, fn, reg));
    return inl(PCI_CONFIG_DATA);
}

static void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg, uint32_t value) {
    outl(PCI_CONFIG_ADDR, pci_addr(bus, dev, fn, reg));
    outl(PCI_CONFIG_DATA, value);
}

static int map_mmio(uint32_t base, uint32_t bytes) {
    uint32_t start = base & 0xFFFFF000u;
    uint32_t end = (base + bytes + 0xFFFu) & 0xFFFFF000u;

    for (uint32_t addr = start; addr < end; addr += PAGE_SIZE) {
        if (!paging_is_mapped(addr) &&
            paging_map_page(addr, addr, P_PRESENT | P_RW) != 0) {
            return -1;
        }
    }
    return 0;
}

static int find_controller(uint32_t *out_base, uint8_t *out_bus, uint8_t *out_dev, uint8_t *out_fn) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            for (uint8_t fn = 0; fn < 8; fn++) {
                uint32_t id = pci_read32((uint8_t)bus, dev, fn, 0x00);
                if (id == 0xFFFFFFFFu)
                    continue;

                uint32_t class_reg = pci_read32((uint8_t)bus, dev, fn, 0x08);
                uint8_t class_code = (class_reg >> 24) & 0xFF;
                uint8_t subclass = (class_reg >> 16) & 0xFF;
                uint8_t prog_if = (class_reg >> 8) & 0xFF;

                if (class_code != 0x01 || subclass != 0x06 || prog_if != 0x01)
                    continue;

                uint32_t bar5 = pci_read32((uint8_t)bus, dev, fn, 0x24);
                if (bar5 & 1) {
                    ahci_status_msg = "AHCI BAR5 is I/O";
                    return -2;
                }

                uint64_t base_addr = bar5 & ~0x0Fu;
                if ((bar5 & 0x6) == 0x4) {
                    uint32_t bar6 = pci_read32((uint8_t)bus, dev, fn, 0x28);
                    if (bar6 != 0) {
                        ahci_status_msg = "AHCI BAR above 4G";
                        return -2;
                    }
                    base_addr |= ((uint64_t)bar6 << 32);
                }

                *out_base = (uint32_t)base_addr;
                *out_bus = (uint8_t)bus;
                *out_dev = dev;
                *out_fn = fn;
                return *out_base ? 0 : -2;
            }
        }
    }

    ahci_status_msg = "no controller";
    return -1;
}

static uint32_t hba_read(uint32_t off) {
    return ahci_base[off / 4];
}

static void hba_write(uint32_t off, uint32_t value) {
    ahci_base[off / 4] = value;
}

static uint32_t port_off(int port, uint32_t reg) {
    return 0x100u + ((uint32_t)port * 0x80u) + reg;
}

static uint32_t port_read(int port, uint32_t reg) {
    return hba_read(port_off(port, reg));
}

static void port_write(int port, uint32_t reg, uint32_t value) {
    hba_write(port_off(port, reg), value);
}

static int wait_bits_clear(int port, uint32_t reg, uint32_t bits, uint32_t ms) {
    uint32_t loops = ms * 10000;
    while (loops-- > 0) {
        if ((port_read(port, reg) & bits) == 0)
            return 0;
        for (volatile int i = 0; i < 10; i++);
    }
    return -1;
}

static int stop_port(int port) {
    uint32_t cmd = port_read(port, AHCI_PORT_CMD);
    cmd &= ~AHCI_CMD_ST;
    port_write(port, AHCI_PORT_CMD, cmd);
    if (wait_bits_clear(port, AHCI_PORT_CMD, AHCI_CMD_CR, 500) != 0)
        return -1;

    cmd &= ~AHCI_CMD_FRE;
    port_write(port, AHCI_PORT_CMD, cmd);
    if (wait_bits_clear(port, AHCI_PORT_CMD, AHCI_CMD_FR, 500) != 0)
        return -1;

    return 0;
}

static void delay_loop(uint32_t loops) {
    while (loops--) {
        for (volatile int i = 0; i < 1000; i++);
    }
}

static int reset_port(int port) {
    if (stop_port(port) != 0)
        return -1;

    port_write(port, AHCI_PORT_SERR, 0xFFFFFFFFu);
    port_write(port, AHCI_PORT_SCTL, 1);
    delay_loop(2);
    port_write(port, AHCI_PORT_SCTL, 0);

    uint32_t loops = 1000 * 10000;
    while (loops-- > 0) {
        uint32_t ssts = port_read(port, AHCI_PORT_SSTS);
        uint32_t det = ssts & 0x0F;
        uint32_t ipm = (ssts >> 8) & 0x0F;

        if (det == 3 && ipm != 0)
            return 0;
        for (volatile int i = 0; i < 10; i++);
    }

    return -1;
}

static int wait_port_ready(int port, uint32_t ms) {
    uint32_t loops = ms * 10000;

    while (loops-- > 0) {
        uint32_t tfd = port_read(port, AHCI_PORT_TFD);
        if ((tfd & (AHCI_TFD_BSY | AHCI_TFD_DRQ)) == 0)
            return 0;
        for (volatile int i = 0; i < 10; i++);
    }

    return -1;
}

static int start_port(int port) {
    if (stop_port(port) != 0)
        return -1;

    ahci_memset(ahci_cmd_list, 0, sizeof(ahci_cmd_list));
    ahci_memset(ahci_fis_area, 0, sizeof(ahci_fis_area));
    ahci_memset(&ahci_cmd_table, 0, sizeof(ahci_cmd_table));

    port_write(port, AHCI_PORT_CLB, (uint32_t)(uintptr_t)ahci_cmd_list);
    port_write(port, AHCI_PORT_CLBU, 0);
    port_write(port, AHCI_PORT_FB, (uint32_t)(uintptr_t)ahci_fis_area);
    port_write(port, AHCI_PORT_FBU, 0);
    port_write(port, AHCI_PORT_SERR, 0xFFFFFFFFu);
    port_write(port, AHCI_PORT_IS, 0xFFFFFFFFu);
    port_write(port, AHCI_PORT_IE, 0);

    uint32_t cmd = port_read(port, AHCI_PORT_CMD);
    cmd |= AHCI_CMD_FRE;
    port_write(port, AHCI_PORT_CMD, cmd);
    cmd |= AHCI_CMD_ST;
    port_write(port, AHCI_PORT_CMD, cmd);
    return 0;
}

static int port_has_drive(int port) {
    uint32_t ssts = port_read(port, AHCI_PORT_SSTS);
    uint32_t det = ssts & 0x0F;
    uint32_t ipm = (ssts >> 8) & 0x0F;
    uint32_t sig = port_read(port, AHCI_PORT_SIG);

    if (det != 3 || ipm == 0 || sig == AHCI_SIG_ATAPI)
        return 0;
    return 1;
}

static int choose_port(void) {
    uint32_t pi = hba_read(0x0C);
    int fallback = -1;

    for (int port = 0; port < 32; port++) {
        if (!(pi & (1u << port)))
            continue;
        if (!port_has_drive(port))
            continue;
        if (port_read(port, AHCI_PORT_SIG) == AHCI_SIG_ATA)
            return port;
        if (fallback < 0)
            fallback = port;
    }

    return fallback;
}

static int wait_command_done(int port, uint32_t slot, uint32_t ms) {
    uint32_t loops = ms * 10000;

    while (loops-- > 0) {
        uint32_t tfd = port_read(port, AHCI_PORT_TFD);
        if (tfd & AHCI_TFD_ERR)
            return -1;
        if ((port_read(port, AHCI_PORT_CI) & (1u << slot)) == 0)
            return 0;
        for (volatile int i = 0; i < 10; i++);
    }

    return -1;
}

static int run_command(uint8_t command, uint32_t lba, void *buffer, int write, int has_data) {
    if (!ahci_is_ready || ahci_port < 0)
        return -1;

    uint32_t tfd = port_read(ahci_port, AHCI_PORT_TFD);
    if (tfd & (AHCI_TFD_BSY | AHCI_TFD_DRQ))
        return -1;

    ahci_memset(&ahci_cmd_table, 0, sizeof(ahci_cmd_table));
    ahci_memset(ahci_cmd_list, 0, sizeof(ahci_cmd_list));

    uint8_t *fis = ahci_cmd_table.cfis;
    fis[0] = AHCI_FIS_REG_H2D;
    fis[1] = 1u << 7;
    fis[2] = command;
    fis[3] = 0;
    fis[4] = (uint8_t)lba;
    fis[5] = (uint8_t)(lba >> 8);
    fis[6] = (uint8_t)(lba >> 16);
    fis[7] = 1u << 6;
    fis[8] = (uint8_t)(lba >> 24);
    fis[9] = 0;
    fis[10] = 0;
    fis[11] = 0;
    fis[12] = has_data ? 1 : 0;
    fis[13] = 0;
    fis[14] = 0;
    fis[15] = 0;

    ahci_cmd_header_t *hdr = &ahci_cmd_list[0];
    hdr->cfl = 5;
    hdr->flags = write ? (1u << 6) : 0;
    hdr->ctba = (uint32_t)(uintptr_t)&ahci_cmd_table;
    hdr->ctbau = 0;

    if (has_data) {
        hdr->prdtl = 1;
        ahci_cmd_table.prdt[0].dba = (uint32_t)(uintptr_t)ahci_sector_buf;
        ahci_cmd_table.prdt[0].dbau = 0;
        ahci_cmd_table.prdt[0].dbc_i = 511;
        if (write)
            ahci_memcpy(ahci_sector_buf, buffer, 512);
    }

    port_write(ahci_port, AHCI_PORT_SERR, 0xFFFFFFFFu);
    port_write(ahci_port, AHCI_PORT_IS, 0xFFFFFFFFu);
    port_write(ahci_port, AHCI_PORT_CI, 1);

    if (wait_command_done(ahci_port, 0, 5000) != 0) {
        port_write(ahci_port, AHCI_PORT_CI, 0);
        return -1;
    }

    if (has_data && !write)
        ahci_memcpy(buffer, ahci_sector_buf, 512);

    return 0;
}

void ahci_init(void) {
    uint32_t base = 0;
    uint8_t bus = 0, dev = 0, fn = 0;

    ahci_is_ready = 0;
    ahci_port = -1;
    ahci_status_msg = "no controller";

    if (find_controller(&base, &bus, &dev, &fn) != 0)
        return;

    if (map_mmio(base, 0x1100) != 0) {
        ahci_status_msg = "MMIO map failed";
        return;
    }

    uint32_t cmd = pci_read32(bus, dev, fn, 0x04);
    pci_write32(bus, dev, fn, 0x04, cmd | 0x00000006u);

    ahci_base = (volatile uint32_t *)(uintptr_t)base;
    hba_write(0x04, hba_read(0x04) | AHCI_GHC_AE);

    int port = choose_port();
    if (port < 0) {
        ahci_status_msg = "no SATA disk";
        return;
    }

    if (reset_port(port) != 0) {
        ahci_status_msg = "port reset failed";
        return;
    }

    if (!port_has_drive(port)) {
        ahci_status_msg = "SATA disk vanished after reset";
        return;
    }

    ahci_port = port;
    if (start_port(port) != 0) {
        ahci_port = -1;
        ahci_status_msg = "port start failed";
        return;
    }
    if (wait_port_ready(port, 1000) != 0) {
        ahci_port = -1;
        ahci_status_msg = "SATA disk not ready";
        return;
    }

    ahci_flush_supported = 1;
    ahci_is_ready = 1;
    ahci_status_msg = "ready";
}

int ahci_ready(void) {
    return ahci_is_ready;
}

const char *ahci_status(void) {
    return ahci_status_msg;
}

int ahci_read_sectors(uint32_t lba, uint8_t sector_count, void *buffer) {
    if (!buffer || sector_count == 0)
        return -1;

    uint8_t *dst = (uint8_t *)buffer;
    for (uint8_t i = 0; i < sector_count; i++) {
        if (run_command(AHCI_ATA_READ_DMA_EXT, lba + i, dst + (i * 512), 0, 1) != 0)
            return -1;
    }
    return 0;
}

int ahci_write_sectors(uint32_t lba, uint8_t sector_count, const void *buffer) {
    if (!buffer || sector_count == 0)
        return -1;

    const uint8_t *src = (const uint8_t *)buffer;
    for (uint8_t i = 0; i < sector_count; i++) {
        if (run_command(AHCI_ATA_WRITE_DMA_EXT, lba + i, (void *)(src + (i * 512)), 1, 1) != 0)
            return -1;
    }

    if (ahci_flush_supported &&
        run_command(AHCI_ATA_FLUSH_EXT, 0, NULL, 0, 0) != 0) {
        ahci_flush_supported = 0;
    }

    return 0;
}
