/* =============================================================================
 *  FalconOS — desktop widget grid  (v5)
 * -----------------------------------------------------------------------------
 *  v5 replaces v4's centered breathing hero circle with a 6-card widget grid:
 *
 *      ┌─────────┐ ┌─────────┐ ┌─────────┐
 *      │ Weather │ │ Calendar│ │ System  │
 *      └─────────┘ └─────────┘ └─────────┘
 *      ┌─────────┐ ┌─────────┐ ┌─────────┐
 *      │ Now-play│ │ Recents │ │ Quick   │
 *      └─────────┘ └─────────┘ └─────────┘
 *
 *  Widgets are *non-interactive* (purely informational) so they don't have
 *  to fight with desktop pin shortcuts for clicks.
 * ============================================================================= */
#include "falcon.h"

extern volatile u32 g_tick;

/* --------------------------------------------------------------------------- */
static void w_card(i32 x, i32 y, i32 w, i32 h, const char *title, u32 tint)
{
    gfx_round_glass(x, y, w, h, 16);
    gfx_round_outline(x, y, w, h, 16, PAL_HAIRLINE);
    /* tint pill at top */
    gfx_round_rect_a(x + 14, y + 12, 28, 28, 8, tint, 220);
    gfx_text(x + 50, y + 18, title, PAL_TEXT);
}

/* --------------------------------------------------------------------------- */
static void w_weather(i32 x, i32 y, i32 w, i32 h)
{
    w_card(x, y, w, h, T("Weather", "Hava"), 0x3070FF);
    /* sun + cloud icon */
    gfx_circle(x + 42, y + 80, 16, COL_WARN);
    gfx_circle_a(x + 60, y + 88, 18, PAL_PANEL, 230);
    gfx_circle_a(x + 76, y + 84, 14, PAL_PANEL, 220);
    /* temperature */
    gfx_text(x + w - 80, y + 60, "21",  PAL_TEXT);
    gfx_text(x + w - 36, y + 60, "C",   PAL_TEXT_DIM);
    gfx_text(x + 18, y + h - 38,
             T("Mostly sunny",
               "Cogunlukla gunesli"), PAL_TEXT_DIM);
    gfx_text(x + 18, y + h - 22, "Istanbul", PAL_TEXT_FAINT);
}

static void w_calendar(i32 x, i32 y, i32 w, i32 h)
{
    w_card(x, y, w, h, T("Calendar", "Takvim"), 0xE85D9C);
    /* big day number + month */
    gfx_text(x + w - 80, y + 60, "01", PAL_TEXT);
    gfx_text(x + 18, y + h - 56,
             T("Thursday",   "Persembe"), PAL_TEXT);
    gfx_text(x + 18, y + h - 38,
             T("May 1, 2026", "1 Mayis 2026"), PAL_TEXT_DIM);
    gfx_text(x + 18, y + h - 22,
             T("Labour Day", "Emek Bayrami"), PAL_TEXT_FAINT);
}

static void w_system(i32 x, i32 y, i32 w, i32 h)
{
    w_card(x, y, w, h, T("System", "Sistem"), 0x16B5A8);
    char buf[40], num[16];

    /* RAM */
    k_strcpy(buf, "RAM "); k_itoa(RAM_TOTAL_KB / 1024, num, 10);
    k_strcat(buf, num);    k_strcat(buf, " MB");
    gfx_text(x + 18, y + 60, buf, PAL_TEXT);

    /* Uptime */
    u32 H_, M_, S_; pit_uptime(&H_, &M_, &S_);
    k_strcpy(buf, T("Up ", "Calisma "));
    k_itoa(H_, num, 10); k_strcat(buf, num); k_strcat(buf, ":");
    if (M_ < 10) k_strcat(buf, "0");
    k_itoa(M_, num, 10); k_strcat(buf, num); k_strcat(buf, ":");
    if (S_ < 10) k_strcat(buf, "0");
    k_itoa(S_, num, 10); k_strcat(buf, num);
    gfx_text(x + 18, y + 78, buf, PAL_TEXT_DIM);

    /* Arch */
    gfx_text(x + 18, y + h - 38, "x86_64 long mode", PAL_TEXT_DIM);
    gfx_text(x + 18, y + h - 22,
             T("Multiboot2 boot",
               "Multiboot2 acilis"), PAL_TEXT_FAINT);

    /* CPU bar */
    i32 bar = (i32)(g_tick / 4) % 100;
    i32 bw = w - 36;
    gfx_rect_a(x + 18, y + h - 60, bw, 6, PAL_HAIRLINE, 200);
    gfx_round_rect(x + 18, y + h - 60, (bar * bw) / 100, 6, 3, PAL_ACCENT);
}

