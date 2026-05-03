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

/* Frosted-glass rounded rect.  When SET.aero_enabled is true (the
 * default in v5.2), this dispatches to the real blur-based Aero glass
 * defined further down so every existing dock / widget / panel call
 * site picks up translucency for free.  The flat-overlay fallback
 * keeps the same visual silhouette for users who turn Aero off, and
 * is also used by very-early-boot code paths (e.g. the installer's
 * first frame) where SET hasn't been initialised yet.                 */
void gfx_round_glass(i32 x, i32 y, i32 w, i32 h, i32 r)
{
    if (SET.aero_enabled) {
        /* Liquid Glass leans into translucency: panel tint drops from
         * 170 -> 120 alpha so the wallpaper genuinely glows through,
         * and we pre-blur once more before the round rect so curved
         * edges look refractive (the same trick Apple's UI uses for
         * iOS Control Center sheets).                               */
        if (SET.theme == THEME_LIQUID) {
            /* Liquid Glass v2 — heavier translucency, layered tints,
             * a soft top-to-bottom luminance gradient across the panel
             * that visually mimics the way light refracts through a
             * curved sheet of glass.  Now affordable per-frame because
             * the rolling-window blur made the underlying primitive
             * ~8x faster (see gfx_blur_rect docs).                   */
            gfx_round_drop_shadow(x, y, w, h, r);
            gfx_blur_rect(x, y, w, h, 7);
            /* core translucent body (very light tint so wallpaper sings) */
            gfx_round_rect_a(x, y, w, h, r, PAL_GLASS, 95);
            /* soft top luminance band — the "aqua bloom" Apple uses on
             * Control Center.  Three stacked thin bands fade out.       */
            gfx_rect_a(x + r,     y + 1, w - 2 * r, 2, 0xCFEFFA, 110);
            gfx_rect_a(x + r,     y + 3, w - 2 * r, 2, 0xB7E3F4,  70);
            gfx_rect_a(x + r,     y + 5, w - 2 * r, 1, 0xA0D8EC,  35);
            /* bottom edge gets a faint cyan kiss for refraction depth */
            gfx_rect_a(x + r,     y + h - 2, w - 2 * r, 1,
                       0x9FD4E8, 50);
            gfx_round_outline(x, y, w, h, r, PAL_HAIRLINE);
            gfx_round_inset_highlight(x, y, w, h, r);
            return;
        }
        /* Lumen / Nox / Nordic / Rose-Gold: stronger tint than flat
         * glass so UI text on top of the panel remains legible
         * against busy blurred backgrounds.                          */
        gfx_aero_round_rect(x, y, w, h, r, PAL_GLASS, 170);
        return;
    }
    /* Aero off — flat overlay, but still benefit from the new layered
     * shadow + inset highlight for visual lift.                      */
    gfx_round_drop_shadow(x, y, w, h, r);
    gfx_round_rect_a(x, y, w, h, r, PAL_GLASS, 235);
    gfx_round_outline(x, y, w, h, r, PAL_HAIRLINE);
    gfx_round_inset_highlight(x, y, w, h, r);
}

/* =============================================================================
 *  v5.2 "Aero" — frosted-glass blur
 * -----------------------------------------------------------------------------
 *  Two-pass separable box blur over a sub-rect of the live back buffer.
 *  Only the requested rect is touched, so the cost scales with rect area
 *  (a 600×400 app-window chrome blur runs in ~12 ms even in TCG).
 *
 *  We need a scratch buffer to hold one intermediate pass — sized for the
 *  largest panel we ever blur (full-width dock at 1920×120 ≈ 0.9 MiB).
 *  Static allocation in BSS keeps the code allocator-free.
 * ============================================================================= */
/* Scratch sized for a full-screen 2K blur — needed by the new sliding
 * Help drawer (full panel height) and any future right-edge / left-edge
 * panels.  ~14 MB BSS; combined with the 2K back-buffer this brings the
 * static footprint to ~29 MB, harmless on the 4 GiB QEMU box and on
 * any modern PC.  Smaller blurs (menu bar, dock, widgets) still pay
 * just for the rectangle they actually touch.                        */
#define BLUR_MAX_W 2560
#define BLUR_MAX_H 1440
static u32 BLUR_SCRATCH[BLUR_MAX_W * BLUR_MAX_H];

/* Separable box blur with a rolling-window sum: each inner-loop step is
 * O(1) (one add + one subtract) instead of O(2r+1) like the previous
 * version.  At radius 6 / 8 this is the difference between recomputing
 * 13 / 17 samples per pixel and recomputing 1 — we drop from ~25 ms
 * per full-screen panel down to ~3 ms on a single 2K vCPU.            */
