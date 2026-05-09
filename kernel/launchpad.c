/* =============================================================================
 *  FalconOS — Launchpad (full-screen app grid, F2)
 * -----------------------------------------------------------------------------
 *  macOS-style Launchpad: dim the desktop with a translucent layer, pop a
 *  4 × 3 grid of every registered app (12 slots), animate scale-in over
 *  ~200 ms, dispatch keyboard / mouse to launch.
 *
 *  Hot keys (only when launchpad is open):
 *      arrows   move grid cursor
 *      Enter    launch selected app  (closes launchpad)
 *      Esc, F2  close launchpad
 *      mouse    hover lifts tile, click launches
 * ============================================================================= */
#include "falcon.h"

#define LP_COLS 5
#define LP_ROWS 5
#define LP_MAX  (LP_COLS * LP_ROWS)

static bool g_open      = false;
static u32  g_open_at   = 0;
static i32  g_cursor    = 0;

bool launchpad_is_open(void)  { return g_open; }

void launchpad_open(void)
{
    g_open    = true;
    g_open_at = pit_ms();
    /* keep g_cursor where it was so user can resume */
}

void launchpad_close(void)
{
    g_open = false;
}

static void clamp_cursor(void)
{
    i32 n = apps_count();
    if (n > LP_MAX) n = LP_MAX;
    if (g_cursor < 0)      g_cursor = 0;
    if (g_cursor > n - 1)  g_cursor = n - 1;
}

void launchpad_input(i32 key)
{
    if (key == KEY_ESC || key == KEY_F2) { launchpad_close(); return; }

    if (key == KEY_LEFT)  g_cursor--;
    if (key == KEY_RIGHT) g_cursor++;
    if (key == KEY_UP)    g_cursor -= LP_COLS;
    if (key == KEY_DOWN)  g_cursor += LP_COLS;
    clamp_cursor();

    /* `p` / `P`  — toggle desktop pin for the highlighted app           */
    if (key == 'p' || key == 'P') {
        desktop_pin_toggle(g_cursor);
        return;
    }

    if (key == KEY_ENTER || key == ' ') {
        apps_open(g_cursor);
        launchpad_close();
    }
}

i32 launchpad_cursor(void) { return g_cursor; }

void launchpad_render(u32 frame)
{
    /* dim the desktop */
    gfx_rect_a(0, 0, FB.width, FB.height, COL_SHADOW, 130);

    /* Aero — blur the entire desktop behind the launchpad so the
     * tiles float over a soft, readable wash of wallpaper. We do
     * the blur in horizontal strips to stay within the BLUR scratch.
     * Flat overlay fallback for users with Aero off.                  */
    if (SET.aero_enabled) {
        i32 W = (i32)FB.width;
        i32 H = (i32)FB.height;
        i32 strip = 320;
        for (i32 y = 0; y < H; y += strip) {
            i32 sh = (y + strip > H) ? H - y : strip;
            gfx_blur_rect(0, y, W, sh, 8);
        }
        gfx_rect_a(0, 0, W, H, PAL_PANEL, 80);
    } else {
        /* very-soft frosted overlay (light/dark-aware tint to keep it readable) */
        gfx_rect_a(0, 0, FB.width, FB.height, PAL_PANEL, 35);
    }

    /* scale-in animation: ~200 ms */
    u32 dt = pit_ms() - g_open_at;
    if (dt > 200) dt = 200;
    /* scale 0.85 → 1.00 over the animation */
    i32 scale = 85 + (i32)(dt * 15 / 200);          /* percent */

    i32 n = apps_count();
    if (n > LP_MAX) n = LP_MAX;

    /* tile sizing (relative to FB so 1080p / 2K look good) */
    i32 tile_base = (i32)FB.height / 7;             /* ~155 at 1080p */
    if (tile_base < 110) tile_base = 110;
    if (tile_base > 220) tile_base = 220;
    i32 gap = tile_base / 4;
    i32 grid_w = LP_COLS * tile_base + (LP_COLS - 1) * gap;
    i32 grid_h = LP_ROWS * tile_base + (LP_ROWS - 1) * gap;
    i32 grid_x = ((i32)FB.width  - grid_w) / 2;
    i32 grid_y = ((i32)FB.height - grid_h) / 2;

    /* title */
    gfx_text_centered((i32)FB.width / 2, grid_y - 60, "Launchpad", PAL_TEXT);
    gfx_text_centered((i32)FB.width / 2, grid_y - 36,
                      T("arrows + Enter to open, click to launch, P pins to desktop, Esc closes",
                        "ok ile gez Enter ile aç, tıkla, P masaüstüne sabitler, Esc kapatır"),
                      PAL_TEXT_DIM);

    i32 mx, my; bool ml; mouse_get(&mx, &my, &ml); (void)ml;
    bool clicked = mouse_consume_click();

    for (i32 i = 0; i < n; i++) {
        i32 r = i / LP_COLS, c = i % LP_COLS;
        i32 cx = grid_x + c * (tile_base + gap) + tile_base / 2;
        i32 cy = grid_y + r * (tile_base + gap) + tile_base / 2;

        /* apply scale */
        i32 tile = tile_base * scale / 100;
        i32 rad  = tile * 38 / 100;            /* tile rounded-rect feel  */

        bool hov_m = (mx >= cx - tile / 2 && mx <= cx + tile / 2 &&
                      my >= cy - tile / 2 && my <= cy + tile / 2);
        bool hov_k = (i == g_cursor);
        bool hov   = hov_m || hov_k;

        /* shadow */
        gfx_round_rect_a(cx - tile / 2 + 4, cy - tile / 2 + 10,
                         tile, tile, rad, COL_SHADOW, 60);
        /* tile background */
        u32 t = apps_tint(i);
        gfx_round_rect_a(cx - tile / 2, cy - tile / 2,
                         tile, tile, rad, t, 255);
        /* gloss highlight on top half */
        gfx_round_rect_a(cx - tile / 2 + 6, cy - tile / 2 + 6,
                         tile - 12, tile / 3, rad - 6, 0xFFFFFF, 60);
        /* glyph drawn at ~1.4× icon scale */
        apps_draw_icon(i, cx, cy - 4);
        /* selection ring */
        if (hov) {
            gfx_round_outline(cx - tile / 2 - 4, cy - tile / 2 - 4,
                              tile + 8, tile + 8, rad + 4, PAL_ACCENT);
        }
        /* pinned indicator */
        if (desktop_pin_is_pinned(i)) {
            gfx_circle(cx + tile / 2 - 12, cy - tile / 2 + 12, 6, COL_OK);
            gfx_circle_outline(cx + tile / 2 - 12, cy - tile / 2 + 12, 6, 0xFFFFFF);
        }
        /* label below tile */
        i32 ly = cy + tile / 2 + 16;
        gfx_text_centered(cx, ly,     apps_display_name(i),     PAL_TEXT);
        gfx_text_centered(cx, ly + 18, apps_display_subtitle(i), PAL_TEXT_DIM);

        if (hov_m && clicked) {
            apps_open(i);
            launchpad_close();
        }
    }

    (void)frame;
}
