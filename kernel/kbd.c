/* =============================================================================
 *  FalconOS — PS/2 keyboard (polled, no IRQs)
 * -----------------------------------------------------------------------------
 *  We avoid the IDT/PIC entirely.  On every frame the dispatcher polls 0x60
 *  and translates scan-code set 1 release/press codes into FalconOS keycodes.
 * ============================================================================= */
#include "falcon.h"

#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_OUT    0x01

/* US scan-code set 1, lower half — extended (0xE0) keys handled below */
static const i32 SCAN1[128] = {
    [0x01] = KEY_ESC,
    [0x0F] = KEY_TAB,
    [0x1C] = KEY_ENTER,
    [0x39] = ' ',
    [0x3B] = KEY_F1,
    [0x3C] = KEY_F2,

    /* digits */
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',

    /* letters (upper-row first) */
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = 'm',
};

i32 kbd_poll(void)
{
    static bool extended = false;

    if (!(inb(PS2_STATUS) & PS2_OUT)) return -1;
    u8 sc = inb(PS2_DATA);

    if (sc == 0xE0) { extended = true; return -1; }
    if (sc & 0x80)  { extended = false; return -1; }       /* key release */

    if (extended) {
        extended = false;
        switch (sc) {
            case 0x48: return KEY_UP;
            case 0x50: return KEY_DOWN;
            case 0x4B: return KEY_LEFT;
            case 0x4D: return KEY_RIGHT;
        }
        return -1;
    }

    i32 k = SCAN1[sc & 0x7F];
    return k ? k : -1;
}
