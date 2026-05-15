#include "usb.h"
#include "../console.h"
#include "../irq/io.h"
#include "../mem/paging.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define USB_HOST_NONE 0
#define USB_HOST_UHCI 1
#define USB_HOST_EHCI 2

#define USB_PID_OUT   0xE1
#define USB_PID_IN    0x69
#define USB_PID_SETUP 0x2D

#define USB_REQ_GET_DESCRIPTOR  6
#define USB_REQ_SET_ADDRESS     5
#define USB_REQ_SET_CONFIG      9
#define USB_DT_DEVICE           1
#define USB_DT_CONFIG           2
#define USB_DT_INTERFACE        4
#define USB_DT_ENDPOINT         5

#define USB_CLASS_MASS_STORAGE  0x08
#define USB_SUBCLASS_SCSI       0x06
#define USB_PROTOCOL_BULK_ONLY  0x50

#define CBW_SIGNATURE 0x43425355
#define CSW_SIGNATURE 0x53425355

#define UHCI_CMD       0x00
#define UHCI_STS       0x02
#define UHCI_INTR      0x04
#define UHCI_FRNUM     0x06
#define UHCI_FLBASE    0x08
#define UHCI_SOFMOD    0x0C
#define UHCI_PORT1     0x10
#define UHCI_PORT2     0x12

#define UHCI_CMD_RS       0x0001
#define UHCI_CMD_HCRESET  0x0002
#define UHCI_CMD_CF       0x0040
#define UHCI_CMD_MAXP     0x0080

#define UHCI_PORT_CONNECT  0x0001
#define UHCI_PORT_CSC      0x0002
#define UHCI_PORT_ENABLE   0x0004
#define UHCI_PORT_PEC      0x0008
#define UHCI_PORT_LOWSPEED 0x0100
#define UHCI_PORT_RESET    0x0200

#define UHCI_PTR_TERM 0x00000001
#define UHCI_PTR_QH   0x00000002

#define UHCI_TD_ACTIVE     0x00800000
#define UHCI_TD_CERR3      0x18000000
#define UHCI_TD_ERROR_MASK 0x00760000

#define EHCI_CMD_RUN   0x00000001
#define EHCI_CMD_RESET 0x00000002
#define EHCI_CMD_ASE   0x00000020

#define EHCI_STS_HALTED 0x00001000
#define EHCI_STS_ASS    0x00008000

#define EHCI_PORT_CONNECT 0x00000001
#define EHCI_PORT_ENABLE  0x00000004
#define EHCI_PORT_RESET   0x00000100
#define EHCI_PORT_POWER   0x00001000
#define EHCI_PORT_OWNER   0x00002000

#define EHCI_REG_USBCMD       0x00
#define EHCI_REG_USBSTS       0x04
#define EHCI_REG_USBINTR      0x08
#define EHCI_REG_CTRLDSSEG    0x10
#define EHCI_REG_ASYNCLIST    0x18
#define EHCI_REG_CONFIGFLAG   0x40
#define EHCI_REG_PORTSC       0x44

#define EHCI_PTR_TERM 0x00000001
#define EHCI_PTR_QH   0x00000002

#define EHCI_QTD_ACTIVE     0x00000080
#define EHCI_QTD_ERROR_MASK 0x0000007C
#define EHCI_PID_CODE_OUT   0
#define EHCI_PID_CODE_IN    1
#define EHCI_PID_CODE_SETUP 2

typedef struct {
    uint32_t link;
    uint32_t status;
    uint32_t token;
    uint32_t buffer;
} __attribute__((packed, aligned(16))) uhci_td_t;

typedef struct {
    uint32_t head;
    uint32_t element;
} __attribute__((packed, aligned(16))) uhci_qh_t;

typedef struct {
    uint32_t next;
    uint32_t alt_next;
    uint32_t token;
    uint32_t buffer[5];
    uint32_t ext_buffer[5];
    uint32_t pad[3];
} __attribute__((packed, aligned(32))) ehci_qtd_t;

typedef struct {
    uint32_t horiz_link;
    uint32_t ep_char;
    uint32_t ep_caps;
    uint32_t current_qtd;
    uint32_t next_qtd;
    uint32_t alt_next_qtd;
    uint32_t token;
    uint32_t buffer[5];
    uint32_t ext_buffer[5];
    uint32_t pad[4];
} __attribute__((packed, aligned(32))) ehci_qh_t;