void gfx_blur_rect(i32 x, i32 y, i32 w, i32 h, i32 r)
{
    /* clamp to buffer bounds and the scratch capacity */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (i32)BACK_W) w = (i32)BACK_W - x;
    if (y + h > (i32)BACK_H) h = (i32)BACK_H - y;
    if (w > BLUR_MAX_W) w = BLUR_MAX_W;
    if (h > BLUR_MAX_H) h = BLUR_MAX_H;
    if (w <= 0 || h <= 0 || r <= 0) return;
    if (r > 8) r = 8;
    if (w <= 1 || h <= 1) return;

    i32 win = 2 * r + 1;        /* fixed kernel width — clamped at edges */

    /* ---- horizontal pass (BACK -> BLUR_SCRATCH) -------------------- */
    for (i32 yy = 0; yy < h; yy++) {
        u32 *src = &BACK[(y + yy) * BACK_W + x];
        u32 *dst = &BLUR_SCRATCH[yy * BLUR_MAX_W];

        u32 sr = 0, sg = 0, sb = 0;
        i32 n  = 0;

        /* prime the window with samples [0 .. r] (clamped to right). */
        for (i32 k = 0; k <= r && k < w; k++) {
            u32 c = src[k];
            sr += (c >> 16) & 0xFF;
            sg += (c >>  8) & 0xFF;
            sb +=  c        & 0xFF;
            n++;
        }

        for (i32 xx = 0; xx < w; xx++) {
            dst[xx] = ((sr / n) << 16) | ((sg / n) << 8) | (sb / n);

            /* advance window: drop sample (xx - r), add (xx + r + 1) */
            i32 drop = xx - r;
            i32 add  = xx + r + 1;
            if (drop >= 0) {
                u32 c = src[drop];
                sr -= (c >> 16) & 0xFF;
                sg -= (c >>  8) & 0xFF;
                sb -=  c        & 0xFF;
                n--;
            }
            if (add < w) {
                u32 c = src[add];
                sr += (c >> 16) & 0xFF;
                sg += (c >>  8) & 0xFF;
                sb +=  c        & 0xFF;
                n++;
            }
        }
        (void)win;
    }

    /* ---- vertical pass (BLUR_SCRATCH -> BACK) ---------------------- */
    for (i32 xx = 0; xx < w; xx++) {
        u32 sr = 0, sg = 0, sb = 0;
        i32 n  = 0;

        /* prime: rows [0 .. r] of the scratch column */
        for (i32 k = 0; k <= r && k < h; k++) {
            u32 c = BLUR_SCRATCH[k * BLUR_MAX_W + xx];
            sr += (c >> 16) & 0xFF;
            sg += (c >>  8) & 0xFF;
            sb +=  c        & 0xFF;
            n++;
        }

        for (i32 yy = 0; yy < h; yy++) {
            BACK[(y + yy) * BACK_W + (x + xx)] =
                ((sr / n) << 16) | ((sg / n) << 8) | (sb / n);

            i32 drop = yy - r;
            i32 add  = yy + r + 1;
            if (drop >= 0) {
                u32 c = BLUR_SCRATCH[drop * BLUR_MAX_W + xx];
                sr -= (c >> 16) & 0xFF;
                sg -= (c >>  8) & 0xFF;
                sb -=  c        & 0xFF;
                n--;
            }
            if (add < h) {
                u32 c = BLUR_SCRATCH[add * BLUR_MAX_W + xx];
                sr += (c >> 16) & 0xFF;
                sg += (c >>  8) & 0xFF;
                sb +=  c        & 0xFF;
                n++;
            }
        }
    }
}

/* Layered soft drop shadow for any panel: 4 progressively-larger,
 * progressively-fainter copies of the same rounded rect offset down.
 * Gives surfaces a tangible "lift" without baking the shadow into
 * every render path.                                                  */
void gfx_round_drop_shadow(i32 x, i32 y, i32 w, i32 h, i32 r)
{
    gfx_round_rect_a(x + 5, y + 14, w, h, r, COL_SHADOW, 18);
    gfx_round_rect_a(x + 3, y + 10, w, h, r, COL_SHADOW, 28);
    gfx_round_rect_a(x + 2, y + 6,  w, h, r, COL_SHADOW, 36);
    gfx_round_rect_a(x + 1, y + 3,  w, h, r, COL_SHADOW, 28);
}

/* 1-pixel inner highlight along the top + left edges of a rounded
 * rect.  Shipped as a thin white stroke at low alpha so glass panels
 * pick up the same "polished" look real macOS sheets get.            */
void gfx_round_inset_highlight(i32 x, i32 y, i32 w, i32 h, i32 r)
{
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    gfx_rect_a(x + r,     y + 1,         w - 2 * r, 1,           0xFFFFFF, 110);
    gfx_rect_a(x + 1,     y + r,         1,         h - 2 * r,   0xFFFFFF,  60);
}

