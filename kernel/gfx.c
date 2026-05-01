/* =============================================================================
 *  FalconOS — minimal software framebuffer renderer
 * -----------------------------------------------------------------------------
 *  Everything we need to compose a macOS-grade UI in ~200 lines:
 *    - alpha-blended pixel
 *    - vertical gradient fill (background)
 *    - solid + alpha rectangle
 *    - 2× super-sampled anti-aliased circle (filled & outline)
 *    - rounded rectangle (filled, alpha, outline) — 4 corner circles + 3 rects
 *    - bitmap text (8 × 16 font, printable ASCII)
 * ============================================================================= */
#include "falcon.h"

fb_t FB;

/* off-screen buffer — every gfx_* call writes here, then the dispatcher
 * blits it to the real framebuffer once per frame via gfx_present().
 * Sized at build time via -DFB_W / -DFB_H (Makefile RES variable):
 *   RES=hd   → 1280×800   (≈ 4 MiB BSS)
 *   RES=fhd  → 1920×1080  (≈ 8 MiB BSS)  ← default
 *   RES=2k   → 2560×1440  (≈ 14 MiB BSS) */
#ifndef FB_W
#define FB_W 1920
#endif
#ifndef FB_H
#define FB_H 1080
#endif

#define BACK_W   FB_W
#define BACK_H   FB_H
static u32 BACK[BACK_W * BACK_H];

u32 gfx_back_w(void) { return BACK_W; }
u32 gfx_back_h(void) { return BACK_H; }

void gfx_init(void *p, u32 w, u32 h, u32 pitch, u8 bpp)
{
    FB.pixels = (u32 *)p;
    FB.width  = w;
    FB.height = h;
    FB.pitch  = pitch;
    FB.bpp    = bpp;
}

/* copy back buffer → physical framebuffer */
void gfx_present(void)
{
    u32 rows = FB.height < BACK_H ? FB.height : BACK_H;
    u32 cols = FB.width  < BACK_W ? FB.width  : BACK_W;
    for (u32 y = 0; y < rows; y++) {
        u32 *src = &BACK[y * BACK_W];
        u32 *dst = (u32 *)((u8 *)FB.pixels + y * FB.pitch);
        for (u32 x = 0; x < cols; x++) dst[x] = src[x];
    }
}

/* ---- pixel ---------------------------------------------------------------- */
static inline u32 *px_at(i32 x, i32 y)
{
    return &BACK[y * BACK_W + x];
}

void gfx_pixel(i32 x, i32 y, u32 c)
{
    if ((u32)x >= BACK_W || (u32)y >= BACK_H) return;
    *px_at(x, y) = c;
}

/* alpha 0..255 (255 = opaque); blends c onto current pixel */
void gfx_pixel_a(i32 x, i32 y, u32 c, u8 a)
{
    if ((u32)x >= BACK_W || (u32)y >= BACK_H) return;
    if (a == 255) { *px_at(x, y) = c; return; }
    if (a == 0)   return;

    u32 dst = *px_at(x, y);
    u32 sr = (c   >> 16) & 0xFF, sg = (c   >> 8) & 0xFF, sb = c   & 0xFF;
    u32 dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
    u32 ia = 255 - a;
    u32 r = (sr * a + dr * ia) >> 8;
    u32 g = (sg * a + dg * ia) >> 8;
    u32 b = (sb * a + db * ia) >> 8;
    *px_at(x, y) = (r << 16) | (g << 8) | b;
}

/* ---- bulk fills ----------------------------------------------------------- */
void gfx_clear(u32 c)
{
    u32 n = BACK_W * BACK_H;
    for (u32 i = 0; i < n; i++) BACK[i] = c;
}

void gfx_gradient_v(u32 top, u32 bot)
{
    u32 tr = (top >> 16) & 0xFF, tg = (top >> 8) & 0xFF, tb = top & 0xFF;
    u32 br = (bot >> 16) & 0xFF, bg = (bot >> 8) & 0xFF, bb = bot & 0xFF;
    for (u32 y = 0; y < BACK_H; y++) {
        u32 t  = (y * 256) / BACK_H;
        u32 it = 256 - t;
        u32 r  = (tr * it + br * t) >> 8;
        u32 g  = (tg * it + bg * t) >> 8;
        u32 b  = (tb * it + bb * t) >> 8;
        u32 c  = (r << 16) | (g << 8) | b;
        u32 *row = &BACK[y * BACK_W];
        for (u32 x = 0; x < BACK_W; x++) row[x] = c;
    }
}

