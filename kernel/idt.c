/* =============================================================================
 *  FalconOS — 256-entry IDT installation + ISR/IRQ dispatch  (v5: x86_64)
 * -----------------------------------------------------------------------------
 *  ISR and IRQ stubs (boot/isr.asm) push (vec, err) and call back into here
 *  via isr_handler / irq_handler.  Hardware IRQs fan out to the device
 *  drivers (PIT, keyboard, mouse) and signal end-of-interrupt to the PIC.
 *
 *  IDT layout in long mode is 16 bytes per entry:
 *      offset_lo  16
 *      selector   16
 *      ist         8     (we use stack 0)
 *      type_attr   8     (0x8E = present, ring 0, 64-bit interrupt gate)
 *      offset_mid 16
 *      offset_hi  32
 *      reserved   32
 * ============================================================================= */
#include "falcon.h"

typedef struct __attribute__((packed)) {
    u16 base_lo;
    u16 sel;
    u8  ist;
    u8  flags;
    u16 base_mid;
    u32 base_hi;
    u32 reserved;
} idt_entry_t;

typedef struct __attribute__((packed)) {
    u16 limit;
    u64 base;
} idt_ptr_t;

extern void *isr_table[32];
extern void *irq_table[16];

static idt_entry_t IDT[256];
static idt_ptr_t   IDTR;

static void idt_set(u8 n, u64 base)
{
    IDT[n].base_lo  =  base        & 0xFFFF;
    IDT[n].base_mid = (base >> 16) & 0xFFFF;
    IDT[n].base_hi  = (base >> 32) & 0xFFFFFFFFu;
    IDT[n].sel      = 0x08;
    IDT[n].ist      = 0;
    IDT[n].flags    = 0x8E;       /* present, ring 0, 64-bit interrupt gate */
    IDT[n].reserved = 0;
}

void idt_install(void)
{
    k_memset(IDT, 0, sizeof(IDT));
    for (i32 i = 0; i < 32; i++) idt_set((u8)i,        (u64)isr_table[i]);
    for (i32 i = 0; i < 16; i++) idt_set((u8)(0x20+i), (u64)irq_table[i]);
#ifdef FALCON_RING3_TEST
    extern void ring3_syscall_int80(void);
    idt_set(0x80u,(u64)(uintptr_t)&ring3_syscall_int80);
    IDT[0x80].flags=0xEEu; /* P=1, DPL=3, interrupt gate */
#endif

    IDTR.limit = sizeof(IDT) - 1;
    IDTR.base  = (u64)&IDT;
    __asm__ volatile ("lidt (%0)" :: "r"(&IDTR));
}

/* ---- C-side interrupt frame --------------------------------------------- */
/* Layout matches PUSHA64 + (vec, err) + IRETQ frame from boot/isr.asm.    */
typedef struct __attribute__((packed)) {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rdi, rsi, rbp, rbx, rdx, rcx, rax;
    u64 vec, err;
    u64 rip, cs, rflags, rsp, ss;
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

#ifdef FALCON_QEMU_TLS_TEST
static void isr_debug_hex(u64 v,u32 digits){
    static const char chars[]="0123456789ABCDEF";
    for(u32 n=digits;n>0;n--)
        outb(0xE9,(u8)chars[(v>>(4u*(n-1)))&15u]);
}
#endif
void isr_handler(regs_t *r)
{
#ifdef FALCON_QEMU_TLS_TEST
    /* Test-only crash record: vector / error / faulting instruction pointer.
     * Handle #PF/#GP without attempting unsafe printf or heap operations. */
    outb(0xE9,'@');
    isr_debug_hex(r->vec,2u);
    outb(0xE9,':');
    isr_debug_hex(r->err,16u);
    outb(0xE9,':');
    isr_debug_hex(r->rip,16u);
    outb(0xE9,':');
    u64 cr2;
    __asm__ volatile("mov %%cr2,%0":"=r"(cr2));
    isr_debug_hex(cr2,16u);
    outb(0xE9,';');
#endif
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
    pic_eoi((u32)r->vec);
}
