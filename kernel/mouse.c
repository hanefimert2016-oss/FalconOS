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
static volatile bool m_l_double_edge = false;
static volatile u32  m_last_click_ms = 0;

/* Driver telemetry — surfaced in Settings ▸ Suruculer alongside the
 * keyboard / disk counters so we have one glance-able health view.    */
static volatile u32  m_pkt_count   = 0;
static volatile u32  m_pkt_dropped = 0;
static volatile u32  m_clicks      = 0;

static u8 pkt[3];
static i32 cyc = 0;

/* PS/2 sign convention: dy positive = "mouse moved up" (away from user).
 * Screen Y grows downward. The natural mapping is therefore
 *     m_y -= dy
 * dx is straightforward: dx positive = "mouse moved right".
 *
 * Pointer scaling: the previous build had a quadratic acceleration
 * curve that felt nice on a 2K display BUT made the cursor jump in
 * unpredictable bursts whenever the IRQ rate dipped (a single missed
 * frame at 60 Hz would suddenly spit out a delta of 12, which the curve
 * scaled to 12×6 = 72 pixels — the 'bozuk' lurch users complained
 * about).  We replace it with a simple linear gain so motion stays
 * predictable, then add a tiny boost for genuinely large flicks so a
 * wrist sweep can still cross a 2K screen in one go.                */
static i32 mouse_scale(i32 d)
{
    i32 a = d < 0 ? -d : d;
    /* 1.5x baseline (round-half-up) — makes 1×1 movement reach the
     * far corner of a 2K display in ~600 mickeys, which feels like a
     * normal desk swipe.  Single-count moves still progress 1 px so
     * fine pointing on tiny targets keeps working.                   */
    i32 out = (d * 3 + (d > 0 ? 1 : -1)) / 2;
    /* small extra punch on flicks (>= 6 counts in one packet)         */
    if (a >= 6) out += d;
    return out;
}

#define DOUBLE_CLICK_MS  350

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

    if (cyc == 0 && !(b & 0x08)) {                     /* desync — drop */
        m_pkt_dropped++;
        return;
    }
    pkt[cyc++] = b;
    if (cyc < 3) return;
    cyc = 0;

    /* Drop packets the controller marks as overflow rather than letting
     * a stale 9th bit cause the cursor to jump across the screen.       */
    if (pkt[0] & 0xC0) { m_pkt_dropped++; return; }
    m_pkt_count++;

    /* Proper 9-bit sign extension: byte 0 carries the sign bits for the
     * deltas, and bytes 1/2 carry the lower 8 bits. The raw cast to i8
     * worked for slow motions but flipped sign on fast ones (>127).    */
    i32 dx = (i32)pkt[1] - ((pkt[0] & 0x10) ? 0x100 : 0);
    i32 dy = (i32)pkt[2] - ((pkt[0] & 0x20) ? 0x100 : 0);

    bool ln = (pkt[0] & 1) != 0;
    bool rn = (pkt[0] & 2) != 0;
    if (ln && !m_l) {
        m_l_edge = true;
        m_clicks++;
        u32 now = pit_ms();
        if (now - m_last_click_ms < DOUBLE_CLICK_MS) m_l_double_edge = true;
        m_last_click_ms = now;
    }
    if (rn && !m_r) m_r_edge = true;
    m_l = ln;
    m_r = rn;

    m_x += mouse_scale(dx);
    m_y -= mouse_scale(dy);     /* PS/2 dy positive = up; flip for screen */
    if (m_x < 0) m_x = 0;
    if (m_y < 0) m_y = 0;
    if ((u32)m_x >= FB.width)  m_x = (i32)FB.width  - 1;
    if ((u32)m_y >= FB.height) m_y = (i32)FB.height - 1;
}

bool mouse_consume_double(void)
{
    bool e = m_l_double_edge;
    m_l_double_edge = false;
    return e;
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

/* Peek at the click edge without consuming it (used by the menu-bar power
 * button so the click can fall through to the rest of the pipeline if the
 * user clicked elsewhere).                                                  */
bool mouse_peek_click(void)
{
    return m_l_edge;
}

/* Re-publish a click edge — used when an early consumer (e.g. the power
 * glyph hit-test) decides the click should still flow downstream.         */
void mouse_inject_click(void)
{
    m_l_edge = true;
}

/* Discard any pending click edges + scroll deltas so a modal transition
 * (installer→lockscreen→desktop, theme switch, sign-out) starts with no
 * stale clicks racing into the new screen.                            */
void mouse_drain(void)
{
    m_l_edge        = false;
    m_l_double_edge = false;
    m_r_edge        = false;
}

bool mouse_consume_right(void)
{
    bool e = m_r_edge;
    m_r_edge = false;
    return e;
}

/* Driver telemetry getters — settings.c renders these on the live
 * Suruculer health row.                                                */
void mouse_stats(u32 *seen, u32 *dropped, u32 *clicks)
{
    if (seen)    *seen    = m_pkt_count;
    if (dropped) *dropped = m_pkt_dropped;
    if (clicks)  *clicks  = m_clicks;
}