/* Big-Sur-style wallpaper: vertical gradient + soft radial accent in the
 * upper-left.  Theme-aware: pulls colours from the runtime palette. */
void gfx_wallpaper(void)
{
    u32 ctop = PAL_BG_TOP, cbot = PAL_BG_BOT, chnt = PAL_BG_HINT;
    u32 tr = (ctop >> 16) & 0xFF, tg = (ctop >> 8) & 0xFF, tb = ctop & 0xFF;
    u32 br = (cbot >> 16) & 0xFF, bg = (cbot >> 8) & 0xFF, bb = cbot & 0xFF;
    u32 hr = (chnt >> 16) & 0xFF, hg = (chnt >> 8) & 0xFF, hb = chnt & 0xFF;
    i32 ax = (i32)BACK_W / 4;
    i32 ay = (i32)BACK_H / 4;
    /* 1 / max(BACK_W, BACK_H) — radial fall-off normalisation */
    u32 norm = (BACK_W > BACK_H ? BACK_W : BACK_H);
    for (u32 y = 0; y < BACK_H; y++) {
        u32 t  = (y * 256) / BACK_H;
        u32 it = 256 - t;
        u32 vr = (tr * it + br * t) >> 8;
        u32 vg = (tg * it + bg * t) >> 8;
        u32 vb = (tb * it + bb * t) >> 8;
        u32 *row = &BACK[y * BACK_W];
        for (u32 x = 0; x < BACK_W; x++) {
            i32 dx = (i32)x - ax;
            i32 dy = (i32)y - ay;
            i32 d  = dx * dx + dy * dy;
            i32 dn = (i32)((u32)d / norm);
            i32 a  = 200 - dn;          /* peaks near (ax, ay) */
            if (a < 0) a = 0;
            u32 ca = (u32)a;
            u32 ia = 256 - ca;
            u32 r = (vr * ia + hr * ca) >> 8;
            u32 g = (vg * ia + hg * ca) >> 8;
            u32 b = (vb * ia + hb * ca) >> 8;
            row[x] = (r << 16) | (g << 8) | b;
        }
    }
}

void gfx_rect(i32 x, i32 y, i32 w, i32 h, u32 c)
{
    for (i32 j = 0; j < h; j++)
        for (i32 i = 0; i < w; i++) gfx_pixel(x + i, y + j, c);
}

void gfx_rect_a(i32 x, i32 y, i32 w, i32 h, u32 c, u8 a)
{
    for (i32 j = 0; j < h; j++)
        for (i32 i = 0; i < w; i++) gfx_pixel_a(x + i, y + j, c, a);
}

/* ---- anti-aliased circle (2× super-sample) -------------------------------- */
/*  Returns a coverage value in [0, 4] for the unit pixel (dx, dy) inside a
 *  circle of integer radius `r`.  Each pixel is sampled at four sub-pixel
 *  centres ±¼; we count how many fall inside.  Multiply by 64 to get an
 *  alpha 0..255 (4 → 255 with saturation in caller). */
static inline u8 _coverage(i32 dx, i32 dy, i32 r)
{
    i32 r2 = (r * 4) * (r * 4);
    i32 c  = 0;
    for (i32 sy = -1; sy <= 1; sy += 2)
        for (i32 sx = -1; sx <= 1; sx += 2) {
            i32 px = dx * 4 + sx;
            i32 py = dy * 4 + sy;
            if (px * px + py * py <= r2) c++;
        }
    return c == 4 ? 255 : (u8)(c * 64);
}

void gfx_circle(i32 cx, i32 cy, i32 r, u32 c)
{
    if (r < 1) { gfx_pixel(cx, cy, c); return; }
    for (i32 y = -r - 1; y <= r + 1; y++)
        for (i32 x = -r - 1; x <= r + 1; x++) {
            u8 a = _coverage(x, y, r);
            if (a) gfx_pixel_a(cx + x, cy + y, c, a);
        }
}

