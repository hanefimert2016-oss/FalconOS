/* =============================================================================
 *  FalconOS — Personal Kernel UI
 * -----------------------------------------------------------------------------
 *  macOS-inspired clean home screen:
 *    - large "Falcon" wordmark + tagline, centred
 *    - animated breathing accent ring around the logo glyph
 *    - bottom dock with five circular app tiles, hovered tile lifts
 *    - up-time / tick counter in upper-left "card"
 * ============================================================================= */
#include "falcon.h"

static i32 dock_idx = 2;     /* hovered dock app */

static const char *DOCK_LABELS[] = {
    "Home", "Files", "Web", "Mail", "Music",
};
static const u32 DOCK_TINTS[] = {
    0x4F9EFF, 0xFFB547, 0x46D160, 0xFF5E57, 0xC084FC,
};
#define DOCK_N ((i32)(sizeof DOCK_LABELS / sizeof *DOCK_LABELS))

void mode_personal_input(i32 key)
{
    if (key == KEY_LEFT  && dock_idx > 0)         dock_idx--;
    if (key == KEY_RIGHT && dock_idx < DOCK_N - 1) dock_idx++;
}

/* simple integer "sine" — peaks at ±64, period 64, no FPU */
static i32 ksin(u32 t)
{
    static const i8 TABLE[16] = {
        0, 12, 24, 35, 45, 53, 59, 62,
        64, 62, 59, 53, 45, 35, 24, 12
    };
    u32 q = (t >> 1) & 63;
    if (q < 16) return  TABLE[q];
    if (q < 32) return  TABLE[31 - q];
    if (q < 48) return -TABLE[q - 32];
    return -TABLE[63 - q];
}

void mode_personal_render(u32 frame)
{
    i32 cx = (i32)FB.width  / 2;
    i32 cy = (i32)FB.height / 2 - 40;

    /* ---- breathing concentric rings (the "circular" identity) ----------- */
    i32 pulse = ksin(frame) / 8;       /* −8 … +8 px */

    for (i32 i = 0; i < 4; i++) {
        i32 r = 70 + i * 28 + pulse;
        u32 col = (i == 0) ? COL_ACCENT : COL_PANEL_HI;
        gfx_circle_outline(cx, cy, r,     col);
        gfx_circle_outline(cx, cy, r + 1, col);
    }
    gfx_circle(cx, cy, 60 + pulse / 2, COL_ACCENT);
    gfx_text_centered(cx, cy - 8, "Falcon", 0x0F1216);

    /* ---- wordmark + tagline -------------------------------------------- */
    gfx_text_centered(cx, cy + 110, "FalconOS",       COL_TEXT);
    gfx_text_centered(cx, cy + 134, "Born to Fly",    COL_TEXT_DIM);

    /* ---- up-time / status card (top-left) ------------------------------ */
    {
        i32 x = 24, y = 56, w = 220, h = 76;
        gfx_round_rect_a(x, y, w, h, 14, COL_PANEL, 220);
        gfx_round_outline(x, y, w, h, 14, COL_PANEL_HI);
        gfx_text(x + 16, y + 12, "Up-time", COL_TEXT_DIM);

        char buf[32]; k_itoa(frame, buf, 10);
        char line[40] = "frames  ";
        char *t = line;
        while (*t) t++;
        k_strcpy(t, buf);
        gfx_text(x + 16, y + 36, line, COL_TEXT);

        gfx_circle(x + w - 22, y + 22, 5, COL_OK);
        gfx_text(x + w - 92, y + 36, "online", COL_OK);
    }

    /* ---- dock ----------------------------------------------------------- */
    {
        i32 tile  = 64;
        i32 gap   = 18;
        i32 dw    = DOCK_N * tile + (DOCK_N - 1) * gap + 32;
        i32 dh    = tile + 28;
        i32 dx    = ((i32)FB.width - dw) / 2;
        i32 dy    = (i32)FB.height - dh - 28;

        /* glass slab */
        gfx_round_rect_a(dx, dy, dw, dh, 22, COL_GLASS, 200);
        gfx_round_outline(dx, dy, dw, dh, 22, COL_PANEL_HI);

        for (i32 i = 0; i < DOCK_N; i++) {
            bool hov   = (i == dock_idx);
            i32  lift  = hov ? -10 : 0;
            i32  ix    = dx + 16 + i * (tile + gap) + tile / 2;
            i32  iy    = dy + 14 + tile / 2 + lift;
            i32  rad   = hov ? tile / 2 : tile / 2 - 4;

            /* drop shadow */
            gfx_circle(ix + 2, iy + 4, rad, 0x000000);
            /* tile */
            gfx_circle(ix, iy, rad, DOCK_TINTS[i]);
            /* highlight ring on hovered tile */
            if (hov) {
                gfx_circle_outline(ix, iy, rad + 4, COL_TEXT);
                gfx_text_centered(ix, iy + rad + 8, DOCK_LABELS[i], COL_TEXT);
            }
            /* tiny inner glyph dot for personality */
            gfx_circle(ix - rad / 4, iy - rad / 4, 4, 0xFFFFFF);
        }
    }

    /* ---- nav hint ------------------------------------------------------- */
    gfx_text_centered((i32)FB.width / 2,
                      (i32)FB.height - 22,
                      "<- ->  navigate dock", COL_TEXT_DIM);
}
