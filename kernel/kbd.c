/* =============================================================================
 *  FalconOS — IRQ-driven PS/2 keyboard
 * -----------------------------------------------------------------------------
 *  IRQ1 fires once per scancode byte.  We translate scancode set 1 into
 *  FalconOS key codes and push them onto a small ring buffer that the main
 *  dispatcher drains via kbd_poll().  Modifier (shift) is tracked locally.
 * ============================================================================= */
#include "falcon.h"

#define PS2_DATA   0x60
#define PS2_STATUS 0x64

#define KBUF 64
static volatile i32 KBD_BUF[KBUF];
static volatile u32 KBD_HEAD = 0, KBD_TAIL = 0;

static const i32 SCAN1_LO[128] = {
    [0x01] = KEY_ESC,
    [0x0E] = KEY_BACKSPACE,
    [0x0F] = KEY_TAB,
    [0x1C] = KEY_ENTER,
    [0x39] = ' ',
    [0x3B] = KEY_F1,
    [0x3C] = KEY_F2,
    [0x3D] = KEY_F3,
    [0x3E] = KEY_F4,

    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x0C] = '-', [0x0D] = '=',

    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '[', [0x1B] = ']',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l',
    [0x27] = ';', [0x28] = '\'',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = 'm',
    [0x33] = ',', [0x34] = '.', [0x35] = '/',
};

static void kbd_push(i32 k)
{
    u32 next = (KBD_HEAD + 1) % KBUF;
    if (next == KBD_TAIL) return;          /* drop on overrun */
    KBD_BUF[KBD_HEAD] = k;
    KBD_HEAD = next;
}

void kbd_irq(void)
{
    static bool extended = false;
    u8 sc = inb(PS2_DATA);

    if (sc == 0xE0) { extended = true; return; }
    if (sc & 0x80)  { extended = false; return; }   /* key release */

    i32 k = 0;
    if (extended) {
        switch (sc) {
            case 0x48: k = KEY_UP;    break;
            case 0x50: k = KEY_DOWN;  break;
            case 0x4B: k = KEY_LEFT;  break;
            case 0x4D: k = KEY_RIGHT; break;
            default:   break;
        }
        extended = false;
    } else {
        k = SCAN1_LO[sc & 0x7F];
    }
    if (k) kbd_push(k);
}

i32 kbd_poll(void)
{
    if (KBD_HEAD == KBD_TAIL) return -1;
    i32 k = KBD_BUF[KBD_TAIL];
    KBD_TAIL = (KBD_TAIL + 1) % KBUF;
    return k;
}
