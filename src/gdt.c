#include "gdt.h"
#include "console.h"
#include <stddef.h>

/* GDT with 6 entries: null, kernel code, kernel data, user code, user data, TSS */
static gdt_entry_t gdt[6];
static gdt_ptr_t gdt_ptr;
static tss_entry_t tss;

extern void gdt_flush(uint32_t gdt_ptr_addr);
extern void tss_flush(void);

static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = (limit >> 16) & 0x0F;
    gdt[num].granularity |= gran & 0xF0;
    gdt[num].access = access;
}

void gdt_init(void) {
    gdt_ptr.limit = (sizeof(gdt_entry_t) * 6) - 1;
    gdt_ptr.base = (uint32_t)&gdt;
    
    gdt_set_gate(0, 0, 0, 0, 0);                /* Null segment */
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); /* Kernel code (ring 0) - 0x9A = P|DPL0|S|Type=11010 */
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); /* Kernel data (ring 0) - 0x92 = P|DPL0|S|Type=10010 */
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF); /* User code (ring 3)   - 0xFA = P|DPL3|S|Type=11010 */
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); /* User data (ring 3)   - 0xF2 = P|DPL3|S|Type=10010 */
    
    /* Initialize TSS */
    for (uint8_t *p = (uint8_t*)&tss; p < (uint8_t*)(&tss + 1); p++) *p = 0;
    
    tss.ss0 = SEL_KERNEL_DATA;
    tss.esp0 = 0;
    
    /* TSS segment - 0x89: P|DPL0|Type=01001 (Available 32-bit TSS) */
    uint32_t tss_base = (uint32_t)&tss;
    uint32_t tss_limit = sizeof(tss_entry_t) - 1;
    gdt_set_gate(5, tss_base, tss_limit, 0x89, 0x00);
    
    gdt_flush((uint32_t)&gdt_ptr);
    tss_flush();
}

void tss_set_kernel_stack(uint32_t esp0) {
    tss.esp0 = esp0;
}