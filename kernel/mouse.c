/* =============================================================================
 *  FalconOS — PS/2 mouse driver (IRQ12)
 * -----------------------------------------------------------------------------
 *  Sets up the auxiliary device on the PS/2 controller, enables IRQ12 and
 *  reassembles 3-byte movement packets.  Tracks cursor position, left
 *  button state, and a rising-edge "click" flag the UI layer consumes
 *  with mouse_consume_click().
 * ============================================================================= */
#include "falcon.h"

#define PS2_DATA  0x60
#define PS2_STAT  0x64

static volatile i32  m_x  = 480;
static volatile i32  m_y  = 360;
static volatile bool m_l  = false;
static volatile bool m_l_edge = false;
static volatile bool m_r  = false;
static volatile bool m_r_edge = false;

static u8 pkt[3];
static i32 cyc = 0;

/* User feedback: mouse felt slow + Y axis inverted. The PS/2 spec sends
 * dy positive = "away from user" (== up on screen) so most kernels do
 *   m_y -= dy
 * but QEMU's input pipeline already inverts Y for the SDL backend, so
 * the screen ends up moving the wrong way. We do the natural mapping
 *   m_y += dy
 * which matches the user's pointer motion 1:1. The 2x multiplier turns
 * a 1-count-per-mickey hardware default into a comfortable cursor speed
 * at FullHD/QHD framebuffers without hiding small motions. */
#define MOUSE_SENSITIVITY  2

static void wait_send(void)  { for (i32 i = 0; i < 100000; i++) if (!(inb(PS2_STAT) & 2)) return; }
static void wait_recv(void)  { for (i32 i = 0; i < 100000; i++) if   (inb(PS2_STAT) & 1)  return; }

static void mw(u8 v)
{
    wait_send(); outb(PS2_STAT, 0xD4);
    wait_send(); outb(PS2_DATA, v);
}

static u8 mr(void)
{
    wait_recv(); return inb(PS2_DATA);
}

void mouse_init(void)
{
    /* enable aux device */
    wait_send(); outb(PS2_STAT, 0xA8);

    /* enable IRQ12 in the controller config byte */
    wait_send(); outb(PS2_STAT, 0x20);
    u8 cfg = mr();
    cfg |= (1 << 1);     /* enable IRQ12 */
    cfg &= ~(1 << 5);    /* enable mouse clock */
    wait_send(); outb(PS2_STAT, 0x60);
    wait_send(); outb(PS2_DATA, cfg);

    mw(0xF6); mr();      /* set defaults */
    mw(0xF4); mr();      /* enable streaming */
}

void mouse_irq(void)
{
    u8 status = inb(PS2_STAT);
    if (!(status & 1) || !(status & 0x20)) return;    /* not mouse data */
    u8 b = inb(PS2_DATA);

    if (cyc == 0 && !(b & 0x08)) return;              /* desync — drop */
    pkt[cyc++] = b;
    if (cyc < 3) return;
    cyc = 0;

    /* Drop packets the controller marks as overflow rather than letting
     * a stale 9th bit cause the cursor to jump across the screen.       */
    if (pkt[0] & 0xC0) return;

    /* Proper 9-bit sign extension: byte 0 carries the sign bits for the
     * deltas, and bytes 1/2 carry the lower 8 bits. The raw cast to i8
     * worked for slow motions but flipped sign on fast ones (>127).    */
    i32 dx = (i32)pkt[1] - ((pkt[0] & 0x10) ? 0x100 : 0);
    i32 dy = (i32)pkt[2] - ((pkt[0] & 0x20) ? 0x100 : 0);

    bool ln = (pkt[0] & 1) != 0;
    bool rn = (pkt[0] & 2) != 0;
    if (ln && !m_l) m_l_edge = true;
    if (rn && !m_r) m_r_edge = true;
    m_l = ln;
    m_r = rn;

    m_x += dx * MOUSE_SENSITIVITY;
    m_y += dy * MOUSE_SENSITIVITY;
    if (m_x < 0) m_x = 0;
    if (m_y < 0) m_y = 0;
    if ((u32)m_x >= FB.width)  m_x = (i32)FB.width  - 1;
    if ((u32)m_y >= FB.height) m_y = (i32)FB.height - 1;
}

void mouse_get(i32 *x, i32 *y, bool *left)
{
    *x = m_x; *y = m_y; *left = m_l;
}

bool mouse_consume_click(void)
{
    bool e = m_l_edge;
    m_l_edge = false;
    return e;
}

bool mouse_consume_right(void)
{
    bool e = m_r_edge;
    m_r_edge = false;
    return e;
}
