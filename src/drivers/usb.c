#include "usb.h"
#include "../console.h"
#include "../irq/io.h"
#include "../mem/paging.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define USB_HOST_NONE 0
#define USB_HOST_UHCI 1
#define USB_HOST_EHCI 2
#define USB_HOST_XHCI 3

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
#define USB_DT_SS_EP_COMPANION  48

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

#define XHCI_CAP_HCSPARAMS1 0x04
#define XHCI_CAP_HCSPARAMS2 0x08
#define XHCI_CAP_HCCPARAMS1 0x10
#define XHCI_CAP_DBOFF      0x14
#define XHCI_CAP_RTSOFF     0x18

#define XHCI_OP_USBCMD      0x00
#define XHCI_OP_USBSTS      0x04
#define XHCI_OP_PAGESIZE    0x08
#define XHCI_OP_CRCR        0x18
#define XHCI_OP_DCBAAP      0x30
#define XHCI_OP_CONFIG      0x38
#define XHCI_OP_PORTSC      0x400

#define XHCI_CMD_RUN        0x00000001
#define XHCI_CMD_RESET      0x00000002

#define XHCI_STS_HCH        0x00000001
#define XHCI_STS_CNR        0x00000800

#define XHCI_PORT_CCS       0x00000001
#define XHCI_PORT_PED       0x00000002
#define XHCI_PORT_RESET     0x00000010
#define XHCI_PORT_POWER     0x00000200
#define XHCI_PORT_CHANGE    0x00FE0000

#define XHCI_TRB_NORMAL       1
#define XHCI_TRB_SETUP_STAGE  2
#define XHCI_TRB_DATA_STAGE   3
#define XHCI_TRB_STATUS_STAGE 4
#define XHCI_TRB_LINK         6
#define XHCI_TRB_ENABLE_SLOT  9
#define XHCI_TRB_ADDRESS_DEV  11
#define XHCI_TRB_CONFIG_EP    12
#define XHCI_TRB_TRANSFER     32
#define XHCI_TRB_CMD_COMPLETE 33

#define XHCI_CC_SUCCESS      1
#define XHCI_CC_SHORT_PACKET 13

#define XHCI_RING_TRBS 64
#define XHCI_EVENT_TRBS 256
#define XHCI_MAX_SLOTS 32
#define XHCI_MAX_SCRATCHPADS 32

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
    uint32_t param_lo;
    uint32_t param_hi;
    uint32_t status;
    uint32_t control;
} __attribute__((packed, aligned(16))) xhci_trb_t;

typedef struct {
    xhci_trb_t trb[XHCI_RING_TRBS];
    uint8_t cycle;
    uint8_t enqueue;
} __attribute__((aligned(64))) xhci_ring_t;

typedef struct {
    uint32_t base_lo;
    uint32_t base_hi;
    uint32_t size;
    uint32_t reserved;
} __attribute__((packed, aligned(16))) xhci_erst_entry_t;

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
static uint8_t bulk_in_burst = 0;
static uint8_t bulk_out_burst = 0;
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

static volatile uint8_t *xhci_caps = 0;
static volatile uint8_t *xhci_regs = 0;
static volatile uint8_t *xhci_runtime = 0;
static volatile uint32_t *xhci_doorbells = 0;
static uint8_t xhci_ctx_size = 32;
static uint8_t xhci_port_count = 0;
static uint8_t xhci_slot_id = 0;
static uint8_t xhci_root_port = 0;
static uint8_t xhci_port_speed = 0;
static uint8_t xhci_bulk_in_dci = 0;
static uint8_t xhci_bulk_out_dci = 0;
static xhci_ring_t xhci_cmd_ring __attribute__((aligned(64)));
static xhci_ring_t xhci_ep0_ring __attribute__((aligned(64)));
static xhci_ring_t xhci_bulk_in_ring __attribute__((aligned(64)));
static xhci_ring_t xhci_bulk_out_ring __attribute__((aligned(64)));
static xhci_trb_t xhci_event_ring[XHCI_EVENT_TRBS] __attribute__((aligned(64)));
static uint8_t xhci_event_cycle = 1;
static uint16_t xhci_event_dequeue = 0;
static uint32_t xhci_dcbaa[XHCI_MAX_SLOTS + 1][2] __attribute__((aligned(64)));
static uint32_t xhci_scratchpad_ptrs[XHCI_MAX_SCRATCHPADS][2] __attribute__((aligned(64)));
static uint8_t xhci_scratchpads[XHCI_MAX_SCRATCHPADS][4096] __attribute__((aligned(4096)));
static xhci_erst_entry_t xhci_erst[1] __attribute__((aligned(64)));
static uint8_t xhci_input_ctx[33 * 64] __attribute__((aligned(64)));
static uint8_t xhci_device_ctx[32 * 64] __attribute__((aligned(64)));

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

