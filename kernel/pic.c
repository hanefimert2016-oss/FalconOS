/* =============================================================================
 *  FalconOS — 8259 PIC remap & IRQ mask helpers
 * -----------------------------------------------------------------------------
 *  Master IRQs are mapped to vectors 0x20..0x27 and slave IRQs to 0x28..0x2F.
 *  This avoids the conflict with CPU exception vectors (0x00..0x1F).
 * ============================================================================= */
#include "falcon.h"

#define PIC1_CMD 0x20
#define PIC1_DAT 0x21
#define PIC2_CMD 0xA0
#define PIC2_DAT 0xA1

static void io_wait(void) { outb(0x80, 0); }

void pic_remap(void)
{
    u8 m1 = inb(PIC1_DAT), m2 = inb(PIC2_DAT);

    outb(PIC1_CMD, 0x11); io_wait();   /* ICW1: init + ICW4 */
    outb(PIC2_CMD, 0x11); io_wait();
    outb(PIC1_DAT, 0x20); io_wait();   /* ICW2: master vec base */
    outb(PIC2_DAT, 0x28); io_wait();   /* ICW2: slave  vec base */
    outb(PIC1_DAT, 0x04); io_wait();   /* ICW3: slave at IRQ2 */
    outb(PIC2_DAT, 0x02); io_wait();   /* ICW3: cascade ID */
    outb(PIC1_DAT, 0x01); io_wait();   /* ICW4: 8086 mode */
    outb(PIC2_DAT, 0x01); io_wait();

    outb(PIC1_DAT, m1);                 /* mask all back to original */
    outb(PIC2_DAT, m2);
}

void pic_unmask(u8 irq)
{
    u16 port = (irq < 8) ? PIC1_DAT : PIC2_DAT;
    if (irq >= 8) irq -= 8;
    u8 m = inb(port) & ~(1u << irq);
    outb(port, m);
}

void pic_eoi(u32 vec)
{
    if (vec >= 0x28) outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
}