static void w_now_playing(i32 x, i32 y, i32 w, i32 h)
{
    w_card(x, y, w, h, T("Now Playing", "Calan"), 0xA45EE5);
    /* album-art square */
    gfx_round_rect(x + 18, y + 56, 56, 56, 10, 0xA45EE5);
    gfx_circle(x + 46, y + 84, 4, 0xFFFFFF);
    gfx_text(x + 88, y + 60,
             T("Aurora",       "Aurora"),       PAL_TEXT);
    gfx_text(x + 88, y + 78,
             T("Falcon Sound", "Falcon Sound"), PAL_TEXT_DIM);
    /* mini transport */
    i32 by = y + h - 32;
    gfx_circle(x + 30, by, 8, PAL_TEXT_DIM);
    gfx_circle(x + 56, by, 12, PAL_ACCENT);
    gfx_circle(x + 84, by, 8, PAL_TEXT_DIM);
    /* play triangle */
    gfx_pixel(x + 53, by - 4, 0xFFFFFF);
    for (i32 i = 0; i < 8; i++) {
        gfx_line(x + 54, by - i / 2, x + 54 + i, by, 0xFFFFFF);
    }
}

static void w_recents(i32 x, i32 y, i32 w, i32 h)
{
    w_card(x, y, w, h, T("Recents", "Son Acilanlar"), 0xF59F1A);
    const char *items[3] = { "notes.txt", "Untitled.calc", "trip-2025.cal" };
    for (i32 i = 0; i < 3; i++) {
        i32 ry = y + 60 + i * 26;
        gfx_circle(x + 24, ry + 6, 4, PAL_ACCENT);
        gfx_text(x + 36, ry, items[i], PAL_TEXT);
    }
    gfx_text(x + 18, y + h - 22,
             T("Click any in Files",
               "Files icinden ac"), PAL_TEXT_FAINT);
}

static void w_quick(i32 x, i32 y, i32 w, i32 h)
{
    w_card(x, y, w, h, T("Quick", "Hizli"), 0x16B5A8);
    /* 4 mini tile grid: F2 Launchpad, F1 Dev, Theme, Lock */
    const char *labels[4] = {
        "F2",
        "F1",
        T("Theme", "Tema"),
        T("Lock",  "Kilit"),
    };
    const char *subs[4] = {
        T("Launchpad",   "Launchpad"),
        T("Developer",   "Gelistirici"),
        T("light/dark",  "acik/koyu"),
        T("from Set...", "ayardan..."),
    };
    u32 colors[4] = { 0x3070FF, 0xA45EE5, 0xE85D9C, 0xF59F1A };
    for (i32 i = 0; i < 4; i++) {
        i32 col = i & 1, row = i >> 1;
        i32 tx = x + 14 + col * (w / 2 - 6);
        i32 ty = y + 56 + row * 56;
        gfx_round_rect_a(tx, ty, w / 2 - 22, 48, 8, colors[i], 200);
        gfx_text(tx + 10, ty + 6,  labels[i], 0xFFFFFF);
        gfx_text(tx + 10, ty + 26, subs[i],   0xFFFFFFu & 0xDDDDDD);
    }
}

/* --------------------------------------------------------------------------- */
void widgets_render(u32 frame)
{
    if (!SET.widgets_shown) return;

    i32 W = (i32)FB.width;
    i32 H = (i32)FB.height;

    /* layout: 3 columns x 2 rows, centered horizontally, anchored below the
     * menu bar (top=30+24) and above the dock (~bottom-110).               */
    i32 top    = 30 + 60;       /* below menu bar + greeting strip          */
    i32 bottom = H - 130;       /* above dock                               */
    i32 grid_h = bottom - top;
    i32 gap    = 14;

    /* card size: 3 cards across with gap */
    i32 cw = (W - 80 - 2 * gap) / 3;
    if (cw > 360) cw = 360;
    if (cw < 240) cw = 240;
    i32 ch = (grid_h - gap) / 2;
    if (ch > 220) ch = 220;
    if (ch < 160) ch = 160;

    i32 row_w = 3 * cw + 2 * gap;
    i32 x0    = (W - row_w) / 2;
    i32 y0    = top + (grid_h - (2 * ch + gap)) / 2;

    /* greeting strip ABOVE the grid */
    i32 ghy = y0 - 50;
    char hello[64];
    k_strcpy(hello, T("Hello, ", "Merhaba, "));
    k_strcat(hello, SET.owner);
    gfx_text(x0, ghy,        hello, PAL_TEXT);
    gfx_text(x0, ghy + 18,
             T("Pin apps to the desktop with P inside Launchpad.",
               "Launchpad icindeyken P ile masaustune sabitle."),
             PAL_TEXT_DIM);

    /* row 1 */
    w_weather   (x0,                     y0, cw, ch);
    w_calendar  (x0 + cw + gap,          y0, cw, ch);
    w_system    (x0 + 2*(cw+gap),        y0, cw, ch);
    /* row 2 */
    w_now_playing(x0,                    y0 + ch + gap, cw, ch);
    w_recents    (x0 + cw + gap,         y0 + ch + gap, cw, ch);
    w_quick      (x0 + 2*(cw+gap),       y0 + ch + gap, cw, ch);
}