typedef struct {
    uint32_t signature;
    uint32_t tag;
    uint32_t data_len;
    uint8_t flags;
    uint8_t lun;
    uint8_t cb_len;
    uint8_t cb[16];
} __attribute__((packed)) msd_cbw_t;

typedef struct {
    uint32_t signature;
    uint32_t tag;
    uint32_t residue;
    uint8_t status;
} __attribute__((packed)) msd_csw_t;

static uint8_t usb_host = USB_HOST_NONE;
static uint8_t usb_available = 0;
static uint8_t dev_addr = 1;
static uint8_t ep0_max = 8;
static uint8_t bulk_in_ep = 0;
static uint8_t bulk_out_ep = 0;
static uint16_t bulk_in_max = 64;
static uint16_t bulk_out_max = 64;
static uint8_t bulk_in_toggle = 0;
static uint8_t bulk_out_toggle = 0;
static uint32_t cbw_tag = 1;

static uint16_t uhci_io = 0;
static uint32_t uhci_frame_list[1024] __attribute__((aligned(4096)));
static uhci_qh_t uhci_qh __attribute__((aligned(16)));
static uhci_td_t uhci_tds[64] __attribute__((aligned(16)));

static volatile uint8_t *ehci_caps = 0;
static volatile uint8_t *ehci_regs = 0;
static uint8_t ehci_port_count = 0;
static ehci_qh_t ehci_qh __attribute__((aligned(32)));
static ehci_qtd_t ehci_qtds[64] __attribute__((aligned(32)));

static uint8_t setup_buf[8] __attribute__((aligned(4096)));
static uint8_t ctrl_buf[512] __attribute__((aligned(4096)));
static uint8_t sector_buf[512] __attribute__((aligned(4096)));
static msd_cbw_t cbw __attribute__((aligned(4096)));
static msd_csw_t csw __attribute__((aligned(4096)));

static void delay_loop(uint32_t loops) {
    while (loops--) {
        for (volatile int i = 0; i < 1000; i++);
    }
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

static int find_usb_controller(uint8_t prog_if, int wanted, uint32_t *base,
                               uint8_t *out_bus, uint8_t *out_dev, uint8_t *out_fn) {
    int seen = 0;

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            for (uint8_t fn = 0; fn < 8; fn++) {
                uint32_t id = pci_read32((uint8_t)bus, dev, fn, 0x00);
                if (id == 0xFFFFFFFF)
                    continue;

                uint32_t class_reg = pci_read32((uint8_t)bus, dev, fn, 0x08);
                uint8_t class_code = (class_reg >> 24) & 0xFF;
                uint8_t subclass = (class_reg >> 16) & 0xFF;
                uint8_t iface = (class_reg >> 8) & 0xFF;

                if (class_code != 0x0C || subclass != 0x03 || iface != prog_if)
                    continue;

                if (seen++ != wanted)
                    continue;

                uint8_t bar = (prog_if == 0x00) ? 0x20 : 0x10;
                uint32_t raw = pci_read32((uint8_t)bus, dev, fn, bar);
                if (prog_if == 0x00) {
                    if (!(raw & 1))
                        continue;
                    *base = raw & ~0x1Fu;
                } else {
                    if (raw & 1)
                        continue;
                    *base = raw & ~0x0Fu;
                }

                *out_bus = (uint8_t)bus;
                *out_dev = dev;
                *out_fn = fn;
                return 0;
            }
        }
    }
    return -1;
}

static uint32_t ehci_read32(uint32_t off) {
    return *(volatile uint32_t *)(ehci_regs + off);
}

