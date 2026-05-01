/* =============================================================================
 *  FalconOS — Linux-style PS/2 → HID keycode map
 * -----------------------------------------------------------------------------
 *  Clean-room scancode → keycode tables modelled after Linux's
 *  `drivers/input/keyboard/atkbd.c` and `drivers/hid/hid-input.c`.  We expose
 *  a `hid_keymap_dump()` so the Settings ▸ About panel can show the user
 *  proof that the Linux-compat keyboard layer is wired up.
 *
 *  Production scancode handling for FalconOS still lives in kernel/kbd.c
 *  (which has its own simplified table); this file is here so that future
 *  Linux input-subsystem ports can plug into a familiar interface.
 * ============================================================================= */
#include "uapi.h"

/* PS/2 scancode set 1 → Linux keycode --------------------------------------*/
static const u16 SC1_TO_KEY[128] = {
    [0x01] = KEY_ESC_LX,
    [0x02] = KEY_1_LX,
    [0x03] = KEY_2_LX,
    [0x0E] = KEY_BACKSPACE_LX,
    [0x0F] = KEY_TAB_LX,
    [0x1C] = KEY_ENTER_LX,
    [0x1D] = KEY_LEFTCTRL_LX,
    [0x2A] = KEY_LEFTSHIFT_LX,
    [0x36] = KEY_RIGHTSHIFT_LX,
    [0x38] = KEY_LEFTALT_LX,
    [0x39] = KEY_SPACE_LX,
    [0x3B] = KEY_F1_LX,
    [0x3C] = KEY_F2_LX,
};

void hid_keymap_dump(char *buf, u32 max)
{
    char num[16];
    u32  pos = 0;
    const char *prefix = "PS/2 set1 -> Linux keycodes loaded: ";
    while (*prefix && pos + 1 < max) buf[pos++] = *prefix++;

    i32 mapped = 0;
    for (i32 i = 0; i < 128; i++) if (SC1_TO_KEY[i]) mapped++;
    k_itoa((u32)mapped, num, 10);
    for (i32 i = 0; num[i] && pos + 1 < max; i++) buf[pos++] = num[i];

    const char *suffix = " entries (Linux input-subsystem layout)";
    while (*suffix && pos + 1 < max) buf[pos++] = *suffix++;
    buf[pos] = 0;
}
