/* =============================================================================
 *  FalconOS — Programmable Interval Timer (channel 0, mode 3, 100 Hz)
 * -----------------------------------------------------------------------------
 *  Drives all real-time clocks in the system: g_ticks counts 10-ms intervals
 *  since boot, pit_ms() returns wall clock in milliseconds.  Animations,
 *  the status-bar uptime clock, and the frame pacer all read these.
 * ============================================================================= */
#include "falcon.h"

#define PIT_CMD  0x43
#define PIT_DAT0 0x40
#define PIT_FREQ 1193182u
#define PIT_HZ   100u
#define PIT_DIV  (PIT_FREQ / PIT_HZ)

volatile u32 g_ticks = 0;

void pit_init(void)
{
    outb(PIT_CMD, 0x36);                         /* ch0, lo+hi, mode 3 */
    outb(PIT_DAT0,  PIT_DIV       & 0xFF);
    outb(PIT_DAT0, (PIT_DIV >> 8) & 0xFF);
}

void pit_irq(void) { g_ticks++; }

u32 pit_ms(void)   { return g_ticks * (1000u / PIT_HZ); }

/* spin until at least `ms` milliseconds have elapsed; halt between IRQs */
void pit_sleep(u32 ms)
{
    u32 target = g_ticks + (ms / 10) + 1;
    while (g_ticks < target) __asm__ volatile ("hlt");
}

/* uptime decomposition for the status bar */
void pit_uptime(u32 *h, u32 *m, u32 *s)
{
    u32 sec = pit_ms() / 1000;
    *h = sec / 3600;
    *m = (sec / 60) % 60;
    *s = sec % 60;
}