/* Blurs the rect under (x,y,w,h,r) then overlays a translucent rounded
 * tint with a hairline border. The corner mask is applied by drawing a
 * full-alpha rectangle of TRANSPARENT pixels outside the rounded shape —
 * which we approximate by re-rendering only the rounded body with the
 * tint and trusting the corner-cut from gfx_round_rect_a.               */
void gfx_aero_round_rect(i32 x, i32 y, i32 w, i32 h, i32 r,
                          u32 tint, u8 tint_alpha)
{
    /* drop shadow (richer) first so the blur doesn't smear it */
    gfx_round_drop_shadow(x, y, w, h, r);

    /* blur the area now (after shadow, so the shadow contributes a soft
     * darkening at the panel edges — looks natural).                  */
    gfx_blur_rect(x, y, w, h, 6);

    /* translucent tint on top of the blurred pixels */
    gfx_round_rect_a(x, y, w, h, r, tint, tint_alpha);

    /* hairline border + inset highlight for crispness + lift          */
    gfx_round_outline(x, y, w, h, r, PAL_HAIRLINE);
    gfx_round_inset_highlight(x, y, w, h, r);
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

/* ---- text -----------------------------------------------------------------
 *  gfx_text:   8 × 16 antialiased default — each glyph is a row-major
 *              alpha map (one byte per pixel, 0 = transparent).
 *  gfx_text_lg: 16 × 32 1-bit headline font, used for big titles.
 *  Same coordinate / color semantics; (x, y) is the top-left of the glyph
 *  cell, c is an 0xAARRGGBB tint.                                        */
i32 gfx_text_width(const char *s)    { return k_strlen(s) * 8;  }
i32 gfx_text_width_lg(const char *s) { return k_strlen(s) * 16; }

void gfx_text(i32 x, i32 y, const char *s, u32 c)
{
    while (*s) {
        u8 ch = (u8)*s++;
        if (ch < 0x20 || ch > 0x7E) ch = '?';
        const u8 *g = FONT8X16[ch - 0x20];      /* 8 × 16 = 128 alpha bytes */
        for (i32 j = 0; j < 16; j++) {
            const u8 *row = g + j * 8;
            for (i32 i = 0; i < 8; i++) {
                u8 a = row[i];
                if (a) gfx_pixel_a(x + i, y + j, c, a);
            }
        }
        x += 8;
    }
}

void gfx_text_centered(i32 cx, i32 y, const char *s, u32 c)
{
    gfx_text(cx - gfx_text_width(s) / 2, y, s, c);
}

/* Pixel-edge anti-aliasing for the 16×32 1-bit headline font.
 * For every "off" pixel adjacent to one or more "on" pixels we emit a
 * faint intermediate alpha so diagonal edges and curves stop looking
 * stair-stepped — a cheap stand-in for true 2× super-sampling that
 * keeps the font binary tiny.                                       */
static inline u8 fontlg_at(const u8 *g, i32 i, i32 j)
{
    if (i < 0 || i > 15 || j < 0 || j > 31) return 0;
    u16 row = ((u16)g[j * 2] << 8) | g[j * 2 + 1];
    return (row & (0x8000 >> i)) ? 1 : 0;
}

void gfx_text_lg(i32 x, i32 y, const char *s, u32 c)
{
    while (*s) {
        u8 ch = (u8)*s++;
        if (ch < 0x20 || ch > 0x7E) ch = '?';
        const u8 *g = FONT16X32[ch - 0x20];     /* 16 × 32 = 64 bytes (1-bit) */
        for (i32 j = 0; j < 32; j++) {
            for (i32 i = 0; i < 16; i++) {
                if (fontlg_at(g, i, j)) {
                    gfx_pixel(x + i, y + j, c);
                    continue;
                }
                /* edge feather: alpha based on cardinal + diagonal
                 * neighbour coverage. card=2*hit, diag=1*hit, max 8. */
                i32 cov = 0;
                cov += 2 * (fontlg_at(g, i - 1, j) +
                            fontlg_at(g, i + 1, j) +
                            fontlg_at(g, i, j - 1) +
                            fontlg_at(g, i, j + 1));
                cov +=     (fontlg_at(g, i - 1, j - 1) +
                            fontlg_at(g, i + 1, j - 1) +
                            fontlg_at(g, i - 1, j + 1) +
                            fontlg_at(g, i + 1, j + 1));
                if (cov) {
                    if (cov > 8) cov = 8;
                    u8 a = (u8)((cov * 96) / 8);
                    gfx_pixel_a(x + i, y + j, c, a);
                }
            }
        }
        x += 16;
    }
}

void gfx_text_lg_centered(i32 cx, i32 y, const char *s, u32 c)
{
    gfx_text_lg(cx - gfx_text_width_lg(s) / 2, y, s, c);
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
