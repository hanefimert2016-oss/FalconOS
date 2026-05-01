/* =============================================================================
 *  FalconOS — IRQ-driven PS/2 keyboard with switchable layout (v5+)
 * -----------------------------------------------------------------------------
 *  IRQ1 fires once per scancode byte.  We translate scancode set 1 into
 *  ASCII keys via a layout table chosen by SET.kbd_layout (TR-Q / TR-F / US).
 *  Modifier (shift) is tracked locally.  Three logical layouts are provided:
 *
 *      KBD_TR_Q   — Türkçe Q (default in Turkey, ISO 9995-2 derivative)
 *      KBD_TR_F   — Türkçe F (typewriter heritage; popular with touch-typists)
 *      KBD_US     — US-QWERTY (standard ANSI 101)
 *
 *  Turkish-specific letters (ç ğ ı ö ş ü) are mapped to safe ASCII fallbacks
 *  ('c', 'g', 'i', 'o', 's', 'u') because the rest of the kernel uses an
 *  ASCII-only 8x16 font; users still see the layout name they picked in the
 *  installer reflected in Settings ▸ Keyboard.
 * ============================================================================= */
#include "falcon.h"

#define PS2_DATA   0x60
#define PS2_STATUS 0x64

#define KBUF 64
static volatile i32 KBD_BUF[KBUF];
static volatile u32 KBD_HEAD = 0, KBD_TAIL = 0;

/* Modifier state — tracked across IRQs.  CapsLock toggles on press
 * (not on release) like a real PS/2 controller; Ctrl/Alt are level-
 * tracked. We expose these to the kernel via kbd_mod_state() so apps
 * can implement Ctrl-C / Ctrl-V style shortcuts later.                 */
static volatile bool g_shift = false;
static volatile bool g_ctrl  = false;
static volatile bool g_alt   = false;
static volatile bool g_caps  = false;

u32 kbd_mod_state(void)
{
    u32 m = 0;
    if (g_shift) m |= 1u << 0;
    if (g_ctrl)  m |= 1u << 1;
    if (g_alt)   m |= 1u << 2;
    if (g_caps)  m |= 1u << 3;
    return m;
}

