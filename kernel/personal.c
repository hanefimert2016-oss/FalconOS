/* =============================================================================
 *  FalconOS — Personal Kernel UI (v4 "Lumen")
 * -----------------------------------------------------------------------------
 *  Big-Sur-inspired light desktop:
 *    - frosted-glass widgets (uptime, weather/status pills) on a soft wallpaper
 *    - macOS-style dock: glass tray, lifted hover tiles, drop shadow,
 *      app name labels, click-to-launch
 *    - F2 opens a full-screen Launchpad (handled by main loop dispatcher)
 *
 *  Mouse-driven: hover detected against tile bounds, click launches apps.
 *  Keyboard fallback: ←/→ to move dock cursor, Enter to launch, F2 grid.
 * ============================================================================= */
#include "falcon.h"

static i32 dock_idx = 2;        /* keyboard hover */

/* up to this many tiles fit on the dock; the rest live in the Launchpad */
#define DOCK_MAX 7

void mode_personal_input(i32 key)
{
    if (apps_active() >= 0) { apps_input_active(key); return; }

    i32 n = apps_count();
    if (n > DOCK_MAX) n = DOCK_MAX;
    if (key == KEY_LEFT  && dock_idx > 0)        dock_idx--;
    if (key == KEY_RIGHT && dock_idx < n - 1)    dock_idx++;
    if (key == KEY_ENTER || key == ' ')          apps_open(dock_idx);
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

/* --- top-left uptime card ------------------------------------------------- */
static void draw_uptime_card(void)
{
    i32 x = 24, y = 50, w = 240, h = 90;
    gfx_round_glass(x, y, w, h, 16);
    gfx_text(x + 18, y + 14, "Up-time", COL_TEXT_DIM);

    u32 H, M, S; pit_uptime(&H, &M, &S);
    char buf[32], tmp[8];
    k_strcpy(buf, "");
    k_itoa(H, tmp, 10); if (H < 10) k_strcat(buf, "0");
    k_strcat(buf, tmp); k_strcat(buf, ":");
    k_itoa(M, tmp, 10); if (M < 10) k_strcat(buf, "0");
    k_strcat(buf, tmp); k_strcat(buf, ":");
    k_itoa(S, tmp, 10); if (S < 10) k_strcat(buf, "0");
    k_strcat(buf, tmp);
    gfx_text(x + 18, y + 38, buf, COL_TEXT);

    gfx_circle(x + w - 22, y + 22, 5, COL_OK);
    gfx_text(x + w - 92, y + 38, "online", COL_OK);
    gfx_text(x + 18, y + 64, "all systems nominal", COL_TEXT_FAINT);
}

/* --- top-right resolution card ------------------------------------------- */
static void draw_res_card(void)
{
    i32 w = 240, h = 90;
    i32 x = (i32)FB.width - w - 24, y = 50;
    gfx_round_glass(x, y, w, h, 16);
    gfx_text(x + 18, y + 14, "Display", COL_TEXT_DIM);

    char buf[32], tmp[8];
    k_strcpy(buf, "");
    k_itoa(FB.width, tmp, 10);  k_strcat(buf, tmp);
    k_strcat(buf, " x ");
    k_itoa(FB.height, tmp, 10); k_strcat(buf, tmp);
    gfx_text(x + 18, y + 38, buf, COL_TEXT);

    k_strcpy(buf, "32 bpp linear FB");
    gfx_text(x + 18, y + 64, buf, COL_TEXT_FAINT);

    gfx_circle(x + w - 22, y + 22, 5, COL_ACCENT);
}

/* --- breathing hero logo -------------------------------------------------- */
static void draw_hero(u32 frame)
{
    i32 cx = (i32)FB.width  / 2;
    i32 cy = (i32)FB.height / 2 - 40;

    i32 pulse = ksin(frame) / 8;
    /* faint outer rings */
    for (i32 i = 0; i < 4; i++) {
        i32 r = 90 + i * 32 + pulse;
        gfx_circle_outline(cx, cy, r,     COL_ACCENT_DIM);
        gfx_circle_outline(cx, cy, r + 1, COL_ACCENT_DIM);
    }
    /* solid hero with subtle highlight */
    gfx_circle_a(cx + 2, cy + 8, 78 + pulse / 2, COL_SHADOW, 30);
    gfx_circle  (cx,     cy,     76 + pulse / 2, COL_ACCENT);
    gfx_circle_a(cx - 18, cy - 22, 30, 0xFFFFFF, 90);
    gfx_text_centered(cx, cy - 8, "Falcon", 0xFFFFFF);

    gfx_text_centered(cx, cy + 110, "FalconOS", COL_TEXT);
    gfx_text_centered(cx, cy + 132, "v4 \"Lumen\"  -  Born to Fly", COL_TEXT_DIM);
}

/* --- bottom dock ---------------------------------------------------------- */
static void draw_dock(void)
{
    i32 mx, my; bool ml; mouse_get(&mx, &my, &ml);
    bool clicked = mouse_consume_click();
    (void)ml;

    i32 n = apps_count();
    if (n > DOCK_MAX) n = DOCK_MAX;

    i32 tile  = 68;
    i32 gap   = 18;
    i32 dw    = n * tile + (n - 1) * gap + 36;
    i32 dh    = tile + 32;
    i32 dx    = ((i32)FB.width - dw) / 2;
    i32 dy    = (i32)FB.height - dh - 22;

    /* glass tray */
    gfx_round_rect_a(dx + 2, dy + 8, dw, dh, 26, COL_SHADOW, 60);
    gfx_round_rect_a(dx,     dy,     dw, dh, 26, COL_PANEL,  220);
    gfx_round_outline(dx,    dy,     dw, dh, 26, COL_HAIRLINE);

    for (i32 i = 0; i < n; i++) {
        i32 ix    = dx + 18 + i * (tile + gap) + tile / 2;
        i32 iy    = dy + 16 + tile / 2;
        i32 rad_f = tile / 2;

        bool hov_m = (mx >= ix - rad_f && mx <= ix + rad_f &&
                      my >= iy - rad_f - 4 && my <= iy + rad_f + 4);
        bool hov_k = (i == dock_idx);
        bool hov   = hov_m || hov_k;

        i32 lift  = hov ? -10 : 0;
        i32 rad   = hov ? rad_f : rad_f - 4;
        i32 ix2   = ix;
        i32 iy2   = iy + lift;

        /* drop shadow */
        gfx_circle_a(ix2 + 2, iy2 + 4, rad, COL_SHADOW, 80);
        /* tile */
        gfx_circle(ix2, iy2, rad, apps_tint(i));
        /* gloss highlight */
        gfx_circle_a(ix2 - rad / 3, iy2 - rad / 3, rad / 3, 0xFFFFFF, 80);
        /* glyph */
        apps_draw_icon(i, ix2, iy2);
        /* highlight ring + label on hovered tile */
        if (hov) {
            gfx_circle_outline(ix2, iy2, rad + 4, COL_ACCENT);
            i32 lw = gfx_text_width(apps_name(i)) + 16;
            i32 lx = ix2 - lw / 2;
            i32 ly = iy2 + rad + 6;
            gfx_round_rect_a(lx, ly, lw, 18, 9, COL_PANEL, 240);
            gfx_round_outline(lx, ly, lw, 18, 9, COL_HAIRLINE);
            gfx_text_centered(ix2, ly + 2, apps_name(i), COL_TEXT);
        }

        if (hov_m && clicked) apps_open(i);
    }

    /* "more" indicator if there are more apps in the launchpad */
    if (apps_count() > DOCK_MAX) {
        i32 mx2 = dx + dw - 14;
        gfx_circle(mx2,     dy + dh - 8, 2, COL_TEXT_FAINT);
        gfx_circle(mx2 - 6, dy + dh - 8, 2, COL_TEXT_FAINT);
        gfx_circle(mx2 - 12,dy + dh - 8, 2, COL_TEXT_FAINT);
    }
}

/* --- entry point --------------------------------------------------------- */
void mode_personal_render(u32 frame)
{
    draw_uptime_card();
    draw_res_card();
    draw_hero(frame);
    draw_dock();

    /* nav hint */
    gfx_text_centered((i32)FB.width / 2,
                      (i32)FB.height - 18,
                      "<- ->  navigate     Enter  open     F2  Launchpad",
                      COL_TEXT_DIM);

    /* active app window (drawn over everything except menu bar / cursor) */
    apps_render_active(frame);
}