void gfx_circle_a(i32 cx, i32 cy, i32 r, u32 c, u8 alpha)
{
    if (r < 1) { gfx_pixel_a(cx, cy, c, alpha); return; }
    for (i32 y = -r - 1; y <= r + 1; y++)
        for (i32 x = -r - 1; x <= r + 1; x++) {
            u8 a = _coverage(x, y, r);
            if (!a) continue;
            u32 aa = ((u32)a * alpha) >> 8;
            gfx_pixel_a(cx + x, cy + y, c, (u8)aa);
        }
}

void gfx_circle_outline(i32 cx, i32 cy, i32 r, u32 c)
{
    if (r < 2) { gfx_circle(cx, cy, r, c); return; }
    for (i32 y = -r - 1; y <= r + 1; y++)
        for (i32 x = -r - 1; x <= r + 1; x++) {
            u8 a_out = _coverage(x, y, r);
            u8 a_in  = _coverage(x, y, r - 1);
            if (a_out > a_in) gfx_pixel_a(cx + x, cy + y, c, a_out - a_in);
        }
}

/* ---- rounded rectangles --------------------------------------------------- */
static void _round_corner(i32 cx, i32 cy, i32 r, u32 c, u8 alpha, i32 qx, i32 qy)
{
    /* draws the (qx, qy) quadrant ∈ {-1,+1}² of a filled AA circle */
    for (i32 y = 0; y <= r + 1; y++)
        for (i32 x = 0; x <= r + 1; x++) {
            u8 a = _coverage(x * qx, y * qy, r);
            if (!a) continue;
            u32 aa = ((u32)a * alpha) >> 8;
            gfx_pixel_a(cx + x * qx, cy + y * qy, c, (u8)aa);
        }
}

void gfx_round_rect_a(i32 x, i32 y, i32 w, i32 h, i32 r, u32 c, u8 a)
{
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;

    /* three rectangle bands */
    gfx_rect_a(x + r,       y,         w - 2 * r, r,             c, a);
    gfx_rect_a(x,           y + r,     w,         h - 2 * r,     c, a);
    gfx_rect_a(x + r,       y + h - r, w - 2 * r, r,             c, a);

    /* four AA corners */
    _round_corner(x + r        - 1, y + r        - 1, r, c, a, -1, -1);
    _round_corner(x + w - r,        y + r        - 1, r, c, a, +1, -1);
    _round_corner(x + r        - 1, y + h - r,        r, c, a, -1, +1);
    _round_corner(x + w - r,        y + h - r,        r, c, a, +1, +1);
}

void gfx_round_rect(i32 x, i32 y, i32 w, i32 h, i32 r, u32 c)
{
    gfx_round_rect_a(x, y, w, h, r, c, 255);
}

/* Frosted-glass rounded rect: soft drop shadow + translucent panel + 1 px
 * hairline border.  Theme-aware via the runtime palette so the same card
 * style works on Lumen (light) and Nox (dark).                            */
void gfx_round_glass(i32 x, i32 y, i32 w, i32 h, i32 r)
{
    /* drop shadow (soft) */
    gfx_round_rect_a(x + 2, y + 6, w, h, r, COL_SHADOW, 28);
    gfx_round_rect_a(x + 1, y + 3, w, h, r, COL_SHADOW, 18);
    /* glass body */
    gfx_round_rect_a(x, y, w, h, r, PAL_GLASS, 235);
    /* hairline border */
    gfx_round_outline(x, y, w, h, r, PAL_HAIRLINE);
}