/* ---- US-QWERTY tables (lower / upper) -----------------------------------*/
static const i32 SC_US_LO[128] = {
    [0x01] = KEY_ESC,
    [0x0E] = KEY_BACKSPACE,
    [0x0F] = KEY_TAB,
    [0x1C] = KEY_ENTER,
    [0x39] = ' ',
    [0x3B] = KEY_F1,  [0x3C] = KEY_F2,  [0x3D] = KEY_F3,  [0x3E] = KEY_F4,

    [0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',
    [0x07]='6',[0x08]='7',[0x09]='8',[0x0A]='9',[0x0B]='0',
    [0x0C]='-',[0x0D]='=',

    [0x10]='q',[0x11]='w',[0x12]='e',[0x13]='r',[0x14]='t',
    [0x15]='y',[0x16]='u',[0x17]='i',[0x18]='o',[0x19]='p',
    [0x1A]='[',[0x1B]=']',
    [0x1E]='a',[0x1F]='s',[0x20]='d',[0x21]='f',[0x22]='g',
    [0x23]='h',[0x24]='j',[0x25]='k',[0x26]='l',
    [0x27]=';',[0x28]='\'',
    [0x2C]='z',[0x2D]='x',[0x2E]='c',[0x2F]='v',[0x30]='b',
    [0x31]='n',[0x32]='m',
    [0x33]=',',[0x34]='.',[0x35]='/',
};

static const i32 SC_US_HI[128] = {
    [0x02]='!',[0x03]='@',[0x04]='#',[0x05]='$',[0x06]='%',
    [0x07]='^',[0x08]='&',[0x09]='*',[0x0A]='(',[0x0B]=')',
    [0x0C]='_',[0x0D]='+',
    [0x10]='Q',[0x11]='W',[0x12]='E',[0x13]='R',[0x14]='T',
    [0x15]='Y',[0x16]='U',[0x17]='I',[0x18]='O',[0x19]='P',
    [0x1A]='{',[0x1B]='}',
    [0x1E]='A',[0x1F]='S',[0x20]='D',[0x21]='F',[0x22]='G',
    [0x23]='H',[0x24]='J',[0x25]='K',[0x26]='L',
    [0x27]=':',[0x28]='"',
    [0x2C]='Z',[0x2D]='X',[0x2E]='C',[0x2F]='V',[0x30]='B',
    [0x31]='N',[0x32]='M',
    [0x33]='<',[0x34]='>',[0x35]='?',
};

/* ---- TR-Q (Türkçe Q) — same alphabetic positions, Turkish punct mapped to
 *  ASCII fallbacks (kernel font is ASCII).                                  */
static const i32 SC_TRQ_LO[128] = {
    [0x01] = KEY_ESC,
    [0x0E] = KEY_BACKSPACE,
    [0x0F] = KEY_TAB,
    [0x1C] = KEY_ENTER,
    [0x39] = ' ',
    [0x3B] = KEY_F1,  [0x3C] = KEY_F2,  [0x3D] = KEY_F3,  [0x3E] = KEY_F4,

    [0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',
    [0x07]='6',[0x08]='7',[0x09]='8',[0x0A]='9',[0x0B]='0',
    [0x0C]='*',[0x0D]='-',

    [0x10]='q',[0x11]='w',[0x12]='e',[0x13]='r',[0x14]='t',
    [0x15]='y',[0x16]='u',[0x17]='i',[0x18]='o',[0x19]='p',
    [0x1A]='g',[0x1B]='u',                          /* ğ ü → fallback     */
    [0x1E]='a',[0x1F]='s',[0x20]='d',[0x21]='f',[0x22]='g',
    [0x23]='h',[0x24]='j',[0x25]='k',[0x26]='l',
    [0x27]='s',[0x28]='i',                          /* ş i (dotless)      */
    [0x2C]='z',[0x2D]='x',[0x2E]='c',[0x2F]='v',[0x30]='b',
    [0x31]='n',[0x32]='m',
    [0x33]='o',[0x34]='c',[0x35]='.',               /* ö ç .              */
};

/* ---- TR-F — different mechanical layout; we mostly mirror QWERTY shift  */
static const i32 SC_TRF_LO[128] = {
    [0x01] = KEY_ESC,
    [0x0E] = KEY_BACKSPACE,
    [0x0F] = KEY_TAB,
    [0x1C] = KEY_ENTER,
    [0x39] = ' ',
    [0x3B] = KEY_F1,  [0x3C] = KEY_F2,  [0x3D] = KEY_F3,  [0x3E] = KEY_F4,

    [0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',
    [0x07]='6',[0x08]='7',[0x09]='8',[0x0A]='9',[0x0B]='0',
    [0x0C]='/',[0x0D]='-',

    [0x10]='f',[0x11]='g',[0x12]='g',[0x13]='i',[0x14]='o',
    [0x15]='d',[0x16]='r',[0x17]='n',[0x18]='h',[0x19]='p',
    [0x1A]='q',[0x1B]='w',
    [0x1E]='u',[0x1F]='i',[0x20]='e',[0x21]='a',[0x22]='u',
    [0x23]='t',[0x24]='k',[0x25]='m',[0x26]='l',
    [0x27]='y',[0x28]='s',
    [0x2C]='j',[0x2D]='o',[0x2E]='v',[0x2F]='c',[0x30]='c',
    [0x31]='z',[0x32]='s',
    [0x33]='b',[0x34]='.',[0x35]=',',
};

i32 kbd_translate(u8 sc, kbd_layout_t layout, bool shift)
{
    if (sc >= 128) return 0;
    const i32 *tbl;
    if (layout == KBD_TR_Q)      tbl = SC_TRQ_LO;
    else if (layout == KBD_TR_F) tbl = SC_TRF_LO;
    else                         tbl = SC_US_LO;

    i32 k = tbl[sc];
    /* CapsLock acts on letter keys only — XOR with shift like real
     * keyboards (caps + shift = lowercase).                            */
    bool letter_upper = shift;
    if (k >= 'a' && k <= 'z' && g_caps) letter_upper = !letter_upper;
    if (letter_upper && k >= 'a' && k <= 'z') return k - 'a' + 'A';
    /* punctuation shift: only US has full mapping; reuse for others       */
    if (shift && SC_US_HI[sc] && (k < 0x100)) return SC_US_HI[sc];
    return k;
}

const char *kbd_layout_name(kbd_layout_t l)
{
    switch (l) {
        case KBD_TR_Q: return "TR-Q";
        case KBD_TR_F: return "TR-F";
        case KBD_US:   return "US-QWERTY";
        default:       return "?";
    }
}

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

    /* PS/2 packet sentinels we never want to treat as keys */
    if (sc == 0x00 || sc == 0xFA || sc == 0xFE || sc == 0xAA || sc == 0xEE) return;

    if (sc == 0xE0) { extended = true; return; }
    if (sc & 0x80)  {
        /* key release — track all level-triggered modifiers */
        u8 raw = sc & 0x7F;
        if (raw == 0x2A || raw == 0x36) g_shift = false;
        if (raw == 0x1D)                g_ctrl  = false;   /* Ctrl  (L/R) */
        if (raw == 0x38)                g_alt   = false;   /* Alt   (L/R) */
        extended = false;
        return;
    }
    /* Modifier presses — never enqueue, just update state */
    if (sc == 0x2A || sc == 0x36) { g_shift = true;  return; }
    if (sc == 0x1D)               { g_ctrl  = true;  return; }
    if (sc == 0x38)               { g_alt   = true;  return; }
    if (sc == 0x3A)               { g_caps  = !g_caps; return; }

    i32 k = 0;
    if (extended) {
        switch (sc) {
            case 0x48: k = KEY_UP;    break;
            case 0x50: k = KEY_DOWN;  break;
            case 0x4B: k = KEY_LEFT;  break;
            case 0x4D: k = KEY_RIGHT; break;
            case 0x47: k = KEY_HOME;  break;
            case 0x4F: k = KEY_END;   break;
            case 0x49: k = KEY_PGUP;  break;
            case 0x51: k = KEY_PGDN;  break;
            case 0x53: k = KEY_DEL;   break;
            default:   break;
        }
        extended = false;
    } else {
        k = kbd_translate(sc, SET.kbd_layout, g_shift);
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
