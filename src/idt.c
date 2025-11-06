#include <stdint.h>
#include "console.h"

extern void vga_set_color(uint8_t background, uint8_t foreground);

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idtr;

extern void isr0(void);  extern void isr1(void);  extern void isr2(void);  extern void isr3(void);
extern void isr4(void);  extern void isr5(void);  extern void isr6(void);  extern void isr7(void);
extern void isr8(void);  extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void); extern void isr15(void);
extern void isr16(void); extern void isr17(void); extern void isr18(void); extern void isr19(void);
extern void isr20(void); extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void); extern void isr27(void);
extern void isr28(void); extern void isr29(void); extern void isr30(void); extern void isr31(void);

extern void irq0(void);  extern void irq1(void);
extern void irq2(void);  extern void irq3(void);
extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);
extern void irq8(void);  extern void irq9(void);
extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void);
extern void irq14(void); extern void irq15(void);

extern void pic_send_eoi(int irq);
extern void perform_task_switch(void);

static inline void load_idt(void) {
    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (uint32_t)&idt;
    __asm__ volatile ("lidt %0" :: "m"(idtr));
    printf("IDT loaded\n");
}

static void idt_set_entry(int num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].offset_low  = base & 0xFFFF;
    idt[num].selector    = sel;
    idt[num].zero        = 0;
    idt[num].type_attr   = flags;
    idt[num].offset_high = (base >> 16) & 0xFFFF;
}

typedef void (*irq_handler_t)(void);
static irq_handler_t irq_handlers[16];

void irq_install_handler(int irq, irq_handler_t h) {
    if (irq >= 0 && irq < 16) irq_handlers[irq] = h;
}

void irq_invoke_from_stub(int vector) {
    int irq = vector - 32;
    if (irq >= 0 && irq < 16) {
        irq_handler_t h = irq_handlers[irq];
        if (h) h();
    }
    pic_send_eoi(irq);
}

void isr_common_stub(int vector, int error_code) {
    vga_set_color(12,15);
    printf("[EXCEPTION] vector=%d err=0x%x\n", vector, (unsigned)error_code);
    for (;;) __asm__ volatile ("hlt");
}

/* Syscall handler for int 0x80 */
void syscall_handler_idt(void) {
    perform_task_switch();
}

void idt_init(void) {
    for (int i = 0; i < 256; ++i) idt_set_entry(i, 0, 0, 0);

    idt_set_entry(0,  (uint32_t)isr0,  0x08, 0x8E);
    idt_set_entry(1,  (uint32_t)isr1,  0x08, 0x8E);
    idt_set_entry(2,  (uint32_t)isr2,  0x08, 0x8E);
    idt_set_entry(3,  (uint32_t)isr3,  0x08, 0x8E);
    idt_set_entry(4,  (uint32_t)isr4,  0x08, 0x8E);
    idt_set_entry(5,  (uint32_t)isr5,  0x08, 0x8E);
    idt_set_entry(6,  (uint32_t)isr6,  0x08, 0x8E);
    idt_set_entry(7,  (uint32_t)isr7,  0x08, 0x8E);
    idt_set_entry(8,  (uint32_t)isr8,  0x08, 0x8E);
    idt_set_entry(9,  (uint32_t)isr9,  0x08, 0x8E);
    idt_set_entry(10, (uint32_t)isr10, 0x08, 0x8E);
    idt_set_entry(11, (uint32_t)isr11, 0x08, 0x8E);
    idt_set_entry(12, (uint32_t)isr12, 0x08, 0x8E);
    idt_set_entry(13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_entry(14, (uint32_t)isr14, 0x08, 0x8E);
    idt_set_entry(15, (uint32_t)isr15, 0x08, 0x8E);
    idt_set_entry(16, (uint32_t)isr16, 0x08, 0x8E);
    idt_set_entry(17, (uint32_t)isr17, 0x08, 0x8E);
    idt_set_entry(18, (uint32_t)isr18, 0x08, 0x8E);
    idt_set_entry(19, (uint32_t)isr19, 0x08, 0x8E);
    idt_set_entry(20, (uint32_t)isr20, 0x08, 0x8E);
    idt_set_entry(21, (uint32_t)isr21, 0x08, 0x8E);
    idt_set_entry(22, (uint32_t)isr22, 0x08, 0x8E);
    idt_set_entry(23, (uint32_t)isr23, 0x08, 0x8E);
    idt_set_entry(24, (uint32_t)isr24, 0x08, 0x8E);
    idt_set_entry(25, (uint32_t)isr25, 0x08, 0x8E);
    idt_set_entry(26, (uint32_t)isr26, 0x08, 0x8E);
    idt_set_entry(27, (uint32_t)isr27, 0x08, 0x8E);
    idt_set_entry(28, (uint32_t)isr28, 0x08, 0x8E);
    idt_set_entry(29, (uint32_t)isr29, 0x08, 0x8E);
    idt_set_entry(30, (uint32_t)isr30, 0x08, 0x8E);
    idt_set_entry(31, (uint32_t)isr31, 0x08, 0x8E);

    idt_set_entry(32, (uint32_t)irq0,  0x08, 0x8E);
    idt_set_entry(33, (uint32_t)irq1,  0x08, 0x8E);
    idt_set_entry(34, (uint32_t)irq2,  0x08, 0x8E);
    idt_set_entry(35, (uint32_t)irq3,  0x08, 0x8E);
    idt_set_entry(36, (uint32_t)irq4,  0x08, 0x8E);
    idt_set_entry(37, (uint32_t)irq5,  0x08, 0x8E);
    idt_set_entry(38, (uint32_t)irq6,  0x08, 0x8E);
    idt_set_entry(39, (uint32_t)irq7,  0x08, 0x8E);
    idt_set_entry(40, (uint32_t)irq8,  0x08, 0x8E);
    idt_set_entry(41, (uint32_t)irq9,  0x08, 0x8E);
    idt_set_entry(42, (uint32_t)irq10, 0x08, 0x8E);
    idt_set_entry(43, (uint32_t)irq11, 0x08, 0x8E);
    idt_set_entry(44, (uint32_t)irq12, 0x08, 0x8E);
    idt_set_entry(45, (uint32_t)irq13, 0x08, 0x8E);
    idt_set_entry(46, (uint32_t)irq14, 0x08, 0x8E);
    idt_set_entry(47, (uint32_t)irq15, 0x08, 0x8E);

    /* Syscall gate for task switching (int 0x80) */
    idt_set_entry(0x80, (uint32_t)syscall_handler_idt, 0x08, 0x8E);

    load_idt();
}