void gfx_round_outline(i32 x, i32 y, i32 w, i32 h, i32 r, u32 c)
{
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;

    /* straight edges */
    gfx_rect(x + r,         y,             w - 2 * r, 1, c);
    gfx_rect(x + r,         y + h - 1,     w - 2 * r, 1, c);
    gfx_rect(x,             y + r,         1,         h - 2 * r, c);
    gfx_rect(x + w - 1,     y + r,         1,         h - 2 * r, c);

    /* corner arcs */
    for (i32 y0 = -r - 1; y0 <= 0; y0++)
        for (i32 x0 = -r - 1; x0 <= 0; x0++) {
            u8 ao = _coverage(x0, y0, r);
            u8 ai = _coverage(x0, y0, r - 1);
            if (ao > ai) {
                u8 a = ao - ai;
                gfx_pixel_a(x + r + x0,         y + r + y0,         c, a);
                gfx_pixel_a(x + w - r - 1 - x0, y + r + y0,         c, a);
                gfx_pixel_a(x + r + x0,         y + h - r - 1 - y0, c, a);
                gfx_pixel_a(x + w - r - 1 - x0, y + h - r - 1 - y0, c, a);
            }
        }
}

/* ---- line (Bresenham) ----------------------------------------------------- */
void gfx_line(i32 x0, i32 y0, i32 x1, i32 y1, u32 c)
{
    i32 dx =  (x1 > x0 ? x1 - x0 : x0 - x1);
    i32 sx = x0 < x1 ? 1 : -1;
    i32 dy = -(y1 > y0 ? y1 - y0 : y0 - y1);
    i32 sy = y0 < y1 ? 1 : -1;
    i32 err = dx + dy;
    for (;;) {
        gfx_pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        i32 e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* ---- text ----------------------------------------------------------------- */
i32 gfx_text_width(const char *s) { return k_strlen(s) * 8; }

void gfx_text(i32 x, i32 y, const char *s, u32 c)
{
    while (*s) {
        u8 ch = (u8)*s++;
        if (ch < 0x20 || ch > 0x7E) ch = '?';
        const u8 *g = FONT8X16[ch - 0x20];
        for (i32 j = 0; j < 16; j++) {
            u8 row = g[j];
            for (i32 i = 0; i < 8; i++)
                if (row & (0x80 >> i)) gfx_pixel(x + i, y + j, c);
        }
        x += 8;
    }
}

void gfx_text_centered(i32 cx, i32 y, const char *s, u32 c)
{
    gfx_text(cx - gfx_text_width(s) / 2, y, s, c);
}

/* Letterbox the back buffer to SET.viewport_w x SET.viewport_h centered.
 * Pixels outside the viewport are filled with the BG_BOT colour so that
 * a "Settings → Display → Resolution" change visibly shrinks the canvas
 * without re-allocating the back buffer (BSS is fixed at compile time).  */
void gfx_apply_viewport(void)
{
    if (SET.viewport_w <= 0 || SET.viewport_h <= 0) return;
    i32 vw = SET.viewport_w; if (vw > (i32)FB.width)  vw = (i32)FB.width;
    i32 vh = SET.viewport_h; if (vh > (i32)FB.height) vh = (i32)FB.height;
    i32 ox = ((i32)FB.width  - vw) / 2;
    i32 oy = ((i32)FB.height - vh) / 2;
    u32 fill = PAL_BG_BOT;
    /* top + bottom bars */
    for (i32 y = 0; y < oy; y++)
        for (u32 x = 0; x < BACK_W; x++) BACK[y * BACK_W + x] = fill;
    for (i32 y = oy + vh; y < (i32)BACK_H; y++)
        for (u32 x = 0; x < BACK_W; x++) BACK[y * BACK_W + x] = fill;
    /* left + right side strips */
    for (i32 y = oy; y < oy + vh; y++) {
        for (i32 x = 0; x < ox;            x++) BACK[y * BACK_W + x] = fill;
        for (i32 x = ox + vw; x < (i32)BACK_W; x++) BACK[y * BACK_W + x] = fill;
    }
}

/* darken every back-buffer pixel by `amount`/255 — used for boot fade */
void gfx_dim(u8 amount)
{
    u32 ia = 255 - amount;
    for (u32 i = 0; i < BACK_W * BACK_H; i++) {
        u32 c = BACK[i];
        u32 r = (((c >> 16) & 0xFF) * ia) >> 8;
        u32 g = (((c >>  8) & 0xFF) * ia) >> 8;
        u32 b = (( c        & 0xFF) * ia) >> 8;
        BACK[i] = (r << 16) | (g << 8) | b;
    }
}
