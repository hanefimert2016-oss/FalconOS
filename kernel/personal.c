/* =============================================================================
 *  FalconOS — Personal Kernel UI
 * -----------------------------------------------------------------------------
 *  macOS-inspired home screen:
 *    - large concentric breathing logo (circular identity)
 *    - top status pill (parent file) and a top-right HH:MM:SS uptime clock
 *    - bottom dock with circular app tiles, hovered tile lifts, click opens
 *    - up-time / status card top-left, navigation hint bottom
 *
 *  Mouse-driven: hover detected against tile bounds, click launches apps.
 * ============================================================================= */
#include "falcon.h"

static i32 dock_idx = 2;        /* keyboard hover */
extern i32 apps_count(void);
extern const char *apps_name(i32);
extern u32  apps_tint(i32);
extern void apps_draw_icon(i32, i32, i32);

void mode_personal_input(i32 key)
{
    if (apps_active() >= 0) { apps_input_active(key); return; }

    i32 n = apps_count();
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

void mode_personal_render(u32 frame)
{
    i32 cx = (i32)FB.width  / 2;
    i32 cy = (i32)FB.height / 2 - 50;

    /* ---- breathing concentric rings ----------------------------------- */
    i32 pulse = ksin(frame) / 8;
    for (i32 i = 0; i < 4; i++) {
        i32 r = 70 + i * 28 + pulse;
        u32 col = (i == 0) ? COL_ACCENT : COL_PANEL_HI;
        gfx_circle_outline(cx, cy, r,     col);
        gfx_circle_outline(cx, cy, r + 1, col);
    }
    gfx_circle(cx, cy, 60 + pulse / 2, COL_ACCENT);
    gfx_text_centered(cx, cy - 8, "Falcon", 0x0F1216);

    /* ---- wordmark + tagline ------------------------------------------- */
    gfx_text_centered(cx, cy + 110, "FalconOS",       COL_TEXT);
    gfx_text_centered(cx, cy + 134, "Born to Fly",    COL_TEXT_DIM);

    /* ---- up-time card (top-left) -------------------------------------- */
    {
        i32 x = 24, y = 56, w = 220, h = 76;
        gfx_round_rect_a(x, y, w, h, 14, COL_PANEL, 220);
        gfx_round_outline(x, y, w, h, 14, COL_PANEL_HI);
        gfx_text(x + 16, y + 12, "Up-time", COL_TEXT_DIM);

        u32 H, M, S; pit_uptime(&H, &M, &S);
        char buf[32], tmp[8];
        k_strcpy(buf, "");
        k_itoa(H, tmp, 10); if (H < 10) k_strcat(buf, "0");
        k_strcat(buf, tmp); k_strcat(buf, ":");
        k_itoa(M, tmp, 10); if (M < 10) k_strcat(buf, "0");
        k_strcat(buf, tmp); k_strcat(buf, ":");
        k_itoa(S, tmp, 10); if (S < 10) k_strcat(buf, "0");
        k_strcat(buf, tmp);
        gfx_text(x + 16, y + 36, buf, COL_TEXT);

        gfx_circle(x + w - 22, y + 22, 5, COL_OK);
        gfx_text(x + w - 92, y + 36, "online", COL_OK);
    }

    /* ---- dock --------------------------------------------------------- */
    {
        i32 mx, my; bool ml; mouse_get(&mx, &my, &ml);
        bool clicked = mouse_consume_click();

        i32 n     = apps_count();
        i32 tile  = 64;
        i32 gap   = 18;
        i32 dw    = n * tile + (n - 1) * gap + 32;
        i32 dh    = tile + 28;
        i32 dx    = ((i32)FB.width - dw) / 2;
        i32 dy    = (i32)FB.height - dh - 28;

        gfx_round_rect_a(dx, dy, dw, dh, 22, COL_GLASS, 200);
        gfx_round_outline(dx, dy, dw, dh, 22, COL_PANEL_HI);

        for (i32 i = 0; i < n; i++) {
            i32  ix    = dx + 16 + i * (tile + gap) + tile / 2;
            i32  iy    = dy + 14 + tile / 2;
            i32  rad_f = tile / 2;

            /* hit test against tile bounding box (slightly bigger than circle) */
            bool hov_m = (mx >= ix - rad_f && mx <= ix + rad_f &&
                          my >= iy - rad_f && my <= iy + rad_f);
            bool hov_k = (i == dock_idx);
            bool hov   = hov_m || hov_k;

            i32  lift  = hov ? -10 : 0;
            i32  rad   = hov ? rad_f : rad_f - 4;
            i32  ix2   = ix;
            i32  iy2   = iy + lift;

            /* drop shadow */
            gfx_circle_a(ix2 + 2, iy2 + 4, rad, 0x000000, 160);
            /* tile */
            gfx_circle(ix2, iy2, rad, apps_tint(i));
            /* glyph */
            apps_draw_icon(i, ix2, iy2);
            /* highlight ring + label on hovered tile */
            if (hov) {
                gfx_circle_outline(ix2, iy2, rad + 4, COL_TEXT);
                gfx_text_centered(ix2, iy2 + rad + 8, apps_name(i), COL_TEXT);
            }

            if (hov_m && clicked) apps_open(i);
        }
    }

    /* ---- nav hint ---------------------------------------------------- */
    gfx_text_centered((i32)FB.width / 2,
                      (i32)FB.height - 22,
                      "<- ->  navigate     Enter  open     mouse: hover & click",
                      COL_TEXT_DIM);

    /* ---- active app window (drawn over everything except status bar) - */
    apps_render_active(frame);
}