static void ehci_write32(uint32_t off, uint32_t value) {
    *(volatile uint32_t *)(ehci_regs + off) = value;
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

static uint32_t uhci_td_token(uint8_t pid, uint8_t addr, uint8_t ep,
                              uint8_t toggle, uint16_t len) {
    uint32_t maxlen = len ? (uint32_t)(len - 1) : 0x7FFu;
    return pid | ((uint32_t)addr << 8) | ((uint32_t)ep << 15) |
           ((uint32_t)(toggle & 1) << 19) | (maxlen << 21);
}

static void uhci_td_fill(int idx, uint8_t pid, uint8_t addr, uint8_t ep,
                         uint8_t toggle, void *buf, uint16_t len, int last) {
    uhci_tds[idx].link = last ? UHCI_PTR_TERM : (uint32_t)&uhci_tds[idx + 1];
    uhci_tds[idx].status = UHCI_TD_ACTIVE | UHCI_TD_CERR3;
    uhci_tds[idx].token = uhci_td_token(pid, addr, ep, toggle, len);
    uhci_tds[idx].buffer = len ? (uint32_t)buf : 0;
}

static int uhci_run_tds(int count) {
    if (count <= 0)
        return -1;

    uhci_qh.head = UHCI_PTR_TERM;
    uhci_qh.element = (uint32_t)&uhci_tds[0];

    for (int i = 0; i < 1024; i++)
        uhci_frame_list[i] = ((uint32_t)&uhci_qh) | UHCI_PTR_QH;

    outw(uhci_io + UHCI_STS, 0xFFFF);

    for (uint32_t spins = 0; spins < 2000000; spins++) {
        int active = 0;
        for (int i = 0; i < count; i++) {
            if (uhci_tds[i].status & UHCI_TD_ACTIVE) {
                active = 1;
                break;
            }
        }
        if (!active)
            break;
        for (volatile int d = 0; d < 20; d++);
    }

    for (int i = 0; i < 1024; i++)
        uhci_frame_list[i] = UHCI_PTR_TERM;
    uhci_qh.element = UHCI_PTR_TERM;

    for (int i = 0; i < count; i++) {
        if (uhci_tds[i].status & UHCI_TD_ACTIVE)
            return -1;
        if (uhci_tds[i].status & UHCI_TD_ERROR_MASK)
            return -1;
    }
    return 0;
}

static int uhci_control_msg(uint8_t addr, uint8_t request_type, uint8_t request,
                            uint16_t value, uint16_t index, void *data,
                            uint16_t len, uint8_t max_packet) {
    setup_buf[0] = request_type;
    setup_buf[1] = request;
    setup_buf[2] = value & 0xFF;
    setup_buf[3] = value >> 8;
    setup_buf[4] = index & 0xFF;
    setup_buf[5] = index >> 8;
    setup_buf[6] = len & 0xFF;
    setup_buf[7] = len >> 8;

    if ((request_type & 0x80) == 0 && data && len)
        memcpy_s(ctrl_buf, data, len);

    int td = 0;
    uhci_td_fill(td++, USB_PID_SETUP, addr, 0, 0, setup_buf, 8, 0);

    uint16_t done = 0;
    uint8_t toggle = 1;
    uint8_t data_pid = (request_type & 0x80) ? USB_PID_IN : USB_PID_OUT;
    while (done < len && td < 62) {
        uint16_t chunk = len - done;
        if (chunk > max_packet) chunk = max_packet;
        uhci_td_fill(td++, data_pid, addr, 0, toggle, ctrl_buf + done, chunk, 0);
        done += chunk;
        toggle ^= 1;
    }

    uint8_t status_pid = (request_type & 0x80) ? USB_PID_OUT : USB_PID_IN;
    uhci_td_fill(td++, status_pid, addr, 0, 1, 0, 0, 1);

    if (uhci_run_tds(td) != 0)
        return -1;

    if ((request_type & 0x80) && data && len)
        memcpy_s(data, ctrl_buf, len);
    return 0;
}

static int uhci_bulk_transfer(uint8_t in, void *data, uint32_t len) {
    uint8_t ep = in ? bulk_in_ep : bulk_out_ep;
    uint16_t maxp = in ? bulk_in_max : bulk_out_max;
    uint8_t *toggle = in ? &bulk_in_toggle : &bulk_out_toggle;
    uint8_t pid = in ? USB_PID_IN : USB_PID_OUT;
    uint8_t *buf = (uint8_t*)data;
    uint32_t done = 0;

    while (done < len) {
        int td = 0;
        while (done < len && td < 63) {
            uint16_t chunk = (uint16_t)(len - done);
            if (chunk > maxp) chunk = maxp;
            uhci_td_fill(td, pid, dev_addr, ep, *toggle, buf + done, chunk, 0);
            done += chunk;
            *toggle ^= 1;
            td++;
        }
        uhci_tds[td - 1].link = UHCI_PTR_TERM;
        if (uhci_run_tds(td) != 0)
            return -1;
    }
    return 0;
}

static int uhci_port_reset(int port_index) {
    uint16_t reg = (port_index == 0) ? UHCI_PORT1 : UHCI_PORT2;
    uint16_t st = inw(uhci_io + reg);
    if (!(st & UHCI_PORT_CONNECT))
        return -1;
    if (st & UHCI_PORT_LOWSPEED)
        return -1;

    outw(uhci_io + reg, (st | UHCI_PORT_CSC | UHCI_PORT_PEC | UHCI_PORT_RESET) & ~UHCI_PORT_ENABLE);
    delay_loop(80);
    st = inw(uhci_io + reg);
    outw(uhci_io + reg, (st | UHCI_PORT_CSC | UHCI_PORT_PEC) & ~UHCI_PORT_RESET);
    delay_loop(20);
    st = inw(uhci_io + reg);
    outw(uhci_io + reg, (st | UHCI_PORT_CSC | UHCI_PORT_PEC | UHCI_PORT_ENABLE) & ~UHCI_PORT_RESET);
    delay_loop(30);

    st = inw(uhci_io + reg);
    return (st & UHCI_PORT_ENABLE) ? 0 : -1;
}

static void ehci_qtd_fill(int idx, uint8_t pid, uint8_t toggle,
                          void *buf, uint32_t len, int last) {
    ehci_qtd_t *qtd = &ehci_qtds[idx];
    memset_s(qtd, 0, sizeof(*qtd));

    qtd->next = last ? EHCI_PTR_TERM : (uint32_t)&ehci_qtds[idx + 1];
    qtd->alt_next = EHCI_PTR_TERM;
    qtd->token = EHCI_QTD_ACTIVE | (3u << 10) |
                 ((uint32_t)pid << 8) |
                 ((len & 0x7FFFu) << 16) |
                 ((uint32_t)(toggle & 1) << 31);

    if (len && buf) {
        uint32_t addr = (uint32_t)buf;
        qtd->buffer[0] = addr;
        uint32_t page = (addr & 0xFFFFF000u) + 0x1000u;
        for (int i = 1; i < 5; i++) {
            qtd->buffer[i] = page;
            page += 0x1000u;
        }
    }
}

static void ehci_prepare_qh(uint8_t addr, uint8_t ep, uint16_t max_packet) {
    memset_s(&ehci_qh, 0, sizeof(ehci_qh));
    ehci_qh.horiz_link = ((uint32_t)&ehci_qh) | EHCI_PTR_QH;
    ehci_qh.ep_char = (uint32_t)addr |
                      ((uint32_t)ep << 8) |
                      (2u << 12) |
                      (1u << 14) |
                      (1u << 15) |
                      ((uint32_t)max_packet << 16);
    ehci_qh.ep_caps = 1u << 30;
    ehci_qh.next_qtd = EHCI_PTR_TERM;
    ehci_qh.alt_next_qtd = EHCI_PTR_TERM;
}

static int ehci_wait_async_enabled(void) {
    for (uint32_t i = 0; i < 1000000; i++) {
        if (ehci_read32(EHCI_REG_USBSTS) & EHCI_STS_ASS)
            return 0;
    }
    return -1;
}

static int ehci_wait_async_disabled(void) {
    for (uint32_t i = 0; i < 1000000; i++) {
        if ((ehci_read32(EHCI_REG_USBSTS) & EHCI_STS_ASS) == 0)
            return 0;
    }
    return -1;
}

static int ehci_run_qtds(int count, uint8_t addr, uint8_t ep, uint16_t max_packet) {
    if (count <= 0)
        return -1;

    uint32_t cmd = ehci_read32(EHCI_REG_USBCMD);
    ehci_write32(EHCI_REG_USBCMD, cmd & ~EHCI_CMD_ASE);
    if (ehci_wait_async_disabled() != 0)
        return -1;

    ehci_prepare_qh(addr, ep, max_packet);
    ehci_qh.next_qtd = (uint32_t)&ehci_qtds[0];
    ehci_write32(EHCI_REG_ASYNCLIST, (uint32_t)&ehci_qh);

    cmd = ehci_read32(EHCI_REG_USBCMD);
    ehci_write32(EHCI_REG_USBCMD, cmd | EHCI_CMD_RUN | EHCI_CMD_ASE);
    if (ehci_wait_async_enabled() != 0)
        return -1;

    for (uint32_t spins = 0; spins < 5000000; spins++) {
        int active = 0;
        for (int i = 0; i < count; i++) {
            if (ehci_qtds[i].token & EHCI_QTD_ACTIVE) {
                active = 1;
                break;
            }
        }
        if (!active)
            break;
        for (volatile int d = 0; d < 20; d++);
    }

    ehci_qh.next_qtd = EHCI_PTR_TERM;
    ehci_qh.alt_next_qtd = EHCI_PTR_TERM;

    for (int i = 0; i < count; i++) {
        if (ehci_qtds[i].token & EHCI_QTD_ACTIVE)
            return -1;
        if (ehci_qtds[i].token & EHCI_QTD_ERROR_MASK)
            return -1;
    }
    return 0;
}

static int ehci_control_msg(uint8_t addr, uint8_t request_type, uint8_t request,
                            uint16_t value, uint16_t index, void *data,
                            uint16_t len, uint8_t max_packet) {
    setup_buf[0] = request_type;
    setup_buf[1] = request;
    setup_buf[2] = value & 0xFF;
    setup_buf[3] = value >> 8;
    setup_buf[4] = index & 0xFF;
    setup_buf[5] = index >> 8;
    setup_buf[6] = len & 0xFF;
    setup_buf[7] = len >> 8;

    if ((request_type & 0x80) == 0 && data && len)
        memcpy_s(ctrl_buf, data, len);

    int td = 0;
    ehci_qtd_fill(td++, EHCI_PID_CODE_SETUP, 0, setup_buf, 8, 0);

    uint16_t done = 0;
    uint8_t toggle = 1;
    uint8_t data_pid = (request_type & 0x80) ? EHCI_PID_CODE_IN : EHCI_PID_CODE_OUT;
    while (done < len && td < 62) {
        uint16_t chunk = len - done;
        if (chunk > max_packet) chunk = max_packet;
        ehci_qtd_fill(td++, data_pid, toggle, ctrl_buf + done, chunk, 0);
        done += chunk;
        toggle ^= 1;
    }

    uint8_t status_pid = (request_type & 0x80) ? EHCI_PID_CODE_OUT : EHCI_PID_CODE_IN;
    ehci_qtd_fill(td++, status_pid, 1, 0, 0, 1);

    if (ehci_run_qtds(td, addr, 0, max_packet) != 0)
        return -1;

    if ((request_type & 0x80) && data && len)
        memcpy_s(data, ctrl_buf, len);
    return 0;
}

static int ehci_bulk_transfer(uint8_t in, void *data, uint32_t len) {
    uint8_t ep = in ? bulk_in_ep : bulk_out_ep;
    uint16_t maxp = in ? bulk_in_max : bulk_out_max;
    uint8_t *toggle = in ? &bulk_in_toggle : &bulk_out_toggle;
    uint8_t *buf = (uint8_t*)data;
    uint32_t done = 0;

    while (done < len) {
        int td = 0;
        while (done < len && td < 63) {
            uint32_t chunk = len - done;
            if (chunk > maxp) chunk = maxp;
            ehci_qtd_fill(td, in ? EHCI_PID_CODE_IN : EHCI_PID_CODE_OUT,
                          *toggle, buf + done, chunk, 0);
            done += chunk;
            *toggle ^= 1;
            td++;
        }

        ehci_qtds[td - 1].next = EHCI_PTR_TERM;
        if (ehci_run_qtds(td, dev_addr, ep, maxp) != 0)
            return -1;
    }
    return 0;
}

static int ehci_port_reset(int port_index) {
    uint32_t reg = EHCI_REG_PORTSC + (uint32_t)port_index * 4;
    uint32_t st = ehci_read32(reg);

    if (!(st & EHCI_PORT_CONNECT))
        return -1;

    if (!(st & EHCI_PORT_POWER)) {
        ehci_write32(reg, st | EHCI_PORT_POWER);
        delay_loop(20);
        st = ehci_read32(reg);
    }

    st &= ~EHCI_PORT_OWNER;
    ehci_write32(reg, st | EHCI_PORT_RESET);
    delay_loop(80);

    st = ehci_read32(reg);
    ehci_write32(reg, st & ~EHCI_PORT_RESET);
    delay_loop(80);

    st = ehci_read32(reg);
    if (st & EHCI_PORT_OWNER)
        return -1;
    return (st & EHCI_PORT_ENABLE) ? 0 : -1;
}

static int host_control_msg(uint8_t addr, uint8_t request_type, uint8_t request,
                            uint16_t value, uint16_t index, void *data,
                            uint16_t len, uint8_t max_packet) {
    if (usb_host == USB_HOST_EHCI)
        return ehci_control_msg(addr, request_type, request, value, index, data, len, max_packet);
    if (usb_host == USB_HOST_UHCI)
        return uhci_control_msg(addr, request_type, request, value, index, data, len, max_packet);
    return -1;
}

static int host_bulk_transfer(uint8_t in, void *data, uint32_t len) {
    if (usb_host == USB_HOST_EHCI)
        return ehci_bulk_transfer(in, data, len);
    if (usb_host == USB_HOST_UHCI)
        return uhci_bulk_transfer(in, data, len);
    return -1;
}

static int host_port_reset(int port_index) {
    if (usb_host == USB_HOST_EHCI)
        return ehci_port_reset(port_index);
    if (usb_host == USB_HOST_UHCI)
        return uhci_port_reset(port_index);
    return -1;
}

static int parse_config(uint8_t *cfg, uint16_t total_len, uint8_t *config_value) {
    int in_mass_iface = 0;
    bulk_in_ep = 0;
    bulk_out_ep = 0;
    bulk_in_max = 64;
    bulk_out_max = 64;
    *config_value = cfg[5];

    for (uint16_t pos = 0; pos + 2 <= total_len; ) {
        uint8_t len = cfg[pos];
        uint8_t type = cfg[pos + 1];
        if (len < 2 || pos + len > total_len)
            break;

        if (type == USB_DT_INTERFACE && len >= 9) {
            in_mass_iface = (cfg[pos + 5] == USB_CLASS_MASS_STORAGE &&
                             cfg[pos + 6] == USB_SUBCLASS_SCSI &&
                             cfg[pos + 7] == USB_PROTOCOL_BULK_ONLY);
        } else if (type == USB_DT_ENDPOINT && in_mass_iface && len >= 7) {
            uint8_t addr = cfg[pos + 2];
            uint8_t attrs = cfg[pos + 3] & 0x03;
            uint16_t maxp = cfg[pos + 4] | ((uint16_t)cfg[pos + 5] << 8);
            if (attrs == 2) {
                if (addr & 0x80) {
                    bulk_in_ep = addr & 0x0F;
                    bulk_in_max = maxp ? maxp : 64;
                } else {
                    bulk_out_ep = addr & 0x0F;
                    bulk_out_max = maxp ? maxp : 64;
                }
            }
        }
        pos += len;
    }

    return (bulk_in_ep && bulk_out_ep) ? 0 : -1;
}

static int msd_command(const uint8_t *cdb, uint8_t cdb_len, void *data, uint32_t len, int in) {
    memset_s(&cbw, 0, sizeof(cbw));
    cbw.signature = CBW_SIGNATURE;
    cbw.tag = cbw_tag++;
    cbw.data_len = len;
    cbw.flags = in ? 0x80 : 0x00;
    cbw.lun = 0;
    cbw.cb_len = cdb_len;
    memcpy_s(cbw.cb, cdb, cdb_len);

    if (host_bulk_transfer(0, &cbw, 31) != 0)
        return -1;
    if (len && data) {
        if (host_bulk_transfer(in ? 1 : 0, data, len) != 0)
            return -1;
    }
    memset_s(&csw, 0, sizeof(csw));
    if (host_bulk_transfer(1, &csw, 13) != 0)
        return -1;
    if (csw.signature != CSW_SIGNATURE || csw.tag != cbw.tag || csw.status != 0)
        return -1;
    return 0;
}

static int msd_test_ready(void) {
    uint8_t cdb[6] = {0x00, 0, 0, 0, 0, 0};
    for (int i = 0; i < 8; i++) {
        if (msd_command(cdb, 6, 0, 0, 0) == 0)
            return 0;
        delay_loop(20);
    }
    return -1;
}

static int msd_read_capacity(void) {
    uint8_t cdb[10] = {0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t cap[8];
    if (msd_command(cdb, 10, cap, sizeof(cap), 1) != 0)
        return -1;
    return 0;
}

static int enumerate_device(int port_index) {
    uint8_t desc[18];
    uint8_t cfg9[9];
    uint8_t config_value = 0;

    dev_addr = (uint8_t)(port_index + 1);
    if (dev_addr == 0)
        dev_addr = 1;
    ep0_max = 8;
    bulk_in_toggle = 0;
    bulk_out_toggle = 0;

    if (host_port_reset(port_index) != 0)
        return -1;

    memset_s(desc, 0, sizeof(desc));
    if (host_control_msg(0, 0x80, USB_REQ_GET_DESCRIPTOR,
                         (USB_DT_DEVICE << 8), 0, desc, 8, 8) != 0)
        return -1;

    ep0_max = desc[7] ? desc[7] : 8;

    if (host_control_msg(0, 0x00, USB_REQ_SET_ADDRESS, dev_addr, 0, 0, 0, ep0_max) != 0)
        return -1;
    delay_loop(30);

    if (host_control_msg(dev_addr, 0x80, USB_REQ_GET_DESCRIPTOR,
                         (USB_DT_DEVICE << 8), 0, desc, sizeof(desc), ep0_max) != 0)
        return -1;

    if (host_control_msg(dev_addr, 0x80, USB_REQ_GET_DESCRIPTOR,
                         (USB_DT_CONFIG << 8), 0, cfg9, sizeof(cfg9), ep0_max) != 0)
        return -1;

    uint16_t total_len = cfg9[2] | ((uint16_t)cfg9[3] << 8);
    if (total_len > sizeof(ctrl_buf))
        total_len = sizeof(ctrl_buf);

    if (host_control_msg(dev_addr, 0x80, USB_REQ_GET_DESCRIPTOR,
                         (USB_DT_CONFIG << 8), 0, ctrl_buf, total_len, ep0_max) != 0)
        return -1;
    if (parse_config(ctrl_buf, total_len, &config_value) != 0)
        return -1;

    if (host_control_msg(dev_addr, 0x00, USB_REQ_SET_CONFIG, config_value, 0, 0, 0, ep0_max) != 0)
        return -1;
    delay_loop(30);

    bulk_in_toggle = 0;
    bulk_out_toggle = 0;

    uint8_t inquiry[36];
    uint8_t inq_cdb[6] = {0x12, 0, 0, 0, sizeof(inquiry), 0};
    (void)msd_command(inq_cdb, 6, inquiry, sizeof(inquiry), 1);

    if (msd_test_ready() != 0)
        return -1;
    if (msd_read_capacity() != 0)
        return -1;

    return 0;
}

static void ehci_legacy_handoff(uint8_t bus, uint8_t dev, uint8_t fn, uint32_t hccparams) {
    uint8_t eecp = (hccparams >> 8) & 0xFF;

    for (int i = 0; eecp >= 0x40 && i < 8; i++) {
        uint32_t cap = pci_read32(bus, dev, fn, eecp);
        uint8_t id = cap & 0xFF;
        uint8_t next = (cap >> 8) & 0xFF;

        if (id == 1) {
            pci_write32(bus, dev, fn, eecp, cap | (1u << 24));
            for (int wait = 0; wait < 100000; wait++) {
                cap = pci_read32(bus, dev, fn, eecp);
                if ((cap & (1u << 16)) == 0)
                    break;
            }
            pci_write32(bus, dev, fn, eecp + 4, 0);
            return;
        }
        eecp = next;
    }
}

static int init_ehci(void) {
    for (int controller = 0; controller < 8; controller++) {
        uint8_t bus, dev, fn;
        uint32_t mmio_base;

        if (find_usb_controller(0x20, controller, &mmio_base, &bus, &dev, &fn) != 0)
            break;
        if (!mmio_base || map_mmio(mmio_base, 0x4000) != 0)
            continue;

        uint32_t pci_cmd = pci_read32(bus, dev, fn, 0x04);
        pci_write32(bus, dev, fn, 0x04, pci_cmd | 0x00000006);

        ehci_caps = (volatile uint8_t*)mmio_base;
        uint8_t cap_len = ehci_caps[0];
        ehci_regs = ehci_caps + cap_len;

        uint32_t hcsparams = *(volatile uint32_t *)(ehci_caps + 0x04);
        uint32_t hccparams = *(volatile uint32_t *)(ehci_caps + 0x08);
        ehci_port_count = hcsparams & 0x0F;
        if (ehci_port_count == 0)
            continue;
        if (ehci_port_count > 8)
            ehci_port_count = 8;

        ehci_legacy_handoff(bus, dev, fn, hccparams);

        ehci_write32(EHCI_REG_USBCMD, ehci_read32(EHCI_REG_USBCMD) & ~EHCI_CMD_RUN);
        for (int i = 0; i < 100000; i++) {
            if (ehci_read32(EHCI_REG_USBSTS) & EHCI_STS_HALTED)
                break;
        }

        ehci_write32(EHCI_REG_USBCMD, EHCI_CMD_RESET);
        for (int i = 0; i < 100000; i++) {
            if ((ehci_read32(EHCI_REG_USBCMD) & EHCI_CMD_RESET) == 0)
                break;
        }

        memset_s(&ehci_qh, 0, sizeof(ehci_qh));
        ehci_qh.horiz_link = ((uint32_t)&ehci_qh) | EHCI_PTR_QH;
        ehci_qh.ep_char = (1u << 14) | (1u << 15) | (64u << 16);
        ehci_qh.ep_caps = 1u << 30;
        ehci_qh.next_qtd = EHCI_PTR_TERM;
        ehci_qh.alt_next_qtd = EHCI_PTR_TERM;

        ehci_write32(EHCI_REG_USBINTR, 0);
        ehci_write32(EHCI_REG_USBSTS, 0x3F);
        ehci_write32(EHCI_REG_CTRLDSSEG, 0);
        ehci_write32(EHCI_REG_ASYNCLIST, (uint32_t)&ehci_qh);
        ehci_write32(EHCI_REG_CONFIGFLAG, 1);

        for (uint8_t i = 0; i < ehci_port_count; i++) {
            uint32_t reg = EHCI_REG_PORTSC + (uint32_t)i * 4;
            ehci_write32(reg, ehci_read32(reg) | EHCI_PORT_POWER);
        }

        ehci_write32(EHCI_REG_USBCMD, EHCI_CMD_RUN | EHCI_CMD_ASE);
        delay_loop(20);

        usb_host = USB_HOST_EHCI;
        for (uint8_t i = 0; i < ehci_port_count; i++) {
            if (enumerate_device(i) == 0)
                return 0;
        }
    }

    return -1;
}

static int init_uhci(void) {
    for (int controller = 0; controller < 8; controller++) {
        uint8_t bus, dev, fn;
        uint32_t io_base;

        if (find_usb_controller(0x00, controller, &io_base, &bus, &dev, &fn) != 0)
            break;

        uhci_io = (uint16_t)io_base;

        uint32_t cmd = pci_read32(bus, dev, fn, 0x04);
        pci_write32(bus, dev, fn, 0x04, cmd | 0x00000005);

        outw(uhci_io + UHCI_CMD, 0);
        outw(uhci_io + UHCI_CMD, UHCI_CMD_HCRESET);
        delay_loop(20);
        outw(uhci_io + UHCI_CMD, 0);

        for (int i = 0; i < 1024; i++)
            uhci_frame_list[i] = UHCI_PTR_TERM;

        outl(uhci_io + UHCI_FLBASE, (uint32_t)uhci_frame_list);
        outw(uhci_io + UHCI_FRNUM, 0);
        outw(uhci_io + UHCI_INTR, 0);
        outb(uhci_io + UHCI_SOFMOD, 0x40);
        outw(uhci_io + UHCI_STS, 0xFFFF);
        outw(uhci_io + UHCI_CMD, UHCI_CMD_RS | UHCI_CMD_CF | UHCI_CMD_MAXP);
        delay_loop(20);

        usb_host = USB_HOST_UHCI;
        for (int i = 0; i < 2; i++) {
            if (enumerate_device(i) == 0)
                return 0;
        }
    }

    return -1;
}

void usb_init(void) {
    usb_available = 0;
    usb_host = USB_HOST_NONE;
    cbw_tag = 1;

    if (init_ehci() == 0 || init_uhci() == 0)
        usb_available = 1;
}

int usb_ready(void) {
    return usb_available;
}

int usb_read_sectors(uint32_t lba, uint8_t sector_count, void *buffer) {
    if (!usb_available || !buffer || sector_count == 0)
        return -1;

    uint8_t *dst = (uint8_t*)buffer;
    for (uint8_t i = 0; i < sector_count; i++) {
        uint32_t cur = lba + i;
        uint8_t cdb[10] = {
            0x28, 0,
            (uint8_t)(cur >> 24), (uint8_t)(cur >> 16),
            (uint8_t)(cur >> 8), (uint8_t)cur,
            0, 0, 1, 0
        };
        if (msd_command(cdb, 10, sector_buf, 512, 1) != 0)
            return -1;
        memcpy_s(dst + (uint32_t)i * 512, sector_buf, 512);
    }
    return 0;
}

int usb_write_sectors(uint32_t lba, uint8_t sector_count, const void *buffer) {
    if (!usb_available || !buffer || sector_count == 0)
        return -1;

    const uint8_t *src = (const uint8_t*)buffer;
    for (uint8_t i = 0; i < sector_count; i++) {
        uint32_t cur = lba + i;
        uint8_t cdb[10] = {
            0x2A, 0,
            (uint8_t)(cur >> 24), (uint8_t)(cur >> 16),
            (uint8_t)(cur >> 8), (uint8_t)cur,
            0, 0, 1, 0
        };
        memcpy_s(sector_buf, src + (uint32_t)i * 512, 512);
        if (msd_command(cdb, 10, sector_buf, 512, 0) != 0)
            return -1;
    }
    return 0;
}
