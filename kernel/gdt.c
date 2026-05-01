/* =============================================================================
 *  FalconOS — minimal flat 32-bit GDT
 * -----------------------------------------------------------------------------
 *  Multiboot2 hands us a usable but undefined GDT.  We replace it with a
 *  conventional 5-entry layout so IDT entries can reference 0x08 (kernel
 *  code) and the data segment selector 0x10 has stable semantics.
 * ============================================================================= */
#include "falcon.h"

typedef struct __attribute__((packed)) {
    u16 lim_lo;
    u16 base_lo;
    u8  base_mid;
    u8  access;
    u8  gran;
    u8  base_hi;
} gdt_entry_t;

typedef struct __attribute__((packed)) {
    u16 limit;
    u32 base;
} gdt_ptr_t;

static gdt_entry_t GDT[5];
static gdt_ptr_t   GDTR;

static void gdt_set(i32 n, u32 base, u32 limit, u8 access, u8 gran)
{
    GDT[n].base_lo  = base & 0xFFFF;
    GDT[n].base_mid = (base >> 16) & 0xFF;
    GDT[n].base_hi  = (base >> 24) & 0xFF;
    GDT[n].lim_lo   = limit & 0xFFFF;
    GDT[n].gran     = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    GDT[n].access   = access;
}

void gdt_install(void)
{
    GDTR.limit = sizeof(GDT) - 1;
    GDTR.base  = (u32)&GDT;

    gdt_set(0, 0, 0,           0x00, 0x00);    /* null                */
    gdt_set(1, 0, 0xFFFFFFFF,  0x9A, 0xCF);    /* kernel code 0x08    */
    gdt_set(2, 0, 0xFFFFFFFF,  0x92, 0xCF);    /* kernel data 0x10    */
    gdt_set(3, 0, 0xFFFFFFFF,  0xFA, 0xCF);    /* user code   0x18    */
    gdt_set(4, 0, 0xFFFFFFFF,  0xF2, 0xCF);    /* user data   0x20    */

    __asm__ volatile (
        "lgdt (%0)\n\t"
        "mov  $0x10, %%ax\n\t"
        "mov  %%ax, %%ds\n\t"
        "mov  %%ax, %%es\n\t"
        "mov  %%ax, %%fs\n\t"
        "mov  %%ax, %%gs\n\t"
        "mov  %%ax, %%ss\n\t"
        "ljmp $0x08, $1f\n\t"
        "1:"
        :: "r"(&GDTR)
        : "ax"
    );
}