static inline void usb_dma_barrier(void) {
    __asm__ volatile ("" ::: "memory");
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
                    if ((raw & 0x6) == 0x4 && pci_read32((uint8_t)bus, dev, fn, bar + 4) != 0)
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

static uint32_t xhci_cap_read32(uint32_t off) {
    return *(volatile uint32_t *)(xhci_caps + off);
}

static uint32_t xhci_read32(uint32_t off) {
    return *(volatile uint32_t *)(xhci_regs + off);
}

static void xhci_write32(uint32_t off, uint32_t value) {
    *(volatile uint32_t *)(xhci_regs + off) = value;
}

static void xhci_write64(uint32_t off, uint32_t lo, uint32_t hi) {
    *(volatile uint32_t *)(xhci_regs + off) = lo;
    *(volatile uint32_t *)(xhci_regs + off + 4) = hi;
}

static void xhci_intr_write32(uint32_t off, uint32_t value) {
    *(volatile uint32_t *)(xhci_runtime + 0x20 + off) = value;
}

static void xhci_intr_write64(uint32_t off, uint32_t lo, uint32_t hi) {
    *(volatile uint32_t *)(xhci_runtime + 0x20 + off) = lo;
    *(volatile uint32_t *)(xhci_runtime + 0x20 + off + 4) = hi;
}

static uint32_t *xhci_input_ctrl(void) {
    return (uint32_t*)xhci_input_ctx;
}

static uint32_t *xhci_input_slot(void) {
    return (uint32_t*)(xhci_input_ctx + xhci_ctx_size);
}

static uint32_t *xhci_input_ep(uint8_t dci) {
    return (uint32_t*)(xhci_input_ctx + ((uint32_t)dci + 1) * xhci_ctx_size);
}

static void xhci_ring_init(xhci_ring_t *ring) {
    memset_s(ring, 0, sizeof(*ring));
    ring->cycle = 1;
    ring->enqueue = 0;
    ring->trb[XHCI_RING_TRBS - 1].param_lo = (uint32_t)&ring->trb[0];
    ring->trb[XHCI_RING_TRBS - 1].param_hi = 0;
    ring->trb[XHCI_RING_TRBS - 1].status = 0;
    ring->trb[XHCI_RING_TRBS - 1].control = (XHCI_TRB_LINK << 10) | (1 << 1) | ring->cycle;
}

static xhci_trb_t *xhci_ring_push(xhci_ring_t *ring, uint32_t param_lo, uint32_t param_hi,
                                  uint32_t status, uint32_t control) {
    xhci_trb_t *trb = &ring->trb[ring->enqueue];

    trb->param_lo = param_lo;
    trb->param_hi = param_hi;
    trb->status = status;
    usb_dma_barrier();
    trb->control = control | ring->cycle;

    ring->enqueue++;
    if (ring->enqueue == XHCI_RING_TRBS - 1) {
        ring->trb[XHCI_RING_TRBS - 1].control =
            (XHCI_TRB_LINK << 10) | (1 << 1) | ring->cycle;
        ring->enqueue = 0;
        ring->cycle ^= 1;
    }

    return trb;
}

static int xhci_next_event(xhci_trb_t *out) {
    for (uint32_t wait = 0; wait < 5000000; wait++) {
        volatile xhci_trb_t *ev = &xhci_event_ring[xhci_event_dequeue];
        if ((ev->control & 1) == xhci_event_cycle) {
            out->param_lo = ev->param_lo;
            out->param_hi = ev->param_hi;
            out->status = ev->status;
            out->control = ev->control;
            xhci_event_dequeue++;
            if (xhci_event_dequeue == XHCI_EVENT_TRBS) {
                xhci_event_dequeue = 0;
                xhci_event_cycle ^= 1;
            }

            xhci_intr_write64(0x18, (uint32_t)&xhci_event_ring[xhci_event_dequeue] | (1 << 3), 0);
            return 0;
        }
        for (volatile int d = 0; d < 20; d++);
    }
    return -1;
}

static int xhci_wait_command(xhci_trb_t *cmd, uint8_t *out_slot) {
    for (;;) {
        xhci_trb_t ev;
        if (xhci_next_event(&ev) != 0)
            return -1;

        uint32_t type = (ev.control >> 10) & 0x3F;
        if (type != XHCI_TRB_CMD_COMPLETE)
            continue;
        if (ev.param_lo != (uint32_t)cmd)
            continue;

        uint8_t cc = (ev.status >> 24) & 0xFF;
        if (cc != XHCI_CC_SUCCESS)
            return -1;
        if (out_slot)
            *out_slot = (ev.control >> 24) & 0xFF;
        return 0;
    }
}

static int xhci_wait_transfer(xhci_trb_t *last_trb, uint8_t dci) {
    for (;;) {
        xhci_trb_t ev;
        if (xhci_next_event(&ev) != 0)
            return -1;

        uint32_t type = (ev.control >> 10) & 0x3F;
        if (type != XHCI_TRB_TRANSFER)
            continue;
        if (ev.param_lo != (uint32_t)last_trb)
            continue;
        if (((ev.control >> 24) & 0xFF) != xhci_slot_id)
            continue;
        if (((ev.control >> 16) & 0x1F) != dci)
            continue;

        uint8_t cc = (ev.status >> 24) & 0xFF;
        return (cc == XHCI_CC_SUCCESS || cc == XHCI_CC_SHORT_PACKET) ? 0 : -1;
    }
}

static xhci_trb_t *xhci_command(uint32_t param_lo, uint32_t param_hi,
                                uint32_t status, uint32_t control) {
    xhci_trb_t *trb = xhci_ring_push(&xhci_cmd_ring, param_lo, param_hi, status, control);
    xhci_doorbells[0] = 0;
    return trb;
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

static uint16_t xhci_ep0_packet_size(uint8_t speed) {
    if (speed >= 4)
        return 512;
    if (speed == 3)
        return 64;
    return 8;
}

static void xhci_setup_ep_context(uint32_t *ep, uint8_t ep_type,
                                  uint16_t max_packet, uint8_t max_burst,
                                  xhci_ring_t *ring) {
    memset_s(ep, 0, xhci_ctx_size);
    ep[1] = (3u << 1) | ((uint32_t)ep_type << 3) |
            ((uint32_t)max_burst << 8) |
            ((uint32_t)max_packet << 16);
    ep[2] = ((uint32_t)&ring->trb[0]) | 1;
    ep[3] = 0;
    ep[4] = max_packet;
}

static int xhci_address_device(uint8_t root_port, uint8_t speed) {
    xhci_trb_t *cmd = xhci_command(0, 0, 0, XHCI_TRB_ENABLE_SLOT << 10);
    if (xhci_wait_command(cmd, &xhci_slot_id) != 0)
        return -1;
    if (xhci_slot_id == 0 || xhci_slot_id > XHCI_MAX_SLOTS)
        return -1;

    memset_s(xhci_input_ctx, 0, sizeof(xhci_input_ctx));
    memset_s(xhci_device_ctx, 0, sizeof(xhci_device_ctx));
    xhci_ring_init(&xhci_ep0_ring);

    xhci_dcbaa[xhci_slot_id][0] = (uint32_t)xhci_device_ctx;
    xhci_dcbaa[xhci_slot_id][1] = 0;

    uint32_t *ctrl = xhci_input_ctrl();
    uint32_t *slot = xhci_input_slot();
    uint32_t *ep0 = xhci_input_ep(1);
    uint16_t maxp = xhci_ep0_packet_size(speed);

    ctrl[1] = (1u << 0) | (1u << 1);
    slot[0] = ((uint32_t)speed << 20) | (1u << 27);
    slot[1] = ((uint32_t)root_port << 16);
    xhci_setup_ep_context(ep0, 4, maxp, 0, &xhci_ep0_ring);

    cmd = xhci_command((uint32_t)xhci_input_ctx, 0, 0,
                       (XHCI_TRB_ADDRESS_DEV << 10) |
                       ((uint32_t)xhci_slot_id << 24));
    if (xhci_wait_command(cmd, 0) != 0)
        return -1;

    ep0_max = (uint8_t)(maxp > 255 ? 255 : maxp);
    return 0;
}

static int xhci_port_reset(int port_index) {
    uint32_t reg = XHCI_OP_PORTSC + (uint32_t)port_index * 0x10;
    uint32_t st = xhci_read32(reg);

    if (!(st & XHCI_PORT_CCS))
        return -1;

    if (!(st & XHCI_PORT_POWER)) {
        xhci_write32(reg, (st & ~XHCI_PORT_CHANGE) | XHCI_PORT_POWER);
        delay_loop(20);
        st = xhci_read32(reg);
    }

    if (!(st & XHCI_PORT_PED)) {
        xhci_write32(reg, (st & ~XHCI_PORT_CHANGE) | XHCI_PORT_POWER | XHCI_PORT_RESET);
        for (int i = 0; i < 100000; i++) {
            st = xhci_read32(reg);
            if ((st & XHCI_PORT_RESET) == 0)
                break;
        }
        delay_loop(20);
    }

    st = xhci_read32(reg);
    xhci_write32(reg, (st & ~XHCI_PORT_CHANGE) | XHCI_PORT_CHANGE);
    if (!(st & XHCI_PORT_PED))
        return -1;

    xhci_port_speed = (st >> 10) & 0x0F;
    xhci_root_port = (uint8_t)(port_index + 1);
    return xhci_address_device((uint8_t)(port_index + 1), xhci_port_speed);
}

static int xhci_control_msg(uint8_t addr, uint8_t request_type, uint8_t request,
                            uint16_t value, uint16_t index, void *data,
                            uint16_t len, uint8_t max_packet) {
    (void)addr;
    (void)max_packet;

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

    uint32_t setup_lo = setup_buf[0] | ((uint32_t)setup_buf[1] << 8) |
                        ((uint32_t)setup_buf[2] << 16) | ((uint32_t)setup_buf[3] << 24);
    uint32_t setup_hi = setup_buf[4] | ((uint32_t)setup_buf[5] << 8) |
                        ((uint32_t)setup_buf[6] << 16) | ((uint32_t)setup_buf[7] << 24);
    uint32_t trt = 0;
    if (len)
        trt = (request_type & 0x80) ? 3 : 2;

    xhci_ring_push(&xhci_ep0_ring, setup_lo, setup_hi, 8,
                   (XHCI_TRB_SETUP_STAGE << 10) | (1 << 6) | (trt << 16));

    if (len) {
        xhci_ring_push(&xhci_ep0_ring, (uint32_t)ctrl_buf, 0, len,
                       (XHCI_TRB_DATA_STAGE << 10) |
                       ((request_type & 0x80) ? (1 << 16) : 0));
    }

    uint32_t status_in = (request_type & 0x80) ? 0 : (1 << 16);
    xhci_trb_t *status = xhci_ring_push(&xhci_ep0_ring, 0, 0, 0,
                                        (XHCI_TRB_STATUS_STAGE << 10) |
                                        status_in | (1 << 5));

    xhci_doorbells[xhci_slot_id] = 1;
    if (xhci_wait_transfer(status, 1) != 0)
        return -1;

    if ((request_type & 0x80) && data && len)
        memcpy_s(data, ctrl_buf, len);
    return 0;
}

static int xhci_configure_bulk_endpoints(void) {
    xhci_bulk_in_dci = (uint8_t)(bulk_in_ep * 2 + 1);
    xhci_bulk_out_dci = (uint8_t)(bulk_out_ep * 2);
    uint8_t last_dci = xhci_bulk_in_dci > xhci_bulk_out_dci ? xhci_bulk_in_dci : xhci_bulk_out_dci;

    if (!xhci_slot_id || !xhci_bulk_in_dci || !xhci_bulk_out_dci || last_dci > 31)
        return -1;

    memset_s(xhci_input_ctx, 0, sizeof(xhci_input_ctx));
    xhci_ring_init(&xhci_bulk_in_ring);
    xhci_ring_init(&xhci_bulk_out_ring);

    uint32_t *ctrl = xhci_input_ctrl();
    uint32_t *slot = xhci_input_slot();
    uint32_t *ep_in = xhci_input_ep(xhci_bulk_in_dci);
    uint32_t *ep_out = xhci_input_ep(xhci_bulk_out_dci);

    ctrl[1] = (1u << 0) | (1u << xhci_bulk_in_dci) | (1u << xhci_bulk_out_dci);
    slot[0] = ((uint32_t)xhci_port_speed << 20) | ((uint32_t)last_dci << 27);
    slot[1] = ((uint32_t)xhci_root_port << 16);
    xhci_setup_ep_context(ep_in, 6, bulk_in_max, bulk_in_burst, &xhci_bulk_in_ring);
    xhci_setup_ep_context(ep_out, 2, bulk_out_max, bulk_out_burst, &xhci_bulk_out_ring);

    xhci_trb_t *cmd = xhci_command((uint32_t)xhci_input_ctx, 0, 0,
                                   (XHCI_TRB_CONFIG_EP << 10) |
                                   ((uint32_t)xhci_slot_id << 24));
    return xhci_wait_command(cmd, 0);
}

static int xhci_bulk_transfer(uint8_t in, void *data, uint32_t len) {
    xhci_ring_t *ring = in ? &xhci_bulk_in_ring : &xhci_bulk_out_ring;
    uint8_t dci = in ? xhci_bulk_in_dci : xhci_bulk_out_dci;
    uint8_t *buf = (uint8_t*)data;
    uint32_t done = 0;

    if (!dci)
        return -1;

    while (done < len) {
        uint32_t chunk = len - done;
        if (chunk > 65536)
            chunk = 65536;

        xhci_trb_t *trb = xhci_ring_push(ring, (uint32_t)(buf + done), 0, chunk,
                                         (XHCI_TRB_NORMAL << 10) | (1 << 5) | (1 << 2));
        xhci_doorbells[xhci_slot_id] = dci;
        if (xhci_wait_transfer(trb, dci) != 0)
            return -1;
        done += chunk;
    }

    return 0;
}

static int host_control_msg(uint8_t addr, uint8_t request_type, uint8_t request,
                            uint16_t value, uint16_t index, void *data,
                            uint16_t len, uint8_t max_packet) {
    if (usb_host == USB_HOST_XHCI)
        return xhci_control_msg(addr, request_type, request, value, index, data, len, max_packet);
    if (usb_host == USB_HOST_EHCI)
        return ehci_control_msg(addr, request_type, request, value, index, data, len, max_packet);
    if (usb_host == USB_HOST_UHCI)
        return uhci_control_msg(addr, request_type, request, value, index, data, len, max_packet);
    return -1;
}

static int host_bulk_transfer(uint8_t in, void *data, uint32_t len) {
    if (usb_host == USB_HOST_XHCI)
        return xhci_bulk_transfer(in, data, len);
    if (usb_host == USB_HOST_EHCI)
        return ehci_bulk_transfer(in, data, len);
    if (usb_host == USB_HOST_UHCI)
        return uhci_bulk_transfer(in, data, len);
    return -1;
}

static int host_port_reset(int port_index) {
    if (usb_host == USB_HOST_XHCI)
        return xhci_port_reset(port_index);
    if (usb_host == USB_HOST_EHCI)
        return ehci_port_reset(port_index);
    if (usb_host == USB_HOST_UHCI)
        return uhci_port_reset(port_index);
    return -1;
}

static int parse_config(uint8_t *cfg, uint16_t total_len, uint8_t *config_value) {
    int in_mass_iface = 0;
    int last_bulk_dir = 0;
    bulk_in_ep = 0;
    bulk_out_ep = 0;
    bulk_in_max = 64;
    bulk_out_max = 64;
    bulk_in_burst = 0;
    bulk_out_burst = 0;
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
            last_bulk_dir = 0;
        } else if (type == USB_DT_ENDPOINT && in_mass_iface && len >= 7) {
            uint8_t addr = cfg[pos + 2];
            uint8_t attrs = cfg[pos + 3] & 0x03;
            uint16_t maxp = cfg[pos + 4] | ((uint16_t)cfg[pos + 5] << 8);
            if (attrs == 2) {
                if (addr & 0x80) {
                    bulk_in_ep = addr & 0x0F;
                    bulk_in_max = maxp ? maxp : 64;
                    last_bulk_dir = 1;
                } else {
                    bulk_out_ep = addr & 0x0F;
                    bulk_out_max = maxp ? maxp : 64;
                    last_bulk_dir = 2;
                }
            }
        } else if (type == USB_DT_SS_EP_COMPANION && in_mass_iface && len >= 6) {
            if (last_bulk_dir == 1)
                bulk_in_burst = cfg[pos + 2];
            else if (last_bulk_dir == 2)
                bulk_out_burst = cfg[pos + 2];
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

    if (usb_host == USB_HOST_XHCI && xhci_port_speed >= 4)
        ep0_max = 255;
    else
        ep0_max = desc[7] ? desc[7] : 8;

    if (usb_host != USB_HOST_XHCI) {
        if (host_control_msg(0, 0x00, USB_REQ_SET_ADDRESS, dev_addr, 0, 0, 0, ep0_max) != 0)
            return -1;
        delay_loop(30);
    }

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

    if (usb_host == USB_HOST_XHCI && xhci_configure_bulk_endpoints() != 0)
        return -1;

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

static void xhci_legacy_handoff(uint32_t hccparams1) {
    uint32_t off = (hccparams1 >> 16) & 0xFFFF;

    for (int i = 0; off && i < 16; i++) {
        uint32_t cap = xhci_cap_read32(off << 2);
        uint8_t id = cap & 0xFF;
        uint8_t next = (cap >> 8) & 0xFF;

        if (id == 1) {
            *(volatile uint32_t *)(xhci_caps + (off << 2)) = cap | (1u << 24);
            for (int wait = 0; wait < 100000; wait++) {
                cap = xhci_cap_read32(off << 2);
                if ((cap & (1u << 16)) == 0)
                    break;
            }
            *(volatile uint32_t *)(xhci_caps + (off << 2) + 4) = 0;
            return;
        }

        if (!next)
            break;
        off += next;
    }
}

static int init_xhci_controller(uint8_t bus, uint8_t dev, uint8_t fn, uint32_t mmio_base) {
    if (!mmio_base || map_mmio(mmio_base, 0x10000) != 0)
        return -1;

    uint32_t pci_cmd = pci_read32(bus, dev, fn, 0x04);
    pci_write32(bus, dev, fn, 0x04, pci_cmd | 0x00000006);

    xhci_caps = (volatile uint8_t*)mmio_base;
    uint8_t cap_len = xhci_caps[0];
    xhci_regs = xhci_caps + cap_len;

    uint32_t hcs1 = xhci_cap_read32(XHCI_CAP_HCSPARAMS1);
    uint32_t hcs2 = xhci_cap_read32(XHCI_CAP_HCSPARAMS2);
    uint32_t hcc1 = xhci_cap_read32(XHCI_CAP_HCCPARAMS1);
    uint32_t dboff = xhci_cap_read32(XHCI_CAP_DBOFF) & ~0x3u;
    uint32_t rtsoff = xhci_cap_read32(XHCI_CAP_RTSOFF) & ~0x1Fu;

    uint8_t max_slots = hcs1 & 0xFF;
    xhci_port_count = (hcs1 >> 24) & 0xFF;
    xhci_ctx_size = (hcc1 & (1u << 2)) ? 64 : 32;
    xhci_doorbells = (volatile uint32_t *)(xhci_caps + dboff);
    xhci_runtime = xhci_caps + rtsoff;

    if (!max_slots || !xhci_port_count)
        return -1;
    if (max_slots > XHCI_MAX_SLOTS)
        max_slots = XHCI_MAX_SLOTS;
    if (xhci_port_count > 32)
        xhci_port_count = 32;

    uint32_t scratchpads = ((hcs2 >> 16) & 0x3E0) | ((hcs2 >> 27) & 0x1F);
    if (scratchpads > XHCI_MAX_SCRATCHPADS)
        return -1;

    xhci_legacy_handoff(hcc1);

    xhci_write32(XHCI_OP_USBCMD, xhci_read32(XHCI_OP_USBCMD) & ~XHCI_CMD_RUN);
    for (int i = 0; i < 100000; i++) {
        if (xhci_read32(XHCI_OP_USBSTS) & XHCI_STS_HCH)
            break;
    }

    xhci_write32(XHCI_OP_USBCMD, XHCI_CMD_RESET);
    for (int i = 0; i < 100000; i++) {
        if ((xhci_read32(XHCI_OP_USBCMD) & XHCI_CMD_RESET) == 0)
            break;
    }
    for (int i = 0; i < 100000; i++) {
        if ((xhci_read32(XHCI_OP_USBSTS) & XHCI_STS_CNR) == 0)
            break;
    }

    memset_s(xhci_dcbaa, 0, sizeof(xhci_dcbaa));
    memset_s(xhci_scratchpad_ptrs, 0, sizeof(xhci_scratchpad_ptrs));
    if (scratchpads) {
        for (uint32_t i = 0; i < scratchpads; i++) {
            xhci_scratchpad_ptrs[i][0] = (uint32_t)&xhci_scratchpads[i][0];
            xhci_scratchpad_ptrs[i][1] = 0;
        }
        xhci_dcbaa[0][0] = (uint32_t)xhci_scratchpad_ptrs;
        xhci_dcbaa[0][1] = 0;
    }

    xhci_ring_init(&xhci_cmd_ring);
    memset_s(xhci_event_ring, 0, sizeof(xhci_event_ring));
    xhci_event_cycle = 1;
    xhci_event_dequeue = 0;

    xhci_erst[0].base_lo = (uint32_t)xhci_event_ring;
    xhci_erst[0].base_hi = 0;
    xhci_erst[0].size = XHCI_EVENT_TRBS;
    xhci_erst[0].reserved = 0;

    xhci_write32(XHCI_OP_USBSTS, 0xFFFFFFFF);
    xhci_write64(XHCI_OP_DCBAAP, (uint32_t)xhci_dcbaa, 0);
    xhci_write64(XHCI_OP_CRCR, ((uint32_t)&xhci_cmd_ring.trb[0]) | 1, 0);
    xhci_write32(XHCI_OP_CONFIG, max_slots);

    xhci_intr_write32(0x00, 3);
    xhci_intr_write32(0x08, 1);
    xhci_intr_write64(0x10, (uint32_t)xhci_erst, 0);
    xhci_intr_write64(0x18, (uint32_t)xhci_event_ring | (1 << 3), 0);
    xhci_intr_write32(0x00, 2);

    xhci_write32(XHCI_OP_USBCMD, XHCI_CMD_RUN);
    for (int i = 0; i < 100000; i++) {
        if ((xhci_read32(XHCI_OP_USBSTS) & XHCI_STS_HCH) == 0)
            break;
    }

    usb_host = USB_HOST_XHCI;
    for (uint8_t i = 0; i < xhci_port_count; i++) {
        if (enumerate_device(i) == 0)
            return 0;
    }

    return -1;
}

static int init_xhci(void) {
    for (int controller = 0; controller < 8; controller++) {
        uint8_t bus, dev, fn;
        uint32_t mmio_base;

        if (find_usb_controller(0x30, controller, &mmio_base, &bus, &dev, &fn) != 0)
            break;
        if (init_xhci_controller(bus, dev, fn, mmio_base) == 0)
            return 0;
    }

    return -1;
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

    if (init_xhci() == 0 || init_ehci() == 0 || init_uhci() == 0)
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
