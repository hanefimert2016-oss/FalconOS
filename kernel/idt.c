/* =============================================================================
 *  FalconOS — 256-entry IDT installation + ISR/IRQ dispatch
 * -----------------------------------------------------------------------------
 *  ISR and IRQ stubs (boot/isr.asm) push (vec, err) and call back into here
 *  via isr_handler / irq_handler.  Hardware IRQs fan out to the device
 *  drivers (PIT, keyboard, mouse) and signal end-of-interrupt to the PIC.
 * ============================================================================= */
#include "falcon.h"

typedef struct __attribute__((packed)) {
    u16 base_lo;
    u16 sel;
    u8  zero;
    u8  flags;
    u16 base_hi;
} idt_entry_t;

typedef struct __attribute__((packed)) {
    u16 limit;
    u32 base;
} idt_ptr_t;

extern void *isr_table[32];
extern void *irq_table[16];

static idt_entry_t IDT[256];
static idt_ptr_t   IDTR;

static void idt_set(u8 n, u32 base)
{
    IDT[n].base_lo = base & 0xFFFF;
    IDT[n].base_hi = (base >> 16) & 0xFFFF;
    IDT[n].sel     = 0x08;
    IDT[n].zero    = 0;
    IDT[n].flags   = 0x8E;       /* present, ring 0, 32-bit interrupt gate */
}

void idt_install(void)
{
    k_memset(IDT, 0, sizeof(IDT));
    for (i32 i = 0; i < 32; i++) idt_set((u8)i,        (u32)isr_table[i]);
    for (i32 i = 0; i < 16; i++) idt_set((u8)(0x20+i), (u32)irq_table[i]);

    IDTR.limit = sizeof(IDT) - 1;
    IDTR.base  = (u32)&IDT;
    __asm__ volatile ("lidt (%0)" :: "r"(&IDTR));
}

/* ---- C-side interrupt frame --------------------------------------------- */
typedef struct __attribute__((packed)) {
    u32 ds;
    u32 edi, esi, ebp, esp_unused, ebx, edx, ecx, eax;
    u32 vec, err;
    u32 eip, cs, eflags;
} regs_t;

extern void pit_irq(void);
extern void kbd_irq(void);
extern void mouse_irq(void);
extern void pic_eoi(u32 vec);
extern void log_push_dev(const char *s);

static const char *EXC_NAMES[32] = {
    "DivByZero","Debug","NMI","Breakpoint","Overflow","BoundRange","InvalidOpcode","DeviceNA",
    "DoubleFault","CoprocSeg","InvalidTSS","SegNotPresent","StackFault","GenProtFault","PageFault","Reserved",
    "FPU","AlignCheck","MachineCheck","SIMDFloat","Virt","Reserved","Reserved","Reserved",
    "Reserved","Reserved","Reserved","Reserved","Reserved","Reserved","Security","Reserved"
};

void isr_handler(regs_t *r)
{
    /* Render a panic banner then halt.  We set a shared flag for the next
     * dispatcher tick — but if interrupts are already off we just freeze. */
    extern volatile bool g_panic;
    extern char          g_panic_msg[80];
    g_panic = true;
    char *m = g_panic_msg;
    const char *prefix = "PANIC: ";
    while (*prefix) *m++ = *prefix++;
    if (r->vec < 32) {
        const char *n = EXC_NAMES[r->vec];
        while (*n) *m++ = *n++;
    }
    *m = 0;
    for (;;) __asm__ volatile ("cli; hlt");
}

void irq_handler(regs_t *r)
{
    switch (r->vec) {
        case 0x20: pit_irq();   break;
        case 0x21: kbd_irq();   break;
        case 0x2C: mouse_irq(); break;
        default: break;
    }
    pic_eoi(r->vec);
}
