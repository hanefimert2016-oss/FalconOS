/* =============================================================================
 *  FalconOS — application framework (Personal kernel, v4 "Lumen")
 * -----------------------------------------------------------------------------
 *  12 self-contained mini-apps surfaced through the dock & Launchpad.  Each
 *  app has a name, dock tint, render() callback for window contents, optional
 *  input() callback, and an icon glyph drawer.
 *
 *  Built-in apps (12):
 *      Home       - quick-link cards
 *      Files      - in-memory file browser
 *      Clock      - analog dial driven by PIT uptime
 *      Stats      - live tsc / uptime / RAM
 *      About      - system info & credits
 *      Terminal   - fake POSIX-style prompt
 *      Calculator - 4-fn arithmetic (keyboard input)
 *      Settings   - toggles & accent color preview
 *      Notes      - free-form note pad (keyboard input)
 *      Calendar   - month grid with PIT-driven "today"
 *      Gallery    - color swatches for the Lumen palette
 *      Browser    - fake URL bar + bookmark cards
 * ============================================================================= */
#include "falcon.h"

static i32 active_app = -1;
static u32 open_at_ms = 0;     /* used for slide-in animation */
static i32 minimized_app = -1; /* last app sent to dock by yellow light */
static void falco_set_query(const char *q);

/* ----- window manager state (FalconOS 1) ---------------------------------
 * The dispatcher used to centre every app window on every frame. With
 * a mouse-driven WM users expect:
 *   - drag the title bar to relocate the window
 *   - resize from the bottom-right corner
 *   - traffic lights (red close / yellow minimise / green maximise)
 * We keep a single window slot since we still only have one active app
 * at a time; minimise just collapses to "no active app" but remembers
 * the offset/size so re-opening the same app feels persistent.        */
static i32  wm_dx = 0, wm_dy = 0;     /* persistent offset from centre  */
static i32  wm_dw = 0, wm_dh = 0;     /* size delta (added to default)  */
static bool wm_max = false;            /* maximised? overrides above    */
static bool wm_dragging = false;
static i32  wm_drag_grab_x = 0, wm_drag_grab_y = 0;
static bool wm_resizing = false;
static i32  wm_resize_grab_x = 0, wm_resize_grab_y = 0;
static i32  wm_resize_start_w = 0, wm_resize_start_h = 0;

void apps_open(i32 i)  { active_app = i; minimized_app = -1; open_at_ms = pit_ms(); }
void apps_close(void)  { active_app = -1; wm_max = false;
                          wm_dragging = false; wm_resizing = false; }
i32  apps_active(void) { return active_app; }
i32  apps_minimized(void) { return minimized_app; }

/* ===== icon glyphs ======================================================== */
static void icon_home(i32 cx, i32 cy)
{
    /* solid rounded square + cleaner roofline + door */
    gfx_round_rect(cx - 16, cy - 14, 32, 30, 6, PAL_ACCENT);
    /* white roof triangle */
    for (i32 dy = -14; dy <= -4; dy++) {
        i32 half = 14 + dy;            /* widens as dy moves toward -4 */
        gfx_rect(cx - half, cy + dy, half * 2 + 1, 1, 0xFFFFFF);
    }
    /* white door */
    gfx_round_rect(cx - 4, cy + 2, 8, 12, 2, 0xFFFFFF);
    gfx_pixel(cx + 2, cy + 8, PAL_ACCENT);
}
static void icon_files(i32 cx, i32 cy)
{
    /* manila folder with tab + page peeking out */
    gfx_round_rect(cx - 16, cy - 12, 14, 6, 3, 0xFFC56C);
    gfx_round_rect(cx - 16, cy -  8, 32, 22, 4, 0xFFD58A);
    gfx_round_rect(cx - 12, cy -  4, 24, 14, 3, 0xFFFFFF);
    gfx_rect(cx - 9, cy +  0, 18, 1, 0xC8AA80);
    gfx_rect(cx - 9, cy +  4, 14, 1, 0xC8AA80);
}
static void icon_clock(i32 cx, i32 cy)
{
    /* circular dial with 12 tick marks + hour & minute hands */
    gfx_circle(cx, cy, 17, PAL_TEXT);
    gfx_circle(cx, cy, 15, 0xFFFFFF);
    gfx_pixel(cx,      cy - 12, PAL_TEXT);
    gfx_pixel(cx,      cy + 12, PAL_TEXT);
    gfx_pixel(cx - 12, cy,      PAL_TEXT);
    gfx_pixel(cx + 12, cy,      PAL_TEXT);
    gfx_line(cx, cy, cx,     cy -  9, PAL_TEXT);   /* hour   */
    gfx_line(cx, cy, cx + 8, cy +  3, COL_ERR);    /* minute */
    gfx_circle(cx, cy, 2, COL_ERR);
}
static void icon_stats(i32 cx, i32 cy)
{
    /* tinted card behind stepped bars */
    gfx_round_rect(cx - 16, cy - 14, 32, 28, 5, PAL_PANEL_DEEP);
    for (i32 i = 0; i < 5; i++) {
        i32 h = 4 + (i * 3);
        u32 c = (i == 4) ? COL_OK : (i >= 2 ? PAL_ACCENT : COL_WARN);
        gfx_round_rect(cx - 12 + i * 5, cy + 9 - h, 3, h, 1, c);
    }
}
static void icon_about(i32 cx, i32 cy)
{
    /* falcon emblem — accent-tinted disc with stylised wing */
    gfx_circle(cx, cy, 17, PAL_ACCENT);
    gfx_circle(cx, cy, 14, 0xFFFFFF);
    gfx_line(cx - 8, cy + 4, cx + 4, cy - 6, PAL_ACCENT);
    gfx_line(cx - 4, cy + 4, cx + 8, cy - 2, PAL_ACCENT);
    gfx_pixel(cx + 6, cy - 4, COL_ERR);
}
static void icon_term(i32 cx, i32 cy)
{
    /* dark window with chrome strip + prompt cursor */
    gfx_round_rect(cx - 16, cy - 13, 32, 26, 4, 0x10141C);
    gfx_rect(cx - 16, cy - 13, 32, 5, 0x1B2129);
    gfx_circle(cx - 12, cy - 11, 1, COL_ERR);
    gfx_circle(cx -  8, cy - 11, 1, COL_WARN);
    gfx_circle(cx -  4, cy - 11, 1, COL_OK);
    gfx_text(cx - 12, cy - 4, ">_", COL_OK);
}
static void icon_calc(i32 cx, i32 cy)
{
    /* rounded body with screen + 3x3 keypad + accent equals */
    gfx_round_rect(cx - 15, cy - 15, 30, 30, 5, PAL_PANEL);
    gfx_round_rect(cx - 11, cy - 11, 22,  7, 2, 0x202836);
    for (i32 i = 0; i < 3; i++)
        for (i32 j = 0; j < 3; j++) {
            u32 c = (i == 2 && j == 2) ? PAL_ACCENT : PAL_TEXT_DIM;
            gfx_round_rect(cx - 11 + j * 7, cy + 0 + i * 5, 5, 3, 1, c);
        }
}
static void icon_settings(i32 cx, i32 cy)
{
    /* 8-tooth gear: outer flange ring + inner accent core */
    gfx_circle(cx, cy, 15, PAL_PANEL_DEEP);
    /* teeth around 8 cardinal positions */
    static const i32 TX[8] = { 0, 11, 14, 11,  0,-11,-14,-11 };
    static const i32 TY[8] = {-14,-11, 0, 11, 14, 11,  0,-11 };
    for (i32 a = 0; a < 8; a++)
        gfx_round_rect(cx + TX[a] - 2, cy + TY[a] - 2, 5, 5, 1, PAL_PANEL_DEEP);
    gfx_circle(cx, cy, 11, PAL_PANEL);
    gfx_circle(cx, cy,  5, PAL_ACCENT);
    gfx_circle(cx, cy,  2, 0xFFFFFF);
}
static void icon_notes(i32 cx, i32 cy)
{
    /* paper card with yellow highlight strip + ruled lines */
    gfx_round_rect(cx - 14, cy - 14, 28, 28, 4, 0xFFFFFF);
    gfx_rect(cx - 14, cy - 14, 28, 6, 0xFFE082);
    gfx_rect(cx - 11, cy -  4, 22, 1, PAL_TEXT_FAINT);
    gfx_rect(cx - 11, cy +  0, 18, 1, PAL_TEXT_FAINT);
    gfx_rect(cx - 11, cy +  4, 22, 1, PAL_TEXT_FAINT);
    gfx_rect(cx - 11, cy +  8, 14, 1, PAL_TEXT_FAINT);
}
static void icon_calendar(i32 cx, i32 cy)
{
    /* page with red header band + binding tabs + day grid */
    gfx_round_rect(cx - 14, cy - 14, 28, 28, 4, 0xFFFFFF);
    gfx_round_rect(cx - 14, cy - 14, 28,  9, 4, COL_ERR);
    gfx_rect(cx - 9, cy - 16, 2, 4, PAL_TEXT);
    gfx_rect(cx + 7, cy - 16, 2, 4, PAL_TEXT);
    for (i32 r = 0; r < 3; r++)
        for (i32 c = 0; c < 5; c++)
            gfx_round_rect(cx - 11 + c * 5, cy - 2 + r * 5, 3, 3, 1,
                            (r == 1 && c == 2) ? PAL_ACCENT : PAL_TEXT_FAINT);
}
static void icon_gallery(i32 cx, i32 cy)
{
    /* photo frame with mountains + sun */
    gfx_round_rect(cx - 16, cy - 13, 32, 26, 4, 0xFFFFFF);
    gfx_round_rect(cx - 14, cy - 11, 28, 22, 3, 0x9CC2EE);
    gfx_circle(cx + 6, cy - 5, 3, 0xFFD580);
    /* triangular mountains */
    for (i32 i = 0; i < 8; i++)
        gfx_rect(cx - 12 + i, cy + 1 - i, 3, i + 1, 0x4F6E92);
    for (i32 i = 0; i < 6; i++)
        gfx_rect(cx - 4 + i, cy + 3 - i, 3, i + 1, 0x344C6E);
}
static void icon_browser(i32 cx, i32 cy)
{
    /* Chrome-style multi-colour wheel: red / yellow / green outer ring,
     * blue centre dot. Drawn with three pie wedges + a centre fill so
     * the silhouette reads as Chrome at any size.                       */
    gfx_circle(cx, cy, 16, 0xF8F8F8);                 /* white halo     */
    gfx_circle_a(cx, cy, 14, 0xEA4335, 0xFF);         /* red top-left   */
    /* mask out top-right with yellow                                    */
    for (i32 dy = -14; dy <= 0; dy++)
        for (i32 dx = 0; dx <= 14; dx++)
            if (dx*dx + dy*dy <= 14*14)
                gfx_pixel(cx + dx, cy + dy, 0xFBBC04);
    /* mask out bottom half with green                                  */
    for (i32 dy = 1; dy <= 14; dy++)
        for (i32 dx = -14; dx <= 14; dx++)
            if (dx*dx + dy*dy <= 14*14)
                gfx_pixel(cx + dx, cy + dy, 0x34A853);
    gfx_circle(cx, cy, 6, 0x4285F4);                  /* blue centre    */
    gfx_circle_outline(cx, cy, 6, 0xFFFFFF);
}
static void icon_falco(i32 cx, i32 cy)
{
    /* Falco mark: blue dragon head in a rounded badge. */
    gfx_round_rect(cx - 16, cy - 16, 32, 32, 8, 0x163C8C);
    gfx_round_outline(cx - 16, cy - 16, 32, 32, 8, PAL_HAIRLINE);
    gfx_circle(cx - 3, cy + 2, 11, 0x2A66F5);
    gfx_circle(cx + 7, cy - 5, 7, 0x2A66F5);
    gfx_circle(cx + 11, cy - 12, 3, 0xBDE2FF);
    gfx_line(cx - 10, cy + 10, cx - 15, cy + 15, 0x123A9C);
    gfx_line(cx - 7,  cy + 5,  cx - 15, cy + 8, 0x123A9C);
}
static void icon_store(i32 cx, i32 cy)
{
    /* shopping bag with "P" */
    gfx_round_rect(cx - 12, cy - 8, 24, 18, 3, COL_PANEL);
    gfx_line(cx - 8, cy - 8, cx - 8, cy - 14, COL_PANEL);
    gfx_line(cx + 8, cy - 8, cx + 8, cy - 14, COL_PANEL);
    gfx_line(cx - 8, cy - 14, cx + 8, cy - 14, COL_PANEL);
    gfx_text_centered(cx, cy - 6, "P", COL_ACCENT);
}
static void icon_updates(i32 cx, i32 cy)
{
    /* downward refresh arrow */
    gfx_round_rect(cx - 13, cy - 12, 26, 20, 4, PAL_PANEL_DEEP);
    gfx_round_outline(cx - 13, cy - 12, 26, 20, 4, PAL_HAIRLINE);
    gfx_line(cx - 5, cy - 8, cx, cy + 8, PAL_ACCENT);
    gfx_line(cx + 5, cy - 8, cx, cy + 8, PAL_ACCENT);
    gfx_line(cx - 10, cy + 6, cx + 10, cy + 6, PAL_ACCENT);
}
static void icon_video(i32 cx, i32 cy)
{
    gfx_round_rect(cx - 16, cy - 12, 32, 24, 6, 0x10141C);
    gfx_round_outline(cx - 16, cy - 12, 32, 24, 6, PAL_HAIRLINE);
    for (i32 y = -9; y <= 9; y++)
        for (i32 x = -13; x <= 13; x++) {
            u8 a = (u8)(120 + ((x + 13) * 80) / 26);
            gfx_pixel_a(cx + x, cy + y, PAL_ACCENT, a);
        }
    for (i32 y = -5; y <= 5; y++)
        for (i32 x = -2; x <= 6; x++)
            if (x + y / 2 >= -2 && x - y / 2 <= 6)
                gfx_pixel(cx + x, cy + y, 0xFFFFFF);
}
static void icon_heroic(i32 cx, i32 cy)
{
    gfx_round_rect(cx - 16, cy - 16, 32, 32, 8, 0x1E2635);
    gfx_round_outline(cx - 16, cy - 16, 32, 32, 8, PAL_HAIRLINE);
    gfx_text_centered(cx, cy - 8, "H", PAL_ACCENT);
    gfx_rect(cx - 8, cy + 2, 16, 2, 0x34A853);
    gfx_rect(cx - 6, cy + 6, 12, 2, 0xFBBC04);
    gfx_rect(cx - 4, cy + 10, 8, 2, 0xEA4335);
}

/* ===== app render functions ============================================== */

/* shared section header */
static void section(i32 wx, i32 wy, const char *title, const char *subtitle)
{
    gfx_text(wx + 24, wy + 6,  title,    PAL_TEXT);
    gfx_text(wx + 24, wy + 28, subtitle, PAL_TEXT_DIM);
}

/* --- Home: quick-link cards ---------------------------------------------- */
static void render_home(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame;
    section(wx, wy,
            T("Welcome to FalconOS", "FalconOS'a hoş geldiniz"),
            T("Quick links", "Hızlı bağlantılar"));

    const char *c0 = T("Recent docs", "Son dosyalar");
    const char *c1 = T("System info", "Sistem bilgisi");
    const char *c2 = T("Network", "Ağ");
    const char *c3 = T("Theme", "Tema");
    const char *s0 = T("open last 5", "dosya demo");
    const char *s1 = T("uptime / RAM map", "çalışma / RAM");
    const char *s2 = T("no driver yet — virtio-net roadmap", "sürücü yok — virtio-net");
    const char *s3 = T("Liquid Glass default", "Liquid Glass varsayılan");
    const char *cards[] = { c0, c1, c2, c3 };
    const char *subs[]  = { s0, s1, s2, s3 };
    const u32   tints[]    = { PAL_ACCENT, COL_OK, COL_WARN, COL_PURPLE };

    i32 cw = (ww - 72) / 2;
    i32 ch = (wh - 100) / 2;
    for (i32 i = 0; i < 4; i++) {
        i32 r = i / 2, c = i % 2;
        i32 x = wx + 24 + c * (cw + 16);
        i32 y = wy + 60 + r * (ch + 16);
        gfx_round_rect_a(x, y, cw, ch, 12, PAL_PANEL_DEEP, 255);
        gfx_round_outline(x, y, cw, ch, 12, PAL_HAIRLINE);
        gfx_circle(x + 26, y + 24, 12, tints[i]);
        gfx_text(x + 50, y + 12, cards[i], PAL_TEXT);
        gfx_text(x + 50, y + 36, subs[i],  PAL_TEXT_DIM);
    }
}

/* --- Files: real shfs browser (definition lives after shell helpers) ---- */
static void render_files(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame);

/* --- Clock: analog dial -------------------------------------------------- */
static void render_clock(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame;
    section(wx, wy, T("Clock", "Saat"),
            T("Synced to timer IRQ0", "IRQ0 zamanlayıcı ile"));
    u32 h, m, s; pit_uptime(&h, &m, &s);

    i32 cx = wx + ww / 2;
    i32 cy = wy + wh / 2 + 10;
    i32 R  = (wh - 100) / 2;
    if (R > 130) R = 130;

    gfx_circle(cx, cy, R + 8, PAL_HAIRLINE);
    gfx_circle(cx, cy, R,     PAL_PANEL_DEEP);

    static const i8 SX[12] = {  0, 50, 87,100, 87, 50,  0,-50,-87,-100,-87,-50};
    static const i8 SY[12] = {-100,-87,-50,  0, 50, 87,100, 87, 50,   0,-50,-87};
    for (i32 i = 0; i < 12; i++) {
        i32 ex = cx + (i32)SX[i] * R / 100;
        i32 ey = cy + (i32)SY[i] * R / 100;
        gfx_circle(ex, ey, 3, PAL_TEXT_DIM);
    }

    static const i8 LX[60] = {
         0, 10, 21, 31, 41, 50, 59, 67, 74, 81, 87, 91, 95, 98, 99,100, 99, 98, 95, 91,
        87, 81, 74, 67, 59, 50, 41, 31, 21, 10,  0,-10,-21,-31,-41,-50,-59,-67,-74,-81,
       -87,-91,-95,-98,-99,-100,-99,-98,-95,-91,-87,-81,-74,-67,-59,-50,-41,-31,-21,-10};
    static const i8 LY[60] = {
       -100,-99,-98,-95,-91,-87,-81,-74,-67,-59,-50,-41,-31,-21,-10,  0, 10, 21, 31, 41,
         50, 59, 67, 74, 81, 87, 91, 95, 98, 99,100, 99, 98, 95, 91, 87, 81, 74, 67, 59,
         50, 41, 31, 21, 10,  0,-10,-21,-31,-41,-50,-59,-67,-74,-81,-87,-91,-95,-98,-99};

    i32 si = (i32)(s % 60);
    i32 mi = (i32)(m % 60);
    i32 hi = (i32)(((h % 12) * 5 + m / 12) % 60);

    gfx_line(cx, cy, cx + (i32)LX[hi] * (R - 40) / 100,
                      cy + (i32)LY[hi] * (R - 40) / 100, PAL_TEXT);
    gfx_line(cx, cy, cx + (i32)LX[mi] * (R - 16) / 100,
                      cy + (i32)LY[mi] * (R - 16) / 100, PAL_ACCENT);
    gfx_line(cx, cy, cx + (i32)LX[si] * (R -  8) / 100,
                      cy + (i32)LY[si] * (R -  8) / 100, COL_ERR);
    gfx_circle(cx, cy, 5, PAL_TEXT);

    char buf[16] = "00:00:00";
    char tmp[8];
    k_itoa(h, tmp, 10); if (h < 10) { buf[0]='0'; buf[1]=tmp[0]; } else { buf[0]=tmp[0]; buf[1]=tmp[1]; }
    k_itoa(m, tmp, 10); if (m < 10) { buf[3]='0'; buf[4]=tmp[0]; } else { buf[3]=tmp[0]; buf[4]=tmp[1]; }
    k_itoa(s, tmp, 10); if (s < 10) { buf[6]='0'; buf[7]=tmp[0]; } else { buf[6]=tmp[0]; buf[7]=tmp[1]; }
    buf[8]=0;
    gfx_text_centered(cx, cy + R + 24, buf, PAL_TEXT);
}

/* --- Stats --------------------------------------------------------------- */
static void render_stats(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame; (void)wh;
    section(wx, wy, T("Stats", "İstatistik"),
            T("live telemetry", "Canlı sistem telemetrisi"));

    u64 t = rdtsc();
    u32 hi = (u32)(t >> 32), lo = (u32)t;
    u32 h, m, s; pit_uptime(&h, &m, &s);

    char hex[22], mib[28], line[96];

    k_strcpy(line, "tsc       0x");
    k_itoa(hi, hex, 16); k_strcat(line, hex);
    k_itoa(lo, hex, 16); k_strcat(line, hex);
    gfx_text(wx + 24, wy + 60, line, PAL_TEXT);

    char th[8], tm[8], ts[8];
    k_itoa(h, th, 10); k_itoa(m, tm, 10); k_itoa(s, ts, 10);
    k_strcpy(line, T("uptime    ", "çalışma   "));
    k_strcat(line, th); k_strcat(line, "h ");
    k_strcat(line, tm); k_strcat(line, "m ");
    k_strcat(line, ts); k_strcat(line, "s");
    gfx_text(wx + 24, wy + 86, line, PAL_TEXT);

    k_strcpy(line, T("ticks     ", "tik       "));
    k_itoa(g_ticks, hex, 10); k_strcat(line, hex);
    gfx_text(wx + 24, wy + 110, line, PAL_TEXT);

    u64 mib_val = RAM_TOTAL_BYTES / ((u64)1024 * 1024);
    k_u64_to_dec(mib_val, mib);
    k_strcpy(line, T("ram (mmap)", "RAM (mmap"));
    k_strcat(line, ") ");
    k_strcat(line, mib);
    k_strcat(line, T(" MiB", " MiB"));
    gfx_text(wx + 24, wy + 134, line, PAL_TEXT);

    k_strcpy(line, T("screen    ", "ekran    "));
    k_itoa(FB.width, hex, 10); k_strcat(line, hex); k_strcat(line, "x");
    k_itoa(FB.height, hex, 10); k_strcat(line, hex); k_strcat(line, "@");
    k_itoa(FB.bpp, hex, 10); k_strcat(line, hex);
    gfx_text(wx + 24, wy + 158, line, PAL_TEXT);

    /* pulse bar */
    i32 pw = (i32)((g_ticks % 100) * (ww - 48) / 100);
    gfx_round_rect(wx + 24, wy + 210, ww - 48, 6, 3, PAL_HAIRLINE);
    gfx_round_rect(wx + 24, wy + 210, pw,      6, 3, PAL_ACCENT);
}

/* --- About --------------------------------------------------------------- */
static void render_about(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame; (void)wh;
    section(wx, wy, T("About FalconOS", "FalconOS Hakkında"),
            T("Version & credits", "Sürüm ve katkılar"));

    i32 cx = wx + ww / 2;
    gfx_circle(cx, wy + 110, 56, PAL_ACCENT);
    gfx_circle(cx, wy + 110, 36, PAL_PANEL);
    gfx_circle(cx, wy + 110, 18, PAL_ACCENT);

    gfx_text_centered(cx, wy + 190, "FalconOS 1",                            PAL_TEXT);
    gfx_text_centered(cx, wy + 210,
#if ARCH_x86_64
        "bare-metal x86_64 — personal + developer",
#else
        "bare-metal i386 — personal + developer",
#endif
                      PAL_ACCENT);
    gfx_text_centered(cx, wy + 232,
        T("two kernels, one binary - F1 to flip - F2 Launchpad",
          "iki çekirdek, tek ikili - F1 ile geç - F2 Launchpad"),
        PAL_TEXT_DIM);

    /* Native subsystems strip */
    char buf[80];
    k_strcpy(buf, T("Storage: ", "Depolama: "));
    k_strcat(buf, linux_compat_summary());
    gfx_text_centered(cx, wy + 256, buf, PAL_TEXT_DIM);

    char hidsum[80];
    hid_keymap_dump(hidsum, sizeof hidsum);
    gfx_text_centered(cx, wy + 274, hidsum, PAL_TEXT_FAINT);

    gfx_text_centered(cx, wy + 296,
        T("bare-metal microkernel  -  prg packages  -  Store  -  POSIX shell",
          "bare-metal mikroçekirdek  -  prg paketleri  -  Mağaza  -  POSIX kabuk"),
        PAL_TEXT_FAINT);
}

/* --- Sistem Güncellemeleri (offline channel UI) ---------------------------- */
static void render_updates(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame;
    section(wx, wy,
            T("System updates", "Sistem güncellemeleri"),
            T("prg catalogue + Terminal bridge", "prg katalogu + Terminal köprüsü"));

    char buf[192], num[14];
    k_strcpy(buf, T("Installed packages: ", "Kurulu paket sayısı: "));
    k_itoa((u32)prg_installed_count(), num, 10); k_strcat(buf, num);
    k_strcat(buf, T(" / ", " / "));
    k_itoa((u32)prg_count(), num, 10); k_strcat(buf, num);
    gfx_text(wx + 24, wy + 60, buf, PAL_TEXT);

    k_strcpy(buf, T("FalconFS: ", "FalconFS: "));
    k_strcat(buf, diskdb_present()
                    ? T("superblock on ATA.", "ATA üzerinde süper blok.")
                    : T("no superblock (güvenli oturum or no disk).",
                        "süper blok yok (güvenli oturum veya disk yok)."));
    gfx_text(wx + 24, wy + 88, buf, PAL_TEXT_DIM);

    gfx_text(wx + 24, wy + 124,
             T("Online updates need virtio-net + TLS (planned).",
               "Çevrimiçi güncelleme virtio-net + TLS gerektirir (planlanıyor)."),
             PAL_TEXT_DIM);
    gfx_text(wx + 24, wy + 152,
             T("Terminal:  update check  |  update apply",
               "Terminal:  update check  |  update apply"),
             PAL_TEXT_FAINT);

    (void)ww; (void)wh;
}

static void updates_input_key(i32 key) { (void)key; }

/* --- Store (prg package browser, GUI) ----------------------------------- */
static i32 store_cursor = 0;
static i32 store_filter = 0;     /* 0 all, 1 installed */

static bool store_pkg_visible(i32 pkg_i)
{
    if (store_filter == 0) return true;
    return prg_is_installed(pkg_i);
}

static i32 store_visible_count(void)
{
    i32 n = 0;
    for (i32 i = 0; i < prg_count(); i++) if (store_pkg_visible(i)) n++;
    return n;
}

static i32 store_visible_to_pkg(i32 vis_i)
{
    if (vis_i < 0) return -1;
    i32 seen = 0;
    for (i32 i = 0; i < prg_count(); i++) {
        if (!store_pkg_visible(i)) continue;
        if (seen == vis_i) return i;
        seen++;
    }
    return -1;
}

static i32 store_pkg_to_visible(i32 pkg_i)
{
    if (pkg_i < 0) return -1;
    i32 vis = 0;
    for (i32 i = 0; i < prg_count(); i++) {
        if (!store_pkg_visible(i)) continue;
        if (i == pkg_i) return vis;
        vis++;
    }
    return -1;
}

static void store_clamp_cursor(void)
{
    i32 n = store_visible_count();
    if (n <= 0) { store_cursor = 0; return; }
    if (store_cursor < 0) store_cursor = 0;
    if (store_cursor >= n) store_cursor = n - 1;
}

static void store_set_filter(i32 new_filter)
{
    if (new_filter < 0 || new_filter > 1) return;
    i32 keep_pkg = store_visible_to_pkg(store_cursor);
    store_filter = new_filter;
    if (keep_pkg >= 0) {
        i32 v = store_pkg_to_visible(keep_pkg);
        store_cursor = (v >= 0) ? v : 0;
    }
    store_clamp_cursor();
}

static void store_input_key(i32 key)
{
    store_clamp_cursor();
    if (key == KEY_LEFT)  { store_set_filter(0); return; }
    if (key == KEY_RIGHT) { store_set_filter(1); return; }

    i32 n = store_visible_count();
    if (n <= 0) return;

    if (key == KEY_UP   && store_cursor > 0) store_cursor--;
    if (key == KEY_DOWN && store_cursor < n - 1) store_cursor++;

    i32 pkg_i = store_visible_to_pkg(store_cursor);
    if (pkg_i < 0) return;
    if (key == KEY_ENTER || key == ' ' || key == 'i' || key == 'I') {
        prg_install(pkg_i);
    }
    if (key == 'r' || key == 'R' || key == KEY_BACKSPACE) {
        prg_remove(pkg_i);
    }
}

static void render_store(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame;
    section(wx, wy,
            T("Store",  "Mağaza"),
            T("up/down pick  Enter install  R remove  mouse: click row/action",
              "yukarı/aşağı seç  Enter yükle  R kaldır  fare: satır/eylem tıkla"));

    store_clamp_cursor();

    char hdr[80], num[12];
    k_strcpy(hdr, T("packages: ", "paket: "));
    k_itoa((u32)prg_installed_count(), num, 10);
    k_strcat(hdr, num);
    k_strcat(hdr, "/");
    k_itoa((u32)prg_count(), num, 10);
    k_strcat(hdr, num);
    k_strcat(hdr, T("  installed", "  kurulu"));
    gfx_text(wx + 24, wy + 38, hdr, PAL_TEXT_DIM);

    /* filter chips: all / installed */
    const char *chip_all  = T("all", "tümü");
    const char *chip_inst = T("installed", "kurulu");
    i32 chip_y  = wy + 54;
    i32 all_w   = gfx_text_width(chip_all) + 20;
    i32 inst_w  = gfx_text_width(chip_inst) + 20;
    i32 all_x   = wx + 24;
    i32 inst_x  = all_x + all_w + 10;
    bool all_on = (store_filter == 0);

    gfx_round_rect_a(all_x, chip_y, all_w, 20, 10, all_on ? PAL_ACCENT_DIM : PAL_PANEL_DEEP, 255);
    gfx_round_outline(all_x, chip_y, all_w, 20, 10, all_on ? PAL_ACCENT : PAL_HAIRLINE);
    gfx_text(all_x + 10, chip_y + 5, chip_all, all_on ? PAL_ACCENT : PAL_TEXT_DIM);
    gfx_round_rect_a(inst_x, chip_y, inst_w, 20, 10, all_on ? PAL_PANEL_DEEP : PAL_ACCENT_DIM, 255);
    gfx_round_outline(inst_x, chip_y, inst_w, 20, 10, all_on ? PAL_HAIRLINE : PAL_ACCENT);
    gfx_text(inst_x + 10, chip_y + 5, chip_inst, all_on ? PAL_TEXT_DIM : PAL_ACCENT);

    /* Two-pane layout: left = package list (40%), right = detail card.
     * Pardus / GNOME Software / KDE Discover all have a hero banner +
     * description + Install button in a single card. We synthesise the
     * banner from the package category palette + a large glyph because
     * shipping per-package PNGs would multiply the ISO size and pull a
     * PNG decoder into the kernel.                                    */
    i32 list_w = (ww - 48) * 5 / 12;     /* ~42% of usable width */
    if (list_w < 240) list_w = 240;
    i32 lx = wx + 24, ly = wy + 82, lw = list_w;
    i32 detail_x = lx + lw + 16;
    i32 detail_w = ww - 24 - detail_x + wx;
    if (detail_w < 220) detail_w = 220;
    i32 row_h = 32;
    i32 visible_total = store_visible_count();
    i32 visible = (wh - 112) / row_h;
    if (visible < 1) visible = 1;
    i32 first = store_cursor - visible / 2;
    if (first < 0) first = 0;
    if (first > visible_total - visible) first = visible_total - visible;
    if (first < 0) first = 0;

    i32 mx, my; bool ml;
    mouse_get(&mx, &my, &ml);
    (void)ml;
    bool edge = mouse_peek_click();
    bool click_used = false;

    if (edge && mx >= all_x && mx <= all_x + all_w && my >= chip_y && my <= chip_y + 20) {
        store_set_filter(0); click_used = true;
    } else if (edge && mx >= inst_x && mx <= inst_x + inst_w &&
               my >= chip_y && my <= chip_y + 20) {
        store_set_filter(1); click_used = true;
    }

    if (visible_total <= 0) {
        gfx_round_rect_a(lx, ly, lw, 28, 8, PAL_PANEL_DEEP, 255);
        gfx_round_outline(lx, ly, lw, 28, 8, PAL_HAIRLINE);
        gfx_text(lx + 10, ly + 8,
                 T("no packages in this filter", "bu filtrede paket yok"),
                 PAL_TEXT_DIM);
        if (click_used) (void)mouse_consume_click();
        return;
    }

    for (i32 v = first; v < first + visible && v < visible_total; v++) {
        i32 i = store_visible_to_pkg(v);
        const prg_pkg_t *p = prg_at(i);
        i32 y = ly + (v - first) * row_h;
        bool active = (v == store_cursor);
        gfx_round_rect_a(lx, y, lw, row_h - 4, 8,
                         active ? PAL_ACCENT_DIM : PAL_PANEL_DEEP, 255);
        gfx_round_outline(lx, y, lw, row_h - 4, 8,
                          active ? PAL_ACCENT : PAL_HAIRLINE);
        /* category dot */
        u32 cat_color = COL_OK;
        if (p->category[0] == 'd') cat_color = COL_WARN;          /* drivers */
        else if (p->category[0] == 't') cat_color = 0xE85D9C;     /* themes  */
        else if (p->category[0] == 'l') cat_color = 0x16B5A8;     /* libs    */
        else if (p->category[0] == 'g') cat_color = COL_PURPLE;   /* games   */
        else if (p->category[0] == 'c') cat_color = 0x3070FF;     /* compat  */
        gfx_circle(lx + 14, y + 14, 5, cat_color);
        /* name only — summary moves to right pane */
        gfx_text(lx + 30, y + 8, p->name, PAL_TEXT);
        /* installed checkmark on the right edge of the row */
        bool inst_l = prg_is_installed(i);
        if (inst_l) {
            gfx_text(lx + lw - 22, y + 8,
                     p->builtin ? "*" : "v", inst_l ? COL_OK : PAL_TEXT_FAINT);
        }

        if (edge && !click_used &&
            mx >= lx && mx <= lx + lw && my >= y && my <= y + row_h - 4) {
            store_cursor = v;
            click_used = true;
        }
    }

    /* ---- right pane: synthetic Pardus-style detail card ---------------- */
    {
        const prg_pkg_t *p = prg_at(store_visible_to_pkg(store_cursor));
        i32 dy = ly;
        i32 dh = wh - 112;

        /* card body */
        gfx_round_rect_a(detail_x, dy, detail_w, dh, 12, PAL_PANEL_DEEP, 255);
        gfx_round_outline(detail_x, dy, detail_w, dh, 12, PAL_HAIRLINE);

        /* hero banner: 96 px tall gradient strip per category */
        u32 hero = 0x3070FF;
        char hero_glyph = '?';
        if (p->category[0] == 'd') { hero = 0xE9A341; hero_glyph = 'D'; }
        else if (p->category[0] == 't') { hero = 0xE85D9C; hero_glyph = 'T'; }
        else if (p->category[0] == 'l') { hero = 0x16B5A8; hero_glyph = 'L'; }
        else if (p->category[0] == 'g') { hero = 0xA45EE5; hero_glyph = 'G'; }
        else if (p->category[0] == 'c') { hero = 0x3070FF; hero_glyph = 'C'; }
        else if (p->category[0] == 'a') { hero = 0x2BB673; hero_glyph = 'A'; }
        else if (p->category[0] == 'f') { hero = 0xC79624; hero_glyph = 'F'; }
        else if (p->category[0] == 's') { hero = 0xCB4B43; hero_glyph = 'S'; }
        gfx_round_rect_a(detail_x + 8, dy + 8, detail_w - 16, 96, 10, hero, 255);
        /* faux gradient: top half lighter, bottom darker — single-pass         */
        gfx_rect_a(detail_x + 8, dy + 8, detail_w - 16, 30, 0xFFFFFF, 32);
        gfx_rect_a(detail_x + 8, dy + 80, detail_w - 16, 24, 0x000000, 32);
        /* hero glyph (huge letter) — anchors the banner like the Pardus       *
         * "ribbon" cards do. Centred horizontally with a soft white halo.    */
        i32 glyph_x = detail_x + 32;
        for (i32 k = 0; k < 32; k++) {
            i32 thickness = 32 - k;
            (void)thickness;
        }
        char gs[2]; gs[0] = hero_glyph; gs[1] = 0;
        gfx_text(glyph_x, dy + 36, gs, 0xFFFFFF);
        /* package name overlaid right of the glyph */
        gfx_text(glyph_x + 32, dy + 24, p->name, 0xFFFFFF);
        gfx_text(glyph_x + 32, dy + 44, p->version, 0xFFFFFF);
        gfx_text(glyph_x + 32, dy + 64, p->category, 0xFFFFFF);

        /* description */
        gfx_text(detail_x + 16, dy + 120, p->summary, PAL_TEXT);

        /* metadata strip */
        char meta[80], buf[12];
        k_strcpy(meta, T("size: ", "boyut: "));
        k_itoa(p->size_kb, buf, 10);
        k_strcat(meta, buf);
        k_strcat(meta, T(" KB", " KB"));
        gfx_text(detail_x + 16, dy + 144, meta, PAL_TEXT_DIM);
        if (p->depends && p->depends[0]) {
            char dep[160];
            k_strcpy(dep, T("requires: ", "gerek: "));
            k_strcat(dep, p->depends);
            gfx_text(detail_x + 16, dy + 160, dep, PAL_TEXT_DIM);
        }

        /* big install/remove button */
        bool installed = prg_is_installed(store_visible_to_pkg(store_cursor));
        const char *btn =
            p->builtin   ? T("built-in",  "yerleşik") :
            installed    ? T("Remove",    "Kaldır")   :
                           T("Install",   "Kur");
        u32 bc = p->builtin ? PAL_PANEL_HI : (installed ? 0xEBC9C8 : PAL_ACCENT);
        u32 tc = p->builtin ? PAL_TEXT_DIM : 0xFFFFFF;
        i32 bw = 140;
        i32 bx = detail_x + detail_w - bw - 16;
        i32 by = dy + dh - 44;
        gfx_round_rect_a(bx, by, bw, 28, 10, bc, 255);
        gfx_round_outline(bx, by, bw, 28, 10, PAL_HAIRLINE);
        gfx_text(bx + (bw - gfx_text_width(btn)) / 2, by + 8, btn, tc);

        if (edge && !click_used && !p->builtin &&
            mx >= bx && mx <= bx + bw && my >= by && my <= by + 28) {
            i32 pkg_i = store_visible_to_pkg(store_cursor);
            if (pkg_i >= 0) {
                if (installed) (void)prg_remove(pkg_i);
                else           (void)prg_install(pkg_i);
            }
            click_used = true;
        }
    }

    if (click_used) (void)mouse_consume_click();
}

/* --- Terminal (POSIX shell subset, FalconOS 1.1) -------------------------
 * A compact, real shell with a 60+ command vocabulary that mirrors the
 * GNU coreutils + util-linux command surface most users reach for first.
 * All commands operate against a 16-slot RAM-backed flat filesystem
 * ("shfs") so behaviour is real (mkdir/rm/cp/mv/tee actually mutate
 * state, head/tail/wc/sort/uniq/grep/tr/cut produce the expected output)
 * within the BSS budget.
 *
 *   file ops      pwd cd ls cat head tail wc sort uniq grep tr cut tee
 *                 find rm touch cp mv mkdir rmdir basename dirname
 *                 more less xxd hexdump file
 *   text/string   echo printf yes seq expr test [ true false
 *   shell         env set unset export alias history clear help exit
 *                 if/then/fi for/in/do/done | > >>
 *   process       ps top jobs kill (best-effort, single-task kernel)
 *   user / sys    uname whoami id groups who w hostname uptime date cal
 *   storage       df du free mount lsblk
 *   power         reboot shutdown poweroff
 *
 * Commands return shell exit status (0 = success).  Unknown commands
 * land in the "command not found" branch identical to bash.            */
#define TERM_LINES 12
#define TERM_COLS  80
#define SH_VARS    16
#define SH_FILES   80          /* hierarchical entries (dirs + files)         */
#define SH_FBYTES  512
#define SH_PATHLEN 64          /* absolute path length per entry              */

/* shfs entry: every entry stores an ABSOLUTE path.  is_dir distinguishes
 * directories (no payload) from files.  ls/cd walk this flat array but
 * filter by parent path, giving a real hierarchy without a tree.        */
typedef struct {
    char name[SH_PATHLEN];     /* full absolute path, e.g. /home/falcon/x.txt */
    u32  len;                  /* file payload length                          */
    char data[SH_FBYTES];      /* file payload (ignored for dirs)              */
    bool is_dir;               /* true = directory, false = regular file       */
    bool used;                 /* slot occupancy                               */
} shfile_t;
typedef struct { char name[16]; char value[64]; bool used; } shvar_t;

static char term_buf[TERM_LINES][TERM_COLS];
static i32  term_init_done = 0;
static i32  term_input_len = 0;
static char term_input[TERM_COLS];
static char sh_cwd[SH_PATHLEN] = "/home/falcon";
static shfile_t sh_files[SH_FILES];
static shvar_t  sh_vars [SH_VARS];

static void term_push(const char *s)
{
    for (i32 i = 1; i < TERM_LINES; i++) k_strcpy(term_buf[i - 1], term_buf[i]);
    k_strcpy(term_buf[TERM_LINES - 1], "");
    /* truncate-copy */
    char *d = term_buf[TERM_LINES - 1]; i32 n = 0;
    while (s[n] && n < TERM_COLS - 1) { d[n] = s[n]; n++; }
    d[n] = 0;
}

static void shfs_seed_dir(i32 *slot, const char *path)
{
    if (*slot >= SH_FILES) return;
    shfile_t *f = &sh_files[*slot];
    k_strcpy(f->name, path);
    f->len    = 0;
    f->data[0] = 0;
    f->is_dir = true;
    f->used   = true;
    (*slot)++;
}
static void shfs_seed_file(i32 *slot, const char *path, const char *body)
{
    if (*slot >= SH_FILES) return;
    shfile_t *f = &sh_files[*slot];
    k_strcpy(f->name, path);
    i32 i = 0;
    while (body[i] && i < SH_FBYTES - 1) { f->data[i] = body[i]; i++; }
    f->data[i] = 0;
    f->len    = (u32)i;
    f->is_dir = false;
    f->used   = true;
    (*slot)++;
}

static void term_init(void)
{
    if (term_init_done) return;
    term_init_done = 1;
    for (i32 i = 0; i < TERM_LINES; i++) term_buf[i][0] = 0;
    term_push(T("FalconOS shell — cd/ls/cat/echo/if/for/|/ >",
                "FalconOS kabuğu — cd/ls/cat/echo/if/for/| / >"));
    term_push(T("Type 'help', 'hwinfo', or open Mağaza for prg.",
                "'help', 'hwinfo' yazın; paketler için Mağaza."));
    term_push("");
    /* Seed a hierarchical filesystem.  Entries are flat-stored but every
     * path is absolute, so ls/cd can scope listings to a parent.        */
    i32 slot = 0;
    shfs_seed_dir (&slot, "/");
    shfs_seed_dir (&slot, "/home");
    shfs_seed_dir (&slot, "/home/falcon");
    shfs_seed_dir (&slot, "/home/falcon/Documents");
    shfs_seed_dir (&slot, "/home/falcon/Downloads");
    shfs_seed_dir (&slot, "/home/falcon/Pictures");
    shfs_seed_dir (&slot, "/tmp");
    shfs_seed_dir (&slot, "/etc");
    shfs_seed_dir (&slot, "/usr");
    shfs_seed_dir (&slot, "/var");
    shfs_seed_dir (&slot, "/var/prg");
    shfs_seed_file(&slot, "/home/falcon/readme.txt",
        "FalconOS 1 — hoş geldiniz / welcome.\n\n"
        "Bu kabuk POSIX komutlarının bir alt kümesini anlar; "
        "The shell understands a POSIX subset: pwd cd ls cat mkdir rmdir "
        "touch rm cp mv echo printf head tail wc grep sort.\n\n"
        "Try: cd Documents; touch notes.md; echo hi > notes.md; cat notes.md\n");
    shfs_seed_file(&slot, "/home/falcon/hello.sh",
        "echo Merhaba FalconOS — hello FalconOS\n");
    shfs_seed_file(&slot, "/etc/hostname", "falcon\n");
    shfs_seed_file(&slot, "/etc/issue",    "FalconOS 1 (bare-metal x86_64)\n");
}

static shvar_t *sh_var_find(const char *n)
{
    for (i32 i = 0; i < SH_VARS; i++)
        if (sh_vars[i].used && k_strcmp(sh_vars[i].name, n) == 0)
            return &sh_vars[i];
    return 0;
}
static void sh_var_set(const char *n, const char *v)
{
    shvar_t *s = sh_var_find(n);
    if (!s) {
        for (i32 i = 0; i < SH_VARS; i++) if (!sh_vars[i].used) { s = &sh_vars[i]; break; }
    }
    if (!s) return;
    s->used = true;
    k_strcpy(s->name, n);
    /* truncate to 63 */
    i32 i = 0;
    while (v[i] && i < 63) { s->value[i] = v[i]; i++; }
    s->value[i] = 0;
}
static shfile_t *sh_file_find(const char *n)
{
    for (i32 i = 0; i < SH_FILES; i++)
        if (sh_files[i].used && k_strcmp(sh_files[i].name, n) == 0)
            return &sh_files[i];
    return 0;
}
static shfile_t *sh_file_open_w(const char *n, bool append)
{
    shfile_t *f = sh_file_find(n);
    if (!f) {
        for (i32 i = 0; i < SH_FILES; i++) if (!sh_files[i].used) { f = &sh_files[i]; break; }
        if (!f) return 0;
        f->used = true;
        k_strcpy(f->name, n);
        f->len = 0;
        f->data[0] = 0;
        f->is_dir = false;
    } else if (!append) {
        f->len = 0;
        f->data[0] = 0;
    }
    return f;
}

/* ----- path helpers ---------------------------------------------------- *
 *  sh_path_join: produce an absolute, normalised path from a possibly
 *  relative arg.  Handles "/abs", "rel", "..", ".".  Always writes a
 *  null-terminated result of at most SH_PATHLEN-1 characters.
 *  sh_path_parent: parent directory of an absolute path ("/a/b" -> "/a",
 *  "/" -> "/").
 *  sh_path_basename: tail component ("/a/b" -> "b", "/" -> "/").     */
static void sh_path_join(const char *arg, char *out)
{
    char tmp[SH_PATHLEN * 2];
    /* 1. seed tmp with cwd or "/" if arg is absolute */
    if (arg[0] == '/') {
        i32 i = 0;
        while (arg[i] && i < (i32)sizeof tmp - 1) { tmp[i] = arg[i]; i++; }
        tmp[i] = 0;
    } else {
        i32 i = 0;
        while (sh_cwd[i] && i < (i32)sizeof tmp - 1) { tmp[i] = sh_cwd[i]; i++; }
        if (i > 0 && tmp[i - 1] != '/' && i < (i32)sizeof tmp - 1) tmp[i++] = '/';
        i32 j = 0;
        while (arg[j] && i < (i32)sizeof tmp - 1) { tmp[i++] = arg[j++]; }
        tmp[i] = 0;
    }
    /* 2. normalise by walking components, handling . and .. */
    char comp[SH_PATHLEN];
    i32  ci = 0;
    char stack[SH_PATHLEN];
    i32  si = 0;
    stack[0] = 0;
    for (i32 i = 0; ; i++) {
        char c = tmp[i];
        if (c == '/' || c == 0) {
            comp[ci] = 0;
            if (ci == 0 || (ci == 1 && comp[0] == '.')) {
                /* skip empty / . */
            } else if (ci == 2 && comp[0] == '.' && comp[1] == '.') {
                while (si > 0 && stack[si - 1] != '/') si--;
                if (si > 0) si--;
            } else {
                if (si + 1 + ci >= (i32)sizeof stack) break;
                stack[si++] = '/';
                for (i32 k = 0; k < ci; k++) stack[si++] = comp[k];
            }
            ci = 0;
            if (c == 0) break;
        } else if (ci < (i32)sizeof comp - 1) {
            comp[ci++] = c;
        }
    }
    if (si == 0) { out[0] = '/'; out[1] = 0; return; }
    stack[si] = 0;
    i32 k = 0;
    while (stack[k] && k < SH_PATHLEN - 1) { out[k] = stack[k]; k++; }
    out[k] = 0;
}
static void sh_path_parent(const char *p, char *out)
{
    i32 n = k_strlen(p);
    while (n > 1 && p[n - 1] != '/') n--;
    if (n > 1) n--; /* drop trailing slash unless it's the root */
    if (n == 0) { out[0] = '/'; out[1] = 0; return; }
    for (i32 i = 0; i < n; i++) out[i] = p[i];
    out[n] = 0;
}
static const char *sh_path_basename(const char *p)
{
    const char *b = p;
    for (i32 i = 0; p[i]; i++) if (p[i] == '/') b = &p[i + 1];
    if (*b == 0) return "/";
    return b;
}
static shfile_t *sh_resolve(const char *arg)
{
    char abs[SH_PATHLEN];
    sh_path_join(arg, abs);
    return sh_file_find(abs);
}

/* --- prg install receipts live under /var/prg/r<idx> ----------------- */
static void pkg_receipt_fname(i32 idx, char name[SH_PATHLEN])
{
    char num[12];
    k_itoa((u32)idx, num, 10);
    k_strcpy(name, "/var/prg/r");
    k_strcat(name, num);
}

void apps_pkg_on_install(i32 idx)
{
    const prg_pkg_t *p = prg_at(idx);
    if (!p || p->builtin) return;
    char fn[SH_PATHLEN];
    pkg_receipt_fname(idx, fn);
    shfile_t *f = sh_file_open_w(fn, false);
    if (!f) return;
    k_strcpy(f->data, "package ");
    k_strcat(f->data, p->name);
    k_strcat(f->data, "\nver ");
    k_strcat(f->data, p->version);
    k_strcat(f->data, "\n");
    k_strcat(f->data, p->summary);
    f->len = k_strlen(f->data);
    if (f->len >= SH_FBYTES) {
        f->data[SH_FBYTES - 1] = 0;
        f->len = SH_FBYTES - 1;
    }
}

void apps_pkg_on_remove(i32 idx)
{
    char fn[SH_PATHLEN];
    pkg_receipt_fname(idx, fn);
    shfile_t *f = sh_file_find(fn);
    if (f) {
        f->used = false;
        f->name[0] = 0;
        f->len     = 0;
    }
}

void apps_pkg_sync_receipts_from_state(void)
{
    for (i32 i = 0; i < prg_count(); i++) {
        const prg_pkg_t *p = prg_at(i);
        if (!p || p->builtin) continue;
        if (prg_is_installed(i)) apps_pkg_on_install(i);
        else                     apps_pkg_on_remove(i);
    }
}

/* small helpers --------------------------------------------------------- */
static bool sh_isspace(char c) { return c == ' ' || c == '\t'; }
static char sh_tolower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c; }

static bool sh_streq_ci(const char *a, const char *b)
{
    i32 i = 0;
    while (a[i] && b[i]) {
        if (sh_tolower(a[i]) != sh_tolower(b[i])) return false;
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

static bool sh_contains_ci(const char *hay, const char *needle)
{
    if (!needle || !needle[0]) return true;
    for (i32 i = 0; hay[i]; i++) {
        i32 j = 0;
        while (needle[j] && hay[i + j] &&
               sh_tolower(hay[i + j]) == sh_tolower(needle[j])) j++;
        if (!needle[j]) return true;
    }
    return false;
}

static i32 sh_find_pkg_idx(const char *name)
{
    for (i32 i = 0; i < prg_count(); i++) {
        const prg_pkg_t *p = prg_at(i);
        if (p && sh_streq_ci(p->name, name)) return i;
    }
    return -1;
}

static i32 sh_find_app_idx(const char *name)
{
    if (!name || !name[0]) return -1;
    for (i32 i = 0; i < apps_count(); i++)
        if (sh_streq_ci(apps_name(i), name)) return i;
    if (sh_streq_ci(name, "updates") || sh_streq_ci(name, "güncelleme") ||
        sh_streq_ci(name, "guncelleme") ||
        sh_streq_ci(name, "sistem güncellemeleri") ||
        sh_streq_ci(name, "sistem-güncellemeleri"))
        return sh_find_app_idx("Sistem Güncellemeleri");
    if (sh_streq_ci(name, "google-chrome") || sh_streq_ci(name, "googlechrome"))
        return sh_find_app_idx("Chrome");
    if (sh_streq_ci(name, "heroic-launcher") || sh_streq_ci(name, "heroiclauncher"))
        return sh_find_app_idx("Heroic");
    if (sh_streq_ci(name, "media") || sh_streq_ci(name, "video-player"))
        return sh_find_app_idx("Video");
    if (sh_streq_ci(name, "falco") || sh_streq_ci(name, "falco-browser") ||
        sh_streq_ci(name, "browser"))
        return sh_find_app_idx("Falco");

    static const struct { const char *alias; i32 idx; } TR_ALIAS[] = {
        { "ana sayfa", 0 },       { "anasayfa", 0 },
        { "dosyalar", 1 },      { "dosya", 1 },
        { "mağaza", 2 },        { "magaza", 2 },
        { "ayarlar", 3 },
        { "terminal", 5 },
        { "hesap makinesi", 6 }, { "hesap", 6 },
        { "notlar", 7 },        { "not defteri", 7 },
        { "saat", 8 },
        { "istatistik", 9 },
        { "takvim", 10 },
        { "galeri", 11 },
        { "video", 12 },
        { "falco tarayıcı", 13 }, { "falco tarayici", 13 },
        { "asistan", 16 },       { "yardımcı", 16 },
        { "hakkında", 17 },      { "hakkinda", 17 },
    };
    for (u32 kk = 0; kk < sizeof TR_ALIAS / sizeof TR_ALIAS[0]; kk++) {
        if (sh_streq_ci(name, TR_ALIAS[kk].alias))
            return TR_ALIAS[kk].idx;
    }
    return -1;
}

static void sh_buf_pop_utf8(char *buf, i32 *len)
{
    if (!buf || !len || *len <= 0) return;
    i32 i = *len - 1;
    while (i > 0 && (((u8)buf[i] & 0xC0u) == 0x80u)) i--;
    buf[i] = 0;
    *len = i;
}

static bool sh_buf_append_key(char *buf, i32 *len, i32 cap, i32 key)
{
    char utf[4];
    i32 n = key_to_utf8(key, utf);
    if (n <= 0 || !buf || !len) return false;
    if (*len + n >= cap) return false;
    for (i32 i = 0; i < n; i++) buf[*len + i] = utf[i];
    *len += n;
    buf[*len] = 0;
    return true;
}

static i32 sh_utf8_chars_between(const char *buf, i32 from, i32 to)
{
    i32 n = 0;
    if (from < 0) from = 0;
    if (to < from) return 0;
    for (i32 i = from; i < to && buf[i]; i++) {
        if (((u8)buf[i] & 0xC0u) != 0x80u) n++;
    }
    return n;
}

/* Tokenise a single command (no metachars). $X expanded inline. Tokens
 * placed in `out[]`, returns token count. */
static i32 sh_tokenise(const char *cmd, char out[][64], i32 max)
{
    i32 n = 0, i = 0;
    while (cmd[i] && n < max) {
        while (cmd[i] && sh_isspace(cmd[i])) i++;
        if (!cmd[i]) break;
        i32 j = 0;
        while (cmd[i] && !sh_isspace(cmd[i]) && j < 63) {
            if (cmd[i] == '$') {
                /* expand variable */
                i++;
                char vn[16]; i32 k = 0;
                while (cmd[i] && ((cmd[i] >= 'A' && cmd[i] <= 'Z') ||
                                  (cmd[i] >= 'a' && cmd[i] <= 'z') ||
                                  (cmd[i] >= '0' && cmd[i] <= '9') ||
                                   cmd[i] == '_') && k < 15) {
                    vn[k++] = cmd[i++];
                }
                vn[k] = 0;
                shvar_t *v = sh_var_find(vn);
                if (v) {
                    i32 t = 0;
                    while (v->value[t] && j < 63) out[n][j++] = v->value[t++];
                }
            } else {
                out[n][j++] = cmd[i++];
            }
        }
        out[n][j] = 0;
        n++;
    }
    return n;
}

/* Run a single command (already tokenised). Output goes to `out` (cap
 * `cap`). Returns exit status (0 == success). */
static i32 sh_run_argv(i32 argc, char argv[][64], char *out, i32 cap)
{
    if (argc == 0) return 0;
    out[0] = 0;
    const char *cmd = argv[0];

    /* assignment X=val (only when first token has '=' and no command) */
    {
        i32 eq = -1;
        for (i32 i = 0; cmd[i]; i++) if (cmd[i] == '=') { eq = i; break; }
        if (eq > 0 && argc == 1) {
            char name[16], val[64]; i32 k = 0;
            for (i32 i = 0; i < eq && k < 15; i++) name[k++] = cmd[i];
            name[k] = 0;
            k = 0;
            for (i32 i = eq + 1; cmd[i] && k < 63; i++) val[k++] = cmd[i];
            val[k] = 0;
            sh_var_set(name, val);
            return 0;
        }
    }

    if (k_strcmp(cmd, "true")  == 0) return 0;
    if (k_strcmp(cmd, "false") == 0) return 1;
    if (k_strcmp(cmd, "exit")  == 0) { apps_close(); return 0; }
    if (k_strcmp(cmd, "clear") == 0) {
        for (i32 i = 0; i < TERM_LINES; i++) term_buf[i][0] = 0;
        return 0;
    }
    if (k_strcmp(cmd, "help") == 0) {
        k_strcpy(out,
            "pwd cd ls cat head tail wc sort uniq grep tr cut tee find rm "
            "touch cp mv mkdir rmdir basename dirname more less xxd file "
            "echo printf yes seq expr test [ env set unset alias export "
            "history ps top kill df du free mount lsblk uname hwinfo lscpu ver version whoami id "
            "groups who w users hostname uptime cal date reboot shutdown "
            "which type prg pkg open chrome falco heroic video search "
            "update man | > >>");
        return 0;
    }
    if (k_strcmp(cmd, "man") == 0) {
        if (argc < 2) { k_strcpy(out, "man: usage: man <command>"); return 1; }
        if (k_strcmp(argv[1], "prg") == 0 || k_strcmp(argv[1], "pkg") == 0) {
            k_strcpy(out, "prg: list | installed | search <term> | info <pkg> | install <pkg> | remove <pkg>");
            return 0;
        }
        if (k_strcmp(argv[1], "open") == 0) {
            k_strcpy(out, "open <app-name>  (e.g. open Falco, open Chrome, open Heroic, open Video)");
            return 0;
        }
        if (k_strcmp(argv[1], "falco") == 0) {
            k_strcpy(out, "falco [query]: opens Falco browser; with query, runs in-app search.");
            return 0;
        }
        if (k_strcmp(argv[1], "search") == 0) {
            k_strcpy(out, "search <query>: open Falco and run indexed search.");
            return 0;
        }
        if (k_strcmp(argv[1], "update") == 0) {
            k_strcpy(out, "update check | update apply: upgrade FalconOS components without reinstall.");
            return 0;
        }
        if (k_strcmp(argv[1], "video") == 0) {
            k_strcpy(out, "video: launches Video app. Keys: Space play/pause, <-/-> seek, Tab next clip.");
            return 0;
        }
        if (k_strcmp(argv[1], "hwinfo") == 0) {
            k_strcpy(out, "hwinfo: CPU vendor/brand via CPUID plus RAMmmap + framebuffer line.");
            return 0;
        }
        if (k_strcmp(argv[1], "ver") == 0 || k_strcmp(argv[1], "version") == 0) {
            k_strcpy(out, "ver / version — prints FalconOS build line (uname style).");
            return 0;
        }
        k_strcpy(out, argv[1]); k_strcat(out, ": manual entry not found");
        return 1;
    }
    if (k_strcmp(cmd, "open") == 0 || k_strcmp(cmd, "xdg-open") == 0) {
        if (argc < 2) { k_strcpy(out, "open: usage: open <app>"); return 1; }
        i32 ai = sh_find_app_idx(argv[1]);
        if (ai < 0 && argc >= 3) {
            char joined[64];
            k_strcpy(joined, argv[1]);
            for (i32 i = 2; i < argc && k_strlen(joined) < 62; i++) {
                k_strcat(joined, " ");
                k_strcat(joined, argv[i]);
            }
            ai = sh_find_app_idx(joined);
        }
        if (ai < 0) { k_strcpy(out, "open: app not found"); return 1; }
        apps_open(ai);
        k_strcpy(out, "opened "); k_strcat(out, apps_display_name(ai));
        return 0;
    }
    if (k_strcmp(cmd, "chrome") == 0) {
        i32 pkg = sh_find_pkg_idx("app-google-chrome");
        if (pkg >= 0 && !prg_is_installed(pkg)) {
            k_strcpy(out, "chrome: package not installed (run: prg install app-google-chrome)");
            return 1;
        }
        i32 ai = sh_find_app_idx("Chrome");
        if (ai >= 0) apps_open(ai);
        k_strcpy(out, "opening Chrome");
        return 0;
    }
    if (k_strcmp(cmd, "heroic") == 0) {
        i32 pkg = sh_find_pkg_idx("app-heroic-launcher");
        if (pkg >= 0 && !prg_is_installed(pkg)) {
            k_strcpy(out, "heroic: package not installed (run: prg install app-heroic-launcher)");
            return 1;
        }
        i32 ai = sh_find_app_idx("Heroic");
        if (ai >= 0) apps_open(ai);
        k_strcpy(out, "opening Heroic Launcher");
        return 0;
    }
    if (k_strcmp(cmd, "video") == 0) {
        i32 ai = sh_find_app_idx("Video");
        if (ai >= 0) apps_open(ai);
        k_strcpy(out, "opening Video player");
        return 0;
    }
    if (k_strcmp(cmd, "falco") == 0) {
        i32 ai = sh_find_app_idx("Falco");
        if (argc >= 2) {
            char q[80];
            q[0] = 0;
            for (i32 i = 1; i < argc && k_strlen(q) < 78; i++) {
                if (i > 1) k_strcat(q, " ");
                k_strcat(q, argv[i]);
            }
            falco_set_query(q);
        }
        if (ai >= 0) apps_open(ai);
        k_strcpy(out, "opening Falco browser");
        return 0;
    }
    if (k_strcmp(cmd, "search") == 0) {
        if (argc < 2) { k_strcpy(out, "search: usage: search <query>"); return 1; }
        char q[80];
        q[0] = 0;
        for (i32 i = 1; i < argc && k_strlen(q) < 78; i++) {
            if (i > 1) k_strcat(q, " ");
            k_strcat(q, argv[i]);
        }
        falco_set_query(q);
        i32 ai = sh_find_app_idx("Falco");
        if (ai >= 0) apps_open(ai);
        k_strcpy(out, "search forwarded to Falco");
        return 0;
    }
    if (k_strcmp(cmd, "update") == 0 || k_strcmp(cmd, "upgrade") == 0) {
        static const char *BUNDLE[] = {
            "app-falco-browser",
            "app-google-chrome",
            "app-heroic-launcher",
            "theme-liquid",
            "fonts-mono-pack",
            NULL
        };
        bool apply = (argc >= 2 &&
                      (k_strcmp(argv[1], "apply") == 0 ||
                       k_strcmp(argv[1], "upgrade") == 0 ||
                       k_strcmp(argv[1], "install") == 0));
        if (!apply) {
            i32 pending = 0;
            for (i32 i = 0; BUNDLE[i]; i++) {
                i32 idx = sh_find_pkg_idx(BUNDLE[i]);
                if (idx >= 0 && !prg_is_installed(idx)) pending++;
            }
            char num[16];
            k_strcpy(out, "update check: ");
            k_itoa((u32)pending, num, 10); k_strcat(out, num);
            k_strcat(out, " package(s) pending. Run: update apply");
            return 0;
        }
        i32 done = 0;
        for (i32 i = 0; BUNDLE[i]; i++) {
            i32 idx = sh_find_pkg_idx(BUNDLE[i]);
            if (idx >= 0 && prg_install(idx)) done++;
        }
        char num[16];
        k_strcpy(out, "update apply: ");
        k_itoa((u32)done, num, 10); k_strcat(out, num);
        k_strcat(out, " package(s) ready. Reinstall not required.");
        return 0;
    }
    if (k_strcmp(cmd, "prg") == 0 || k_strcmp(cmd, "pkg") == 0) {
        if (argc < 2 || k_strcmp(argv[1], "help") == 0) {
            k_strcpy(out, "prg: list | installed | search <term> | info <pkg> | install <pkg> | remove <pkg>");
            return 0;
        }
        if (k_strcmp(argv[1], "list") == 0) {
            out[0] = 0;
            for (i32 i = 0; i < prg_count(); i++) {
                const prg_pkg_t *p = prg_at(i);
                if (!p) continue;
                if (k_strlen(out) > cap - 40) break;
                k_strcat(out, prg_is_installed(i) ? "[i] " : "[ ] ");
                k_strcat(out, p->name);
                k_strcat(out, "\n");
            }
            i32 n = k_strlen(out); if (n > 0) out[n - 1] = 0;
            return 0;
        }
        if (k_strcmp(argv[1], "installed") == 0) {
            out[0] = 0;
            for (i32 i = 0; i < prg_count(); i++) {
                if (!prg_is_installed(i)) continue;
                const prg_pkg_t *p = prg_at(i);
                if (!p) continue;
                if (k_strlen(out) > cap - 32) break;
                k_strcat(out, p->name); k_strcat(out, "\n");
            }
            i32 n = k_strlen(out); if (n > 0) out[n - 1] = 0;
            if (!out[0]) k_strcpy(out, "(none)");
            return 0;
        }
        if (k_strcmp(argv[1], "search") == 0) {
            if (argc < 3) { k_strcpy(out, "prg search: missing term"); return 1; }
            out[0] = 0;
            for (i32 i = 0; i < prg_count(); i++) {
                const prg_pkg_t *p = prg_at(i);
                if (!p) continue;
                if (!sh_contains_ci(p->name, argv[2]) && !sh_contains_ci(p->summary, argv[2])) continue;
                if (k_strlen(out) > cap - 48) break;
                k_strcat(out, p->name);
                k_strcat(out, " - ");
                k_strcat(out, p->summary);
                k_strcat(out, "\n");
            }
            i32 n = k_strlen(out); if (n > 0) out[n - 1] = 0;
            if (!out[0]) k_strcpy(out, "no matches");
            return 0;
        }
        if (k_strcmp(argv[1], "info") == 0) {
            if (argc < 3) { k_strcpy(out, "prg info: missing package"); return 1; }
            i32 idx = sh_find_pkg_idx(argv[2]);
            if (idx < 0) { k_strcpy(out, "prg info: package not found"); return 1; }
            const prg_pkg_t *p = prg_at(idx);
            k_strcpy(out, p->name); k_strcat(out, " "); k_strcat(out, p->version);
            k_strcat(out, "\n");
            k_strcat(out, p->summary);
            k_strcat(out, "\nstatus: ");
            k_strcat(out, p->builtin ? "built-in" : (prg_is_installed(idx) ? "installed" : "not installed"));
            if (p->depends && p->depends[0]) { k_strcat(out, "\ndepends: "); k_strcat(out, p->depends); }
            return 0;
        }
        if (k_strcmp(argv[1], "install") == 0 || k_strcmp(argv[1], "remove") == 0) {
            if (argc < 3) { k_strcpy(out, "prg: missing package"); return 1; }
            i32 idx = sh_find_pkg_idx(argv[2]);
            if (idx < 0) { k_strcpy(out, "prg: package not found"); return 1; }
            bool ok = (k_strcmp(argv[1], "install") == 0) ? prg_install(idx) : prg_remove(idx);
            if (ok) {
                k_strcpy(out, argv[1]); k_strcat(out, " ok: "); k_strcat(out, argv[2]);
                return 0;
            }
            k_strcpy(out, argv[1]); k_strcat(out, " failed: "); k_strcat(out, argv[2]);
            return 1;
        }
        k_strcpy(out, "prg: unknown subcommand");
        return 1;
    }
    if (k_strcmp(cmd, "pwd") == 0)    { k_strcpy(out, sh_cwd); return 0; }
    if (k_strcmp(cmd, "uname") == 0)  {
#if ARCH_x86_64
        k_strcpy(out, "FalconOS 1 x86_64 bare-metal");
#else
        k_strcpy(out, "FalconOS 1 i386 bare-metal");
#endif
        return 0;
    }
    if (k_strcmp(cmd, "hwinfo") == 0) {
        hw_probe_summary(out, cap);
        return 0;
    }
    if (k_strcmp(cmd, "lscpu") == 0) {
        hw_probe_summary(out, cap);
        return 0;
    }
    if (k_strcmp(cmd, "ver") == 0 || k_strcmp(cmd, "version") == 0) {
        k_strcpy(out, "FalconOS 1  kernel  ");
#if ARCH_x86_64
        k_strcat(out, "x86_64  (tek komut: make start)");
#else
        k_strcat(out, "i386");
#endif
        return 0;
    }
    if (k_strcmp(cmd, "whoami") == 0) {
        const falcon_user_t *u = users_at(SET.active_user);
        k_strcpy(out, u ? u->name : "falcon");
        return 0;
    }
    if (k_strcmp(cmd, "date") == 0) {
        rtc_time_t t; rtc_local(&t);
        char num[16];
        out[0] = 0;
        loc_format_date(out, &t);
        k_strcat(out, "  ");
        if (t.hour < 10) k_strcat(out, "0");
        k_itoa(t.hour, num, 10); k_strcat(out, num); k_strcat(out, ":");
        if (t.min  < 10) k_strcat(out, "0");
        k_itoa(t.min,  num, 10); k_strcat(out, num);
        return 0;
    }
    if (k_strcmp(cmd, "cd") == 0) {
        char target[SH_PATHLEN];
        if (argc < 2) { k_strcpy(sh_cwd, "/home/falcon"); return 0; }
        sh_path_join(argv[1], target);
        shfile_t *d = sh_file_find(target);
        if (!d || !d->is_dir) {
            k_strcpy(out, "cd: not a directory: ");
            k_strcat(out, argv[1]);
            return 1;
        }
        k_strcpy(sh_cwd, target);
        return 0;
    }
    if (k_strcmp(cmd, "ls") == 0) {
        /* ls [path] — show immediate children of cwd, or of argv[1] if
         * supplied.  Directories are appended with '/' so they stand
         * out in plain text output. */
        char dirp[SH_PATHLEN];
        if (argc < 2) k_strcpy(dirp, sh_cwd);
        else          sh_path_join(argv[1], dirp);
        shfile_t *dd = sh_file_find(dirp);
        if (!dd) { k_strcpy(out, "ls: no such directory: "); k_strcat(out, dirp); return 1; }
        if (!dd->is_dir) { k_strcat(out, sh_path_basename(dirp)); return 0; }
        out[0] = 0; i32 first = 1;
        i32 plen = k_strlen(dirp);
        bool root = (plen == 1 && dirp[0] == '/');
        for (i32 i = 0; i < SH_FILES; i++) {
            if (!sh_files[i].used) continue;
            const char *p = sh_files[i].name;
            /* must start with dirp + '/' and have one more component */
            i32 j = 0;
            while (j < plen && p[j] == dirp[j]) j++;
            if (j != plen) continue;
            if (!root && p[j] != '/') continue;
            const char *child = root ? &p[1] : &p[j + 1];
            if (!*child) continue;
            for (const char *q = child; *q; q++) if (*q == '/') { child = 0; break; }
            if (!child) continue;
            if (!first) k_strcat(out, "  ");
            k_strcat(out, child);
            if (sh_files[i].is_dir) k_strcat(out, "/");
            first = 0;
        }
        if (first) k_strcpy(out, "");
        return 0;
    }
    if (k_strcmp(cmd, "cat") == 0) {
        if (argc < 2) return 1;
        shfile_t *f = sh_resolve(argv[1]);
        if (!f) { k_strcpy(out, "cat: not found: "); k_strcat(out, argv[1]); return 1; }
        if (f->is_dir) { k_strcpy(out, "cat: is a directory: "); k_strcat(out, argv[1]); return 1; }
        i32 k = 0;
        while (f->data[k] && k < cap - 1) { out[k] = f->data[k]; k++; }
        out[k] = 0;
        return 0;
    }
    if (k_strcmp(cmd, "echo") == 0) {
        out[0] = 0;
        for (i32 i = 1; i < argc; i++) {
            if (i > 1) k_strcat(out, " ");
            k_strcat(out, argv[i]);
        }
        return 0;
    }
    if (k_strcmp(cmd, "env") == 0 || k_strcmp(cmd, "set") == 0) {
        out[0] = 0;
        for (i32 i = 0; i < SH_VARS; i++) if (sh_vars[i].used) {
            k_strcat(out, sh_vars[i].name);
            k_strcat(out, "=");
            k_strcat(out, sh_vars[i].value);
            k_strcat(out, " ");
        }
        return 0;
    }
    if (k_strcmp(cmd, "unset") == 0) {
        if (argc < 2) return 1;
        shvar_t *v = sh_var_find(argv[1]);
        if (v) v->used = false;
        return 0;
    }
    if (k_strcmp(cmd, "rm") == 0) {
        if (argc < 2) { k_strcpy(out, "rm: missing operand"); return 1; }
        /* support -r/-rf: when set, also recursively unlink directory contents */
        bool recursive = false;
        i32 start = 1;
        if (argv[1][0] == '-') {
            for (i32 c = 1; argv[1][c]; c++) if (argv[1][c] == 'r' || argv[1][c] == 'R' || argv[1][c] == 'f') recursive = true;
            start = 2;
            if (argc < 3) { k_strcpy(out, "rm: missing operand"); return 1; }
        }
        for (i32 a = start; a < argc; a++) {
            char abs[SH_PATHLEN];
            sh_path_join(argv[a], abs);
            shfile_t *f = sh_file_find(abs);
            if (!f) continue;
            if (f->is_dir) {
                if (!recursive) { k_strcpy(out, "rm: is a directory: "); k_strcat(out, argv[a]); return 1; }
                /* unlink anything starting with abs + '/' */
                i32 alen = k_strlen(abs);
                for (i32 i = 0; i < SH_FILES; i++) {
                    if (!sh_files[i].used) continue;
                    const char *p = sh_files[i].name;
                    bool under = (k_strncmp(p, abs, alen) == 0 &&
                                  (p[alen] == '/' || (alen == 1 && p[0] == '/')));
                    if (under) sh_files[i].used = false;
                }
            }
            f->used = false;
        }
        return 0;
    }
    if (k_strcmp(cmd, "touch") == 0) {
        if (argc < 2) return 1;
        char abs[SH_PATHLEN];
        sh_path_join(argv[1], abs);
        /* parent must exist and be a dir */
        char parent[SH_PATHLEN];
        sh_path_parent(abs, parent);
        shfile_t *pd = sh_file_find(parent);
        if (!pd || !pd->is_dir) { k_strcpy(out, "touch: no such directory: "); k_strcat(out, parent); return 1; }
        sh_file_open_w(abs, true);
        return 0;
    }
    if (k_strcmp(cmd, "cp") == 0 || k_strcmp(cmd, "mv") == 0) {
        if (argc < 3) return 1;
        char asrc[SH_PATHLEN], adst[SH_PATHLEN];
        sh_path_join(argv[1], asrc);
        sh_path_join(argv[2], adst);
        shfile_t *src = sh_file_find(asrc);
        if (!src) { k_strcpy(out, cmd); k_strcat(out, ": no such file: "); k_strcat(out, argv[1]); return 1; }
        if (src->is_dir) { k_strcpy(out, cmd); k_strcat(out, ": is a directory: "); k_strcat(out, argv[1]); return 1; }
        /* If dst is an existing directory, copy into it preserving basename */
        shfile_t *dstd = sh_file_find(adst);
        if (dstd && dstd->is_dir) {
            i32 n = k_strlen(adst);
            if (n > 1) adst[n++] = '/';
            else            n = 1; /* root already ends in / */
            const char *b = sh_path_basename(asrc);
            i32 k = 0;
            while (b[k] && n < SH_PATHLEN - 1) adst[n++] = b[k++];
            adst[n] = 0;
        }
        char parent[SH_PATHLEN];
        sh_path_parent(adst, parent);
        shfile_t *pd = sh_file_find(parent);
        if (!pd || !pd->is_dir) { k_strcpy(out, cmd); k_strcat(out, ": no such directory: "); k_strcat(out, parent); return 1; }
        shfile_t *dst = sh_file_open_w(adst, false);
        if (!dst) return 1;
        for (u32 i = 0; i < src->len && i < SH_FBYTES - 1; i++) dst->data[i] = src->data[i];
        dst->len = src->len;
        dst->data[dst->len] = 0;
        if (k_strcmp(cmd, "mv") == 0) src->used = false;
        return 0;
    }
    if (k_strcmp(cmd, "mkdir") == 0) {
        if (argc < 2) { k_strcpy(out, "mkdir: missing operand"); return 1; }
        bool parents = false;
        i32 start = 1;
        if (argv[1][0] == '-' && argv[1][1] == 'p') { parents = true; start = 2; }
        if (start >= argc) { k_strcpy(out, "mkdir: missing operand"); return 1; }
        for (i32 a = start; a < argc; a++) {
            char abs[SH_PATHLEN];
            sh_path_join(argv[a], abs);
            if (sh_file_find(abs)) {
                if (parents) continue;
                k_strcpy(out, "mkdir: exists: "); k_strcat(out, argv[a]); return 1;
            }
            char parent[SH_PATHLEN];
            sh_path_parent(abs, parent);
            shfile_t *pd = sh_file_find(parent);
            if (!pd || !pd->is_dir) {
                if (!parents) { k_strcpy(out, "mkdir: no parent: "); k_strcat(out, parent); return 1; }
                /* -p: walk components and create each missing dir */
                char cur[SH_PATHLEN]; cur[0] = 0;
                i32 ci = 0;
                for (i32 i = 0; abs[i]; i++) {
                    cur[ci++] = abs[i];
                    if (abs[i] == '/' || abs[i + 1] == 0) {
                        cur[ci] = 0;
                        i32 cl = k_strlen(cur);
                        char trimmed[SH_PATHLEN];
                        k_strcpy(trimmed, cur);
                        if (cl > 1 && trimmed[cl - 1] == '/') trimmed[cl - 1] = 0;
                        if (trimmed[0] && !sh_file_find(trimmed)) {
                            i32 slot = -1;
                            for (i32 k = 0; k < SH_FILES; k++) if (!sh_files[k].used) { slot = k; break; }
                            if (slot < 0) { k_strcpy(out, "mkdir: shfs full"); return 1; }
                            sh_files[slot].used   = true;
                            sh_files[slot].is_dir = true;
                            sh_files[slot].len    = 0;
                            sh_files[slot].data[0] = 0;
                            k_strcpy(sh_files[slot].name, trimmed);
                        }
                    }
                }
                continue;
            }
            i32 slot = -1;
            for (i32 i = 0; i < SH_FILES; i++) if (!sh_files[i].used) { slot = i; break; }
            if (slot < 0) { k_strcpy(out, "mkdir: shfs full"); return 1; }
            sh_files[slot].used   = true;
            sh_files[slot].is_dir = true;
            sh_files[slot].len    = 0;
            sh_files[slot].data[0] = 0;
            k_strcpy(sh_files[slot].name, abs);
        }
        return 0;
    }
    if (k_strcmp(cmd, "rmdir") == 0) {
        if (argc < 2) { k_strcpy(out, "rmdir: missing operand"); return 1; }
        for (i32 a = 1; a < argc; a++) {
            char abs[SH_PATHLEN];
            sh_path_join(argv[a], abs);
            shfile_t *d = sh_file_find(abs);
            if (!d || !d->is_dir) { k_strcpy(out, "rmdir: not a directory: "); k_strcat(out, argv[a]); return 1; }
            /* must be empty */
            i32 alen = k_strlen(abs);
            for (i32 i = 0; i < SH_FILES; i++) {
                if (!sh_files[i].used || &sh_files[i] == d) continue;
                const char *p = sh_files[i].name;
                bool under = (k_strncmp(p, abs, alen) == 0 &&
                              (p[alen] == '/' || (alen == 1 && p[0] == '/')));
                if (under) { k_strcpy(out, "rmdir: not empty: "); k_strcat(out, argv[a]); return 1; }
            }
            d->used = false;
        }
        return 0;
    }
    /* head / tail [-n N] file --------------------------------------- */
    if (k_strcmp(cmd, "head") == 0 || k_strcmp(cmd, "tail") == 0) {
        i32 n = 5; i32 farg = 1;
        if (argc >= 3 && argv[1][0] == '-' && argv[1][1] == 'n') {
            n = 0; const char *p = argv[2];
            while (*p >= '0' && *p <= '9') { n = n*10 + (*p - '0'); p++; }
            farg = 3;
        } else if (argc >= 4 && k_strcmp(argv[1], "-n") == 0) {
            n = 0; const char *p = argv[2];
            while (*p >= '0' && *p <= '9') { n = n*10 + (*p - '0'); p++; }
            farg = 3;
        }
        if (argc <= farg) return 1;
        shfile_t *f = sh_resolve(argv[farg]);
        if (!f) { k_strcpy(out, cmd); k_strcat(out, ": no such file"); return 1; }
        /* count newlines */
        i32 total = 1;
        for (u32 i = 0; i < f->len; i++) if (f->data[i] == '\n') total++;
        i32 keep_from = 0, keep_to = total;
        if (k_strcmp(cmd, "head") == 0) keep_to = (n < total) ? n : total;
        else                            keep_from = (total - n > 0) ? total - n : 0;
        out[0] = 0; i32 k = 0; i32 line_idx = 0; i32 oc = 0;
        for (u32 i = 0; i < f->len && oc < cap - 1; i++) {
            if (line_idx >= keep_from && line_idx < keep_to) out[oc++] = f->data[i];
            if (f->data[i] == '\n') line_idx++;
            (void)k;
        }
        out[oc] = 0;
        return 0;
    }
    /* wc [-l|-w|-c] file -------------------------------------------- */
    if (k_strcmp(cmd, "wc") == 0) {
        char mode = 0; i32 farg = 1;
        if (argc >= 3 && argv[1][0] == '-') { mode = argv[1][1]; farg = 2; }
        if (argc <= farg) return 1;
        shfile_t *f = sh_resolve(argv[farg]);
        if (!f) { k_strcpy(out, "wc: no such file"); return 1; }
        u32 lines = 0, words = 0, chars = f->len; bool inw = false;
        for (u32 i = 0; i < f->len; i++) {
            if (f->data[i] == '\n') lines++;
            bool sp = (f->data[i] == ' ' || f->data[i] == '\t' || f->data[i] == '\n');
            if (!sp && !inw) { words++; inw = true; }
            else if (sp)     { inw = false; }
        }
        if (f->len > 0 && f->data[f->len - 1] != '\n') lines++;
        char num[16];
        out[0] = 0;
        if (mode == 'l' || mode == 0) { k_itoa(lines, num, 10); k_strcat(out, num); k_strcat(out, " "); }
        if (mode == 'w' || mode == 0) { k_itoa(words, num, 10); k_strcat(out, num); k_strcat(out, " "); }
        if (mode == 'c' || mode == 0) { k_itoa(chars, num, 10); k_strcat(out, num); k_strcat(out, " "); }
        k_strcat(out, argv[farg]);
        return 0;
    }
    /* sort / uniq — operate on file content line-by-line ------------ */
    if (k_strcmp(cmd, "sort") == 0 || k_strcmp(cmd, "uniq") == 0) {
        if (argc < 2) return 1;
        shfile_t *f = sh_resolve(argv[1]);
        if (!f) { k_strcpy(out, cmd); k_strcat(out, ": no such file"); return 1; }
        /* split into up to 32 lines (max 96 chars each)               */
        char lines[32][96]; i32 nl = 0;
        i32 li = 0; lines[0][0] = 0;
        for (u32 i = 0; i < f->len && nl < 32; i++) {
            if (f->data[i] == '\n') {
                lines[nl][li] = 0; nl++; li = 0;
                if (nl < 32) lines[nl][0] = 0;
            } else if (li < 95) {
                lines[nl][li++] = f->data[i];
            }
        }
        if (li > 0 && nl < 32) { lines[nl][li] = 0; nl++; }
        if (k_strcmp(cmd, "sort") == 0) {
            /* O(n^2) selection sort (n ≤ 32, fine)                   */
            for (i32 a = 0; a < nl - 1; a++) {
                i32 best = a;
                for (i32 b = a + 1; b < nl; b++)
                    if (k_strcmp(lines[b], lines[best]) < 0) best = b;
                if (best != a) {
                    char tmp[96]; k_strcpy(tmp, lines[a]);
                    k_strcpy(lines[a], lines[best]); k_strcpy(lines[best], tmp);
                }
            }
        } else { /* uniq — drop adjacent duplicates                  */
            i32 w = 0;
            for (i32 r = 0; r < nl; r++) {
                if (w == 0 || k_strcmp(lines[w - 1], lines[r]) != 0) {
                    if (w != r) k_strcpy(lines[w], lines[r]);
                    w++;
                }
            }
            nl = w;
        }
        out[0] = 0; i32 oc = 0;
        for (i32 a = 0; a < nl && oc < cap - 2; a++) {
            for (i32 b = 0; lines[a][b] && oc < cap - 2; b++) out[oc++] = lines[a][b];
            if (a < nl - 1) out[oc++] = '\n';
        }
        out[oc] = 0;
        return 0;
    }
    /* grep PATTERN file --------------------------------------------- */
    if (k_strcmp(cmd, "grep") == 0) {
        if (argc < 3) { k_strcpy(out, "grep: usage: grep PATTERN FILE"); return 1; }
        shfile_t *f = sh_resolve(argv[2]);
        if (!f) { k_strcpy(out, "grep: no such file"); return 1; }
        const char *pat = argv[1]; i32 pl = k_strlen(pat);
        out[0] = 0; i32 oc = 0;
        i32 line_start = 0;
        for (u32 i = 0; i <= f->len; i++) {
            if (i == f->len || f->data[i] == '\n') {
                /* check pat in [line_start, i) */
                bool hit = false;
                for (i32 a = line_start; a + pl <= (i32)i && !hit; a++) {
                    bool ok = true;
                    for (i32 b = 0; b < pl && ok; b++)
                        if (f->data[a + b] != pat[b]) ok = false;
                    if (ok) hit = true;
                }
                if (hit) {
                    for (i32 a = line_start; a < (i32)i && oc < cap - 2; a++)
                        out[oc++] = f->data[a];
                    if (oc < cap - 2) out[oc++] = '\n';
                }
                line_start = i + 1;
            }
        }
        if (oc > 0) out[oc - 1] = 0;
        else        out[0] = 0;
        return 0;
    }
    /* tr FROM TO — character-class translate (echo|tr 'a-z' 'A-Z')  */
    if (k_strcmp(cmd, "tr") == 0) {
        if (argc < 3) return 1;
        const char *from = argv[1], *to = argv[2];
        i32 fl = k_strlen(from);
        out[0] = 0; i32 oc = 0;
        /* tr operates on the file we were given, defaulting to a piped
         * stdin (we don't implement pipes here, so callers redirect).  */
        const char *src = (argc >= 4) ? argv[3] : "";
        shfile_t *f = (argc >= 4) ? sh_resolve(src) : 0;
        const char *data = f ? f->data : src;
        u32 dlen = f ? f->len : (u32)k_strlen(src);
        for (u32 i = 0; i < dlen && oc < cap - 1; i++) {
            char c = data[i]; bool hit = false;
            for (i32 j = 0; j < fl; j++) {
                if (from[j] == c) {
                    out[oc++] = (j < k_strlen(to)) ? to[j] : c;
                    hit = true; break;
                }
            }
            if (!hit) out[oc++] = c;
        }
        out[oc] = 0;
        return 0;
    }
    /* cut -c N-M file ----------------------------------------------- */
    if (k_strcmp(cmd, "cut") == 0) {
        if (argc < 4) return 1;
        i32 a = 1, b = 1;
        const char *spec = argv[2];
        if (argv[1][0] == '-' && argv[1][1] == 'c') {
            a = 0; const char *p = spec;
            while (*p >= '0' && *p <= '9') { a = a * 10 + (*p - '0'); p++; }
            if (*p == '-') {
                p++; b = 0;
                while (*p >= '0' && *p <= '9') { b = b * 10 + (*p - '0'); p++; }
            } else b = a;
        }
        shfile_t *f = sh_resolve(argv[3]);
        if (!f) return 1;
        out[0] = 0; i32 oc = 0; i32 col = 1;
        for (u32 i = 0; i < f->len && oc < cap - 1; i++) {
            if (f->data[i] == '\n') {
                out[oc++] = '\n'; col = 1; continue;
            }
            if (col >= a && col <= b) out[oc++] = f->data[i];
            col++;
        }
        out[oc] = 0;
        return 0;
    }
    /* tee file — overwrite a file with the joined remaining args ---- */
    if (k_strcmp(cmd, "tee") == 0) {
        if (argc < 2) return 1;
        char abs[SH_PATHLEN];
        sh_path_join(argv[1], abs);
        shfile_t *f = sh_file_open_w(abs, false);
        if (!f) return 1;
        out[0] = 0;
        for (i32 i = 2; i < argc; i++) {
            i32 ol = k_strlen(out);
            if (i > 2 && ol < cap - 1) k_strcat(out, " ");
            k_strcat(out, argv[i]);
        }
        i32 ol = k_strlen(out);
        for (i32 i = 0; i < ol && i < SH_FBYTES - 1; i++) f->data[i] = out[i];
        f->len = ol; f->data[f->len] = 0;
        return 0;
    }
    /* find — list all shfs entries (no path filter, single-dir fs)   */
    if (k_strcmp(cmd, "find") == 0) {
        out[0] = 0;
        for (i32 i = 0; i < SH_FILES; i++) if (sh_files[i].used) {
            k_strcat(out, "./");
            k_strcat(out, sh_files[i].name);
            k_strcat(out, "\n");
        }
        i32 ol = k_strlen(out);
        if (ol > 0) out[ol - 1] = 0;
        return 0;
    }
    /* basename / dirname ------------------------------------------- */
    if (k_strcmp(cmd, "basename") == 0) {
        if (argc < 2) return 1;
        const char *p = argv[1]; const char *last = p;
        for (; *p; p++) if (*p == '/') last = p + 1;
        k_strcpy(out, last);
        return 0;
    }
    if (k_strcmp(cmd, "dirname") == 0) {
        if (argc < 2) return 1;
        i32 last = -1;
        for (i32 i = 0; argv[1][i]; i++) if (argv[1][i] == '/') last = i;
        if (last < 0) { k_strcpy(out, "."); return 0; }
        for (i32 i = 0; i < last; i++) out[i] = argv[1][i];
        out[last] = 0; if (last == 0) k_strcpy(out, "/");
        return 0;
    }
    /* more / less — single-page cat ------------------------------- */
    if (k_strcmp(cmd, "more") == 0 || k_strcmp(cmd, "less") == 0) {
        if (argc < 2) return 1;
        shfile_t *f = sh_resolve(argv[1]);
        if (!f) return 1;
        i32 k = 0;
        while (f->data[k] && k < cap - 1) { out[k] = f->data[k]; k++; }
        out[k] = 0;
        return 0;
    }
    /* xxd / hexdump first 64 bytes -------------------------------- */
    if (k_strcmp(cmd, "xxd") == 0 || k_strcmp(cmd, "hexdump") == 0) {
        if (argc < 2) return 1;
        shfile_t *f = sh_resolve(argv[1]);
        if (!f) return 1;
        out[0] = 0; char num[8]; i32 oc = 0;
        for (u32 i = 0; i < f->len && i < 64 && oc < cap - 8; i++) {
            u8 b = (u8)f->data[i];
            const char *hex = "0123456789abcdef";
            num[0] = hex[(b >> 4) & 0xF]; num[1] = hex[b & 0xF]; num[2] = ' '; num[3] = 0;
            k_strcat(out, num); oc += 3;
        }
        return 0;
    }
    /* file — guess content type ------------------------------------ */
    if (k_strcmp(cmd, "file") == 0) {
        if (argc < 2) return 1;
        shfile_t *f = sh_resolve(argv[1]);
        if (!f) { k_strcpy(out, "file: no such file"); return 1; }
        bool ascii = true;
        for (u32 i = 0; i < f->len; i++) {
            u8 c = (u8)f->data[i];
            if (c < 9 || (c > 13 && c < 32) || c == 127) { ascii = false; break; }
        }
        k_strcpy(out, argv[1]);
        k_strcat(out, ascii ? ": ASCII text" : ": data");
        return 0;
    }
    /* printf — %s only --------------------------------------------- */
    if (k_strcmp(cmd, "printf") == 0) {
        out[0] = 0;
        if (argc < 2) return 0;
        i32 ai = 2;
        for (i32 i = 0; argv[1][i]; i++) {
            if (argv[1][i] == '%' && argv[1][i + 1] == 's' && ai < argc) {
                k_strcat(out, argv[ai++]); i++;
            } else if (argv[1][i] == '\\' && argv[1][i + 1] == 'n') {
                k_strcat(out, "\n"); i++;
            } else {
                char one[2] = { argv[1][i], 0 }; k_strcat(out, one);
            }
        }
        return 0;
    }
    /* yes — bash echoes 'y' forever; we echo it once so the term
     * doesn't lock up                                              */
    if (k_strcmp(cmd, "yes") == 0) {
        k_strcpy(out, argc >= 2 ? argv[1] : "y");
        return 0;
    }
    /* seq M [N] ---------------------------------------------------- */
    if (k_strcmp(cmd, "seq") == 0) {
        if (argc < 2) return 1;
        i32 a = 1, b = 0;
        const char *p1 = argv[1];
        i32 v1 = 0; while (*p1 >= '0' && *p1 <= '9') { v1 = v1 * 10 + (*p1 - '0'); p1++; }
        if (argc >= 3) {
            a = v1;
            const char *p2 = argv[2]; b = 0;
            while (*p2 >= '0' && *p2 <= '9') { b = b * 10 + (*p2 - '0'); p2++; }
        } else { a = 1; b = v1; }
        out[0] = 0; char num[16];
        for (i32 i = a; i <= b && k_strlen(out) < cap - 8; i++) {
            k_itoa(i, num, 10); k_strcat(out, num);
            if (i < b) k_strcat(out, "\n");
        }
        return 0;
    }
    /* expr / test / [ ---------------------------------------------- */
    if (k_strcmp(cmd, "expr") == 0) {
        if (argc < 4) return 1;
        i32 a = 0, b = 0;
        const char *p = argv[1]; while (*p >= '0' && *p <= '9') { a = a * 10 + (*p - '0'); p++; }
        p = argv[3]; while (*p >= '0' && *p <= '9') { b = b * 10 + (*p - '0'); p++; }
        i32 r = 0;
        if (k_strcmp(argv[2], "+") == 0) r = a + b;
        else if (k_strcmp(argv[2], "-") == 0) r = a - b;
        else if (k_strcmp(argv[2], "*") == 0) r = a * b;
        else if (k_strcmp(argv[2], "/") == 0) r = (b == 0) ? 0 : a / b;
        else if (k_strcmp(argv[2], "%") == 0) r = (b == 0) ? 0 : a % b;
        else { k_strcpy(out, "expr: unsupported op"); return 2; }
        char num[16]; k_itoa(r, num, 10); k_strcpy(out, num);
        return 0;
    }
    if (k_strcmp(cmd, "test") == 0 || k_strcmp(cmd, "[") == 0) {
        /* [ -z STR ] / [ -n STR ] / [ A = B ] / [ A != B ] / [ A -lt B ] etc */
        i32 stripped = (k_strcmp(cmd, "[") == 0 &&
                        argc >= 2 && k_strcmp(argv[argc - 1], "]") == 0) ? 1 : 0;
        i32 nargs = argc - stripped;
        if (nargs == 3) {
            i32 a = 0, b = 0;
            for (i32 i = 0; argv[1][i]; i++) a = a * 10 + (argv[1][i] - '0');
            for (i32 i = 0; argv[3][i]; i++) b = b * 10 + (argv[3][i] - '0');
            if (k_strcmp(argv[2], "=")    == 0) return k_strcmp(argv[1], argv[3]) == 0 ? 0 : 1;
            if (k_strcmp(argv[2], "!=")   == 0) return k_strcmp(argv[1], argv[3]) != 0 ? 0 : 1;
            if (k_strcmp(argv[2], "-eq")  == 0) return a == b ? 0 : 1;
            if (k_strcmp(argv[2], "-ne")  == 0) return a != b ? 0 : 1;
            if (k_strcmp(argv[2], "-lt")  == 0) return a <  b ? 0 : 1;
            if (k_strcmp(argv[2], "-le")  == 0) return a <= b ? 0 : 1;
            if (k_strcmp(argv[2], "-gt")  == 0) return a >  b ? 0 : 1;
            if (k_strcmp(argv[2], "-ge")  == 0) return a >= b ? 0 : 1;
        } else if (nargs == 2) {
            if (k_strcmp(argv[1], "-z") == 0) return argv[2][0] == 0 ? 0 : 1;
            if (k_strcmp(argv[1], "-n") == 0) return argv[2][0] != 0 ? 0 : 1;
            if (k_strcmp(argv[1], "-e") == 0 || k_strcmp(argv[1], "-f") == 0)
                return sh_resolve(argv[2]) ? 0 : 1;
            if (k_strcmp(argv[1], "-d") == 0) {
                shfile_t *fd = sh_resolve(argv[2]);
                return (fd && fd->is_dir) ? 0 : 1;
            }
        }
        return 1;
    }
    /* alias / export — we accept the syntax but treat them as no-ops
     * (assignment via X=val already covered above)                  */
    if (k_strcmp(cmd, "alias") == 0 || k_strcmp(cmd, "export") == 0) return 0;
    /* sleep — cooperative spin (1s = ~PIT_HZ ticks).  Single-task
     * kernel, so we just no-op past tiny intervals.                 */
    if (k_strcmp(cmd, "sleep") == 0) return 0;
    /* history — show the last command we just ran (single slot).    */
    if (k_strcmp(cmd, "history") == 0) { k_strcpy(out, "1  (history is shell-buffered)"); return 0; }
    /* ---- system info commands ---------------------------------- */
    if (k_strcmp(cmd, "hostname") == 0) { k_strcpy(out, "falcon-1"); return 0; }
    if (k_strcmp(cmd, "id") == 0) {
        const falcon_user_t *u = users_at(SET.active_user);
        k_strcpy(out, "uid=1000(");
        k_strcat(out, u ? u->name : "falcon");
        k_strcat(out, ") gid=1000(staff)");
        return 0;
    }
    if (k_strcmp(cmd, "groups") == 0) { k_strcpy(out, "staff falcon"); return 0; }
    if (k_strcmp(cmd, "who") == 0 || k_strcmp(cmd, "w") == 0 ||
        k_strcmp(cmd, "users") == 0) {
        out[0] = 0;
        for (i32 i = 0; i < SET.user_count; i++) {
            if (i > 0) k_strcat(out, "\n");
            k_strcat(out, SET.users[i].name);
            k_strcat(out, "  tty1   (local)");
        }
        return 0;
    }
    if (k_strcmp(cmd, "uptime") == 0) {
        u32 h = 0, m = 0, s = 0; pit_uptime(&h, &m, &s);
        char num[16]; out[0] = 0;
        k_strcat(out, "up ");
        k_itoa(h, num, 10); k_strcat(out, num); k_strcat(out, "h ");
        k_itoa(m, num, 10); k_strcat(out, num); k_strcat(out, "m ");
        k_itoa(s, num, 10); k_strcat(out, num); k_strcat(out, "s, ");
        k_itoa(SET.user_count, num, 10); k_strcat(out, num);
        k_strcat(out, " user(s)");
        return 0;
    }
    if (k_strcmp(cmd, "cal") == 0) {
        rtc_time_t t; rtc_local(&t);
        char num[16];
        k_strcpy(out, loc_month_short(t.month)); k_strcat(out, " ");
        k_itoa(t.year, num, 10); k_strcat(out, num);
        return 0;
    }
    /* ps / top — single-task kernel; show one fake row              */
    if (k_strcmp(cmd, "ps") == 0 || k_strcmp(cmd, "top") == 0 ||
        k_strcmp(cmd, "jobs") == 0) {
        k_strcpy(out, "  PID TTY     TIME CMD\n    1 tty1   0:00 falcon-kernel\n   42 tty1   0:00 ");
        k_strcat(out, cmd);
        return 0;
    }
    if (k_strcmp(cmd, "kill") == 0) { k_strcpy(out, "kill: no userland processes"); return 1; }
    /* df / du / free / mount / lsblk -------------------------------- */
    if (k_strcmp(cmd, "df") == 0) {
        i32 disks = ata_probe_count();
        char num[28], tag[24];
        k_strcpy(out,
                 "Filesystem   SizeMiB  UsedMiB AvailMiB Mounted\n");
        if (disks <= 0) {
            k_strcat(out,
                     "ramfs        -       -       -       /\n");
            return 0;
        }
        for (i32 d = 0; d < disks; d++) {
            u64 sec = ata_sectors(d);
            if (sec == 0) continue;
            u32 mib = (u32)((sec * 512u) / (1024u * 1024u));
            tag[0] = 0;
            k_strcpy(tag, "ata");
            k_itoa((u32)d, num, 10);
            k_strcat(tag, num);
            k_strcat(out, tag);
            for (i32 pad = k_strlen(tag); pad < 14; pad++) k_strcat(out, " ");
            k_itoa(mib, num, 10);
            k_strcat(out, num);
            k_strcat(out, "     -       -       /\n");
        }
        return 0;
    }
    if (k_strcmp(cmd, "du") == 0) {
        u32 total = 0; out[0] = 0; char num[16];
        for (i32 i = 0; i < SH_FILES; i++) if (sh_files[i].used) {
            total += sh_files[i].len;
            k_itoa(sh_files[i].len, num, 10);
            k_strcat(out, num); k_strcat(out, "\t");
            k_strcat(out, sh_files[i].name); k_strcat(out, "\n");
        }
        k_itoa(total, num, 10);
        k_strcat(out, num); k_strcat(out, "\ttotal");
        return 0;
    }
    if (k_strcmp(cmd, "free") == 0) {
        char tot[28], used[28], fr[28];
        u64 mib = RAM_TOTAL_BYTES / ((u64)1024 * 1024);
        u64 u_mib = (mib > 256u) ? 256u : (mib > 32u ? mib / 8u : 8u);
        u64 f_mib = (mib > u_mib) ? (mib - u_mib) : 0;
        k_u64_to_dec(mib, tot);
        k_u64_to_dec(u_mib, used);
        k_u64_to_dec(f_mib, fr);
        k_strcpy(out,
                 "               totalMiB       usedMiB       freeMiB\nMem: ");
        k_strcat(out, tot);
        while (k_strlen(out) < 42) k_strcat(out, " ");
        k_strcat(out, used);
        while (k_strlen(out) < 58) k_strcat(out, " ");
        k_strcat(out, fr);
        k_strcat(out, "\n(mmap totals from firmware; kernel+BSS heuristic in used)");
        return 0;
    }
    if (k_strcmp(cmd, "mount") == 0) {
        k_strcpy(out, "ata0 on / type FalconFS (rw,relatime)\nshfs on /home/falcon type ramfs (rw)");
        return 0;
    }
    if (k_strcmp(cmd, "lsblk") == 0) {
        char num[28], nm[24];
        k_strcpy(out, "NAME        SIZEMiB TRAN\n");
        i32 disks = ata_probe_count();
        for (i32 i = 0; i < disks; i++) {
            u64 sec = ata_sectors(i);
            u32 mib = sec ? (u32)((sec * 512ull) / (1024ull * 1024ull)) : 0;
            nm[0] = 0;
            k_strcpy(nm, "ata");
            k_itoa((u32)i, num, 10); k_strcat(nm, num);
            k_strcat(out, nm);
            for (i32 pad = k_strlen(nm); pad < 12; pad++) k_strcat(out, " ");
            k_itoa(mib, num, 10);
            k_strcat(out, num);
            k_strcat(out, "    disk\n");
        }
        if (disks == 0)
            k_strcat(out, "(no block devices)\n");
        return 0;
    }
    /* power -------------------------------------------------------- */
    if (k_strcmp(cmd, "reboot") == 0) {
        k_strcpy(out, "reboot scheduled — see Power menu (F12)");
        return 0;
    }
    if (k_strcmp(cmd, "shutdown") == 0 || k_strcmp(cmd, "poweroff") == 0 ||
        k_strcmp(cmd, "halt") == 0) {
        k_strcpy(out, cmd); k_strcat(out, ": use F12 power menu to confirm");
        return 0;
    }
    /* which / type — search built-in keywords ---------------------- */
    if (k_strcmp(cmd, "which") == 0 || k_strcmp(cmd, "type") == 0) {
        if (argc < 2) return 1;
        static const char *BUILTINS[] = {
            "pwd","cd","ls","cat","echo","env","set","unset","export","alias",
            "rm","touch","cp","mv","mkdir","rmdir","head","tail","wc","sort",
            "uniq","grep","tr","cut","tee","find","basename","dirname","more",
            "less","xxd","hexdump","file","printf","yes","seq","expr","test",
            "[","sleep","history","hostname","id","groups","who","w","users",
            "uptime","cal","ps","top","jobs","kill","df","du","free","mount",
            "lsblk","reboot","shutdown","poweroff","halt","which","type","hwinfo",
            "uname","whoami","date","clear","help","true","false","exit",
            "man","open","xdg-open","prg","pkg","chrome","falco","heroic",
            "video","search","update","upgrade","ver","version","lscpu",NULL
        };
        for (i32 i = 0; BUILTINS[i]; i++) {
            if (k_strcmp(BUILTINS[i], argv[1]) == 0) {
                k_strcpy(out, argv[1]);
                k_strcat(out, ": shell builtin");
                return 0;
            }
        }
        k_strcpy(out, argv[1]); k_strcat(out, " not found");
        return 1;
    }
    /* network — delegate to net_tools (ip / ping / arp / netstat) ---- */
    if (k_strcmp(cmd, "ip") == 0 || k_strcmp(cmd, "ping") == 0 ||
        k_strcmp(cmd, "arp") == 0 || k_strcmp(cmd, "netstat") == 0) {
        char buf[256];
        k_strcpy(buf, cmd);
        for (i32 i = 1; i < argc; i++) {
            k_strcat(buf, " ");
            k_strcat(buf, argv[i]);
        }
        net_tools_dispatch(buf, out);
        return 0;
    }
    if (k_strcmp(cmd, "ifconfig") == 0) {
        net_tools_dispatch("ip addr", out);
        return 0;
    }
    if (k_strcmp(cmd, "route") == 0) {
        if (!net_present()) { k_strcpy(out, "route: no adapter"); return 1; }
        k_strcpy(out, "Destination     Gateway         Iface\n0.0.0.0         ");
        k_strcat(out, net_gateway());
        k_strcat(out, "         eth0");
        return 0;
    }
    if (k_strcmp(cmd, "wget") == 0 || k_strcmp(cmd, "curl") == 0) {
        if (argc < 2) {
            k_strcpy(out, cmd); k_strcat(out, ": missing URL");
            return 1;
        }
        if (!net_connected()) {
            k_strcpy(out, cmd); k_strcat(out, ": no network — Settings > Network or 'ip dhcp'");
            return 1;
        }
        k_strcpy(out, cmd); k_strcat(out, ": HTTPS pending TLS port (FalconOS 1.2 in progress)");
        return 1;
    }
    /* nl — number lines (mimics POSIX nl with default style)         */
    if (k_strcmp(cmd, "nl") == 0) {
        if (argc < 2) { k_strcpy(out, "nl: missing operand"); return 1; }
        shfile_t *n = sh_resolve(argv[1]);
        if (!n) { k_strcpy(out, "nl: "); k_strcat(out, argv[1]); k_strcat(out, ": no such file"); return 1; }
        i32 ln = 1;
        i32 oi = 0; out[0] = 0;
        u32 i = 0;
        while (i < n->len && oi < 1900) {
            char nbuf[8];
            k_itoa(ln, nbuf, 10);
            for (i32 j = 0; nbuf[j] && oi < 1900; j++) out[oi++] = nbuf[j];
            out[oi++] = '\t';
            while (i < n->len && n->data[i] != '\n' && oi < 1900) out[oi++] = n->data[i++];
            if (i < n->len && n->data[i] == '\n') { out[oi++] = '\n'; i++; ln++; }
        }
        out[oi] = 0;
        return 0;
    }
    /* paste — paste multiple files line-by-line, tab-separated       */
    if (k_strcmp(cmd, "paste") == 0) {
        if (argc < 2) { k_strcpy(out, "paste: missing operand"); return 1; }
        shfile_t *files[8] = {0};
        i32 fc = 0;
        for (i32 i = 1; i < argc && fc < 8; i++) {
            files[fc] = sh_resolve(argv[i]);
            if (!files[fc]) { k_strcpy(out, "paste: "); k_strcat(out, argv[i]); k_strcat(out, ": no such file"); return 1; }
            fc++;
        }
        u32 pos[8] = {0};
        i32 oi = 0; out[0] = 0;
        bool any = true;
        while (any && oi < 1900) {
            any = false;
            for (i32 f = 0; f < fc; f++) {
                if (f) out[oi++] = '\t';
                while (pos[f] < files[f]->len && files[f]->data[pos[f]] != '\n' && oi < 1900) {
                    out[oi++] = files[f]->data[pos[f]++];
                    any = true;
                }
                if (pos[f] < files[f]->len && files[f]->data[pos[f]] == '\n') {
                    pos[f]++;
                    any = true;
                }
            }
            if (any && oi < 1900) out[oi++] = '\n';
        }
        out[oi] = 0;
        return 0;
    }
    /* awk -F<sep> '{print $N}' file — minimal field extraction        */
    if (k_strcmp(cmd, "awk") == 0) {
        if (argc < 4) { k_strcpy(out, "awk: usage: awk -F<sep> {print $N} <file>"); return 1; }
        char sep = ' ';
        i32 ai = 1;
        if (argv[ai][0] == '-' && argv[ai][1] == 'F' && argv[ai][2]) {
            sep = argv[ai][2]; ai++;
        }
        i32 col = 0;
        char *prog = argv[ai];
        for (i32 j = 0; prog[j]; j++) {
            if (prog[j] == '$' && prog[j+1] >= '0' && prog[j+1] <= '9') {
                col = prog[j+1] - '0';
                break;
            }
        }
        ai++;
        shfile_t *n = sh_resolve(argv[ai]);
        if (!n) { k_strcpy(out, "awk: "); k_strcat(out, argv[ai]); k_strcat(out, ": no such file"); return 1; }
        i32 oi = 0; out[0] = 0;
        u32 i = 0;
        while (i < n->len && oi < 1900) {
            i32 c = 1;
            while (i < n->len && n->data[i] != '\n') {
                if (c == col || col == 0) {
                    if (oi < 1900) out[oi++] = n->data[i];
                }
                if (n->data[i] == sep) c++;
                i++;
            }
            if (i < n->len) { out[oi++] = '\n'; i++; }
        }
        out[oi] = 0;
        return 0;
    }
    /* sed s/X/Y/ <file> — single-rule substitution                   */
    if (k_strcmp(cmd, "sed") == 0) {
        if (argc < 3 || argv[1][0] != 's' || argv[1][1] != '/') {
            k_strcpy(out, "sed: usage: sed s/X/Y/ <file>");
            return 1;
        }
        char from[64] = {0}, to[64] = {0};
        i32 sai = 2; /* skip s and / */
        i32 fi = 0;
        while (argv[1][sai] && argv[1][sai] != '/' && fi < 63) from[fi++] = argv[1][sai++];
        if (argv[1][sai] == '/') sai++;
        i32 ti = 0;
        while (argv[1][sai] && argv[1][sai] != '/' && ti < 63) to[ti++] = argv[1][sai++];
        shfile_t *n = sh_resolve(argv[2]);
        if (!n) { k_strcpy(out, "sed: "); k_strcat(out, argv[2]); k_strcat(out, ": no such file"); return 1; }
        i32 fl = k_strlen(from);
        i32 tl = k_strlen(to);
        i32 oi = 0; out[0] = 0;
        for (u32 i = 0; i < n->len && oi < 1900;) {
            bool match = (fl > 0 && i + (u32)fl <= n->len);
            for (i32 k = 0; k < fl && match; k++) if (n->data[i+k] != from[k]) match = false;
            if (match) { for (i32 k = 0; k < tl && oi < 1900; k++) out[oi++] = to[k]; i += fl; }
            else       { out[oi++] = n->data[i++]; }
        }
        out[oi] = 0;
        return 0;
    }
    /* ==================== FalconOS 1.2.1 — added shell commands ==================== */
    if (k_strcmp(cmd, "tac") == 0) {
        if (argc < 2) { k_strcpy(out, "tac: missing operand"); return 1; }
        shfile_t *f = sh_resolve(argv[1]);
        if (!f || f->is_dir) { k_strcpy(out, "tac: not a file"); return 1; }
        i32 oi = 0; out[0] = 0;
        i32 line_starts[64]; i32 lc = 0; line_starts[lc++] = 0;
        for (u32 i = 0; i < f->len && lc < 64; i++)
            if (f->data[i] == '\n' && i + 1 < f->len) line_starts[lc++] = (i32)i + 1;
        for (i32 li = lc - 1; li >= 0 && oi < 1900; li--) {
            i32 s = line_starts[li];
            i32 e = (li + 1 < lc) ? line_starts[li + 1] - 1 : (i32)f->len;
            for (i32 i = s; i < e && oi < 1900; i++) out[oi++] = f->data[i];
            if (oi < 1900) out[oi++] = '\n';
        }
        out[oi] = 0;
        return 0;
    }
    if (k_strcmp(cmd, "rev") == 0) {
        if (argc < 2) { k_strcpy(out, ""); return 0; }
        i32 al = k_strlen(argv[1]);
        for (i32 i = 0; i < al; i++) out[i] = argv[1][al - 1 - i];
        out[al] = 0; return 0;
    }
    if (k_strcmp(cmd, "realpath") == 0 || k_strcmp(cmd, "readlink") == 0) {
        if (argc < 2) { k_strcpy(out, cmd); k_strcat(out, ": missing operand"); return 1; }
        char abs[SH_PATHLEN]; sh_path_join(argv[1], abs);
        k_strcpy(out, abs); return 0;
    }
    if (k_strcmp(cmd, "stat") == 0) {
        if (argc < 2) { k_strcpy(out, "stat: missing operand"); return 1; }
        shfile_t *f = sh_resolve(argv[1]);
        if (!f) { k_strcpy(out, "stat: no such file: "); k_strcat(out, argv[1]); return 1; }
        char num[24];
        k_strcpy(out, "  File: ");
        k_strcat(out, f->name);
        k_strcat(out, f->is_dir ? "\n  Type: directory\n  Size: -" : "\n  Type: regular file\n  Size: ");
        if (!f->is_dir) { k_itoa(f->len, num, 10); k_strcat(out, num); k_strcat(out, " bytes"); }
        k_strcat(out, "\n  Mode: 644  Uid: 1000  Gid: 1000");
        return 0;
    }
    if (k_strcmp(cmd, "tree") == 0) {
        const char *base = (argc >= 2) ? argv[1] : ".";
        char root[SH_PATHLEN]; sh_path_join(base, root);
        i32 oi = 0; out[0] = 0;
        k_strcpy(out, root); k_strcat(out, "\n"); oi = k_strlen(out);
        i32 rl = k_strlen(root);
        for (i32 i = 0; i < SH_FILES && oi < 1900; i++) {
            if (!sh_files[i].used) continue;
            const char *p = sh_files[i].name;
            i32 j = 0; while (j < rl && p[j] == root[j]) j++;
            if (j != rl) continue;
            if (!p[j]) continue;
            if (rl > 1 && p[j] != '/') continue;
            i32 depth = 0;
            for (i32 k = (rl > 1 ? rl + 1 : rl); p[k]; k++) if (p[k] == '/') depth++;
            for (i32 d = 0; d <= depth && oi < 1898; d++) { out[oi++] = ' '; out[oi++] = ' '; }
            const char *base2 = sh_path_basename(p);
            for (i32 k = 0; base2[k] && oi < 1898; k++) out[oi++] = base2[k];
            if (sh_files[i].is_dir && oi < 1899) out[oi++] = '/';
            if (oi < 1899) out[oi++] = '\n';
        }
        out[oi] = 0; return 0;
    }
    if (k_strcmp(cmd, "chmod") == 0 || k_strcmp(cmd, "chown") == 0 || k_strcmp(cmd, "chgrp") == 0) {
        /* FalconFS has no permission bits; accept silently like /bin/true. */
        return 0;
    }
    if (k_strcmp(cmd, "ln") == 0) {
        /* Hard/symlinks would need a separate field; emit a copy as the
         * best approximation while keeping the same exit code shape. */
        if (argc < 3) { k_strcpy(out, "ln: missing operand"); return 1; }
        i32 t_argc = 3;
        const char *t_argv[3] = { "cp", argv[argc - 2], argv[argc - 1] };
        (void)t_argc; (void)t_argv;
        k_strcpy(out, "ln: symlinks not supported; use cp(1)");
        return 1;
    }
    if (k_strcmp(cmd, "mktemp") == 0) {
        u32 seed = g_ticks ^ 0xA5A5A5A5u;
        char name[SH_PATHLEN]; k_strcpy(name, "/tmp/tmp.");
        i32 nl = k_strlen(name);
        for (i32 k = 0; k < 6; k++) {
            seed = seed * 1103515245u + 12345u;
            name[nl++] = 'a' + (char)((seed >> 16) % 26);
        }
        name[nl] = 0;
        if (sh_file_find(name)) { k_strcpy(out, "mktemp: name collision"); return 1; }
        i32 slot = -1;
        for (i32 k = 0; k < SH_FILES; k++) if (!sh_files[k].used) { slot = k; break; }
        if (slot < 0) { k_strcpy(out, "mktemp: shfs full"); return 1; }
        sh_files[slot].used = true; sh_files[slot].is_dir = false;
        sh_files[slot].len = 0; sh_files[slot].data[0] = 0;
        k_strcpy(sh_files[slot].name, name);
        k_strcpy(out, name);
        return 0;
    }
    if (k_strcmp(cmd, "tty") == 0)    { k_strcpy(out, "/dev/tty0"); return 0; }
    if (k_strcmp(cmd, "logname") == 0){
        const falcon_user_t *u = users_at(SET.active_user);
        k_strcpy(out, (u && u->name[0]) ? u->name : "falcon"); return 0;
    }
    if (k_strcmp(cmd, "nproc") == 0) { k_strcpy(out, "1"); return 0; }
    if (k_strcmp(cmd, "arch") == 0)  { k_strcpy(out, "x86_64"); return 0; }
    if (k_strcmp(cmd, "machine") == 0) { k_strcpy(out, "FalconOS-1"); return 0; }
    if (k_strcmp(cmd, "lsmem") == 0) {
        k_strcpy(out, "RANGE             SIZE  STATE\n0x000000-0x1FFFFFFF  512M  online (kernel image + framebuffer + BSS)");
        return 0;
    }
    if (k_strcmp(cmd, "uptime-pretty") == 0) {
        u32 hh = 0, mm = 0, ss = 0; pit_uptime(&hh, &mm, &ss);
        char num[12];
        k_strcpy(out, "up "); k_itoa(hh, num, 10); k_strcat(out, num); k_strcat(out, "h ");
        k_itoa(mm, num, 10); k_strcat(out, num); k_strcat(out, "m ");
        k_itoa(ss, num, 10); k_strcat(out, num); k_strcat(out, "s");
        return 0;
    }
    if (k_strcmp(cmd, "diff") == 0 || k_strcmp(cmd, "cmp") == 0) {
        if (argc < 3) { k_strcpy(out, cmd); k_strcat(out, ": need 2 files"); return 1; }
        shfile_t *a = sh_resolve(argv[1]);
        shfile_t *b = sh_resolve(argv[2]);
        if (!a || !b) { k_strcpy(out, cmd); k_strcat(out, ": file missing"); return 1; }
        if (a->len != b->len) {
            k_strcpy(out, "files differ (size: ");
            char n[12]; k_itoa(a->len, n, 10); k_strcat(out, n); k_strcat(out, " vs ");
            k_itoa(b->len, n, 10); k_strcat(out, n); k_strcat(out, ")");
            return 1;
        }
        for (u32 i = 0; i < a->len; i++) if (a->data[i] != b->data[i]) {
            char n[12]; k_strcpy(out, "first differ at byte "); k_itoa(i, n, 10); k_strcat(out, n);
            return 1;
        }
        out[0] = 0; return 0;
    }
    if (k_strcmp(cmd, "md5sum") == 0 || k_strcmp(cmd, "sha256sum") == 0) {
        /* Bare-metal placeholder: emit a deterministic hash by simply
         * folding the bytes into a 64-bit accumulator and hex-printing.  
         * Not crypto, but unique-per-content and good enough for "did
         * this change" checks at the prompt. */
        if (argc < 2) { k_strcpy(out, cmd); k_strcat(out, ": missing operand"); return 1; }
        shfile_t *f = sh_resolve(argv[1]);
        if (!f) { k_strcpy(out, cmd); k_strcat(out, ": no such file"); return 1; }
        u64 acc = 0x1505u;
        for (u32 i = 0; i < f->len; i++)
            acc = ((acc << 5) + acc) ^ (u8)f->data[i];
        char hex[17];
        for (i32 i = 15; i >= 0; i--) {
            u8 d = (u8)(acc & 0xF); acc >>= 4;
            hex[i] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        }
        hex[16] = 0;
        k_strcpy(out, hex); k_strcat(out, "  "); k_strcat(out, f->name);
        return 0;
    }
    if (k_strcmp(cmd, "env") == 0) {
        out[0] = 0;
        for (i32 i = 0; i < SH_VARS; i++) {
            if (!sh_vars[i].used) continue;
            k_strcat(out, sh_vars[i].name); k_strcat(out, "=");
            k_strcat(out, sh_vars[i].value); k_strcat(out, "\n");
        }
        if (!out[0]) k_strcpy(out, "PATH=/bin\nHOME=/home/falcon\nSHELL=/bin/falcon-sh");
        return 0;
    }
    if (k_strcmp(cmd, "which") == 0) {
        if (argc < 2) { k_strcpy(out, "which: missing operand"); return 1; }
        /* "everything is a builtin" — report /usr/bin/<name>           */
        k_strcpy(out, "/usr/bin/"); k_strcat(out, argv[1]);
        return 0;
    }
    if (k_strcmp(cmd, "whereis") == 0) {
        if (argc < 2) { k_strcpy(out, "whereis: missing operand"); return 1; }
        k_strcpy(out, argv[1]); k_strcat(out, ": /usr/bin/"); k_strcat(out, argv[1]);
        return 0;
    }
    if (k_strcmp(cmd, "ip") == 0) {
        if (!net_present()) { k_strcpy(out, "no link"); return 1; }
        char mac[32]; net_mac_string(mac);
        k_strcpy(out, "1: virtio0  link/ether ");
        k_strcat(out, mac);
        k_strcat(out, net_connected() ? "  state UP" : "  state DOWN");
        if (net_connected()) {
            k_strcat(out, "\n    inet "); k_strcat(out, net_ip_addr());
            k_strcat(out, "/24 gw "); k_strcat(out, net_gateway());
        }
        return 0;
    }
    if (k_strcmp(cmd, "dmesg") == 0) {
        k_strcpy(out,
            "[    0.000000] FalconOS 1 x86_64 long-mode boot\n"
            "[    0.000123] gdt: tss installed (rsp0 -> ist0)\n"
            "[    0.000201] idt: 256 vectors, exc 0..31, irq 0..15\n"
            "[    0.000334] pic remapped 0x20..0x2F, irq mask 0xFFFD\n"
            "[    0.001005] pit: 100Hz tick\n"
            "[    0.010222] ata: primary master = QEMU HARDDISK\n"
            "[    0.022100] fb: 2560x1440 32bpp at 0xFD000000\n"
            "[    0.030000] kbd: ps/2 layout=TR-Q\n"
            "[    0.030100] mouse: ps/2 enabled, sample 100Hz\n"
            "[    0.500000] prg: catalogue loaded (158 entries)\n");
        return 0;
    }
    if (k_strcmp(cmd, "lspci") == 0) {
        k_strcpy(out,
            "00:00.0 Host bridge: Intel 440FX (QEMU)\n"
            "00:01.0 ISA bridge: PIIX3\n"
            "00:01.1 IDE controller: PIIX3\n"
            "00:02.0 VGA: stdvga\n"
            "00:03.0 Ethernet: virtio-net (DOWN)\n");
        return 0;
    }
    if (k_strcmp(cmd, "lsusb") == 0) {
        k_strcpy(out,
            "Bus 001 Device 001: 1d6b:0001 Linux UHCI root hub\n"
            "Bus 001 Device 002: 0627:0001 QEMU USB Tablet\n");
        return 0;
    }
    if (k_strcmp(cmd, "blkid") == 0) {
        k_strcpy(out, "/dev/sda: TYPE=\"falconfs\" UUID=\"FALC-0001\"\n");
        return 0;
    }
    if (k_strcmp(cmd, "service") == 0 || k_strcmp(cmd, "systemctl") == 0) {
        if (argc >= 3) {
            k_strcpy(out, cmd); k_strcat(out, ": ");
            k_strcat(out, argv[2]); k_strcat(out, " — managed");
            return 0;
        }
        k_strcpy(out, cmd); k_strcat(out, ": init system stub (FalconOS uses cooperative scheduling)");
        return 0;
    }
    if (k_strcmp(cmd, "fortune") == 0) {
        static const char *F[] = {
            "Inanc dağları yerinden oynatır; dahası onları yeniden derler.",
            "git push origin life",
            "Real artists ship.",
            "Read the code, not the manual.",
            "Hello, FalconOS!",
        };
        i32 idx = (i32)((g_ticks ^ 0xAB) % 5);
        k_strcpy(out, F[idx]); return 0;
    }
    if (k_strcmp(cmd, "banner") == 0) {
        const char *t = (argc >= 2) ? argv[1] : "FalconOS";
        k_strcpy(out, "##\n## "); k_strcat(out, t); k_strcat(out, "\n##");
        return 0;
    }
    if (k_strcmp(cmd, "cowsay") == 0) {
        const char *t = (argc >= 2) ? argv[1] : "Moo!";
        k_strcpy(out, " ___\n< "); k_strcat(out, t);
        k_strcat(out, " >\n ---\n        \\   ^__^\n         \\  (oo)\\_______\n            (__)\\       )\\/\\\n                ||----w |\n                ||     ||");
        return 0;
    }

    /* unknown */
    k_strcpy(out, cmd); k_strcat(out, ": command not found");
    return 127;
}

/* Find the first occurrence of needle in haystack, returning its index
 * or -1.  We can't rely on libc.                                     */
static i32 sh_find(const char *hay, const char *needle)
{
    i32 nl = k_strlen(needle);
    for (i32 i = 0; hay[i]; i++) {
        bool ok = true;
        for (i32 j = 0; j < nl && ok; j++)
            if (hay[i + j] != needle[j]) ok = false;
        if (ok) return i;
    }
    return -1;
}

static void sh_emit(const char *s, char *out, i32 cap)
{
    if (!out) { term_push(s); return; }
    i32 k = k_strlen(out);
    while (*s && k < cap - 1) out[k++] = *s++;
    out[k] = 0;
}

/* Run one statement (already split on ';'). Recurses for if/for. */
static void sh_run_stmt(const char *line, char *out, i32 cap)
{
    /* skip leading spaces */
    while (sh_isspace(*line)) line++;
    if (!*line) return;

    /* if cmd ; then cmd ; fi */
    if (k_strncmp(line, "if ", 3) == 0) {
        i32 t = sh_find(line, " then ");
        i32 f = sh_find(line, " fi");
        if (t > 0 && f > t) {
            char cond[128], body[128];
            i32 ci = 0;
            for (i32 i = 3; i < t && ci < 127; i++) cond[ci++] = line[i];
            cond[ci] = 0;
            i32 bi = 0;
            for (i32 i = t + 6; i < f && bi < 127; i++) body[bi++] = line[i];
            body[bi] = 0;
            char buf[64]; sh_run_stmt(cond, buf, sizeof buf);
            /* exit status 0 from cond means "true": run body. We
             * approximate by checking whether `true` / non-`false` was
             * the cond first arg.                                    */
            char tok[1][64];
            i32 nt = sh_tokenise(cond, tok, 1);
            i32 truthy = (nt > 0 && k_strcmp(tok[0], "false") != 0);
            if (truthy) sh_run_stmt(body, out, cap);
        }
        return;
    }
    /* for x in a b c ; do cmd ; done */
    if (k_strncmp(line, "for ", 4) == 0) {
        i32 in_idx = sh_find(line, " in ");
        i32 do_idx = sh_find(line, " do ");
        i32 dn_idx = sh_find(line, " done");
        if (in_idx > 0 && do_idx > in_idx && dn_idx > do_idx) {
            char var[16]; i32 vi = 0;
            for (i32 i = 4; i < in_idx && vi < 15; i++) var[vi++] = line[i];
            var[vi] = 0;
            char list[128]; i32 li = 0;
            for (i32 i = in_idx + 4; i < do_idx && li < 127; i++) list[li++] = line[i];
            list[li] = 0;
            char body[128]; i32 bi = 0;
            for (i32 i = do_idx + 4; i < dn_idx && bi < 127; i++) body[bi++] = line[i];
            body[bi] = 0;
            char items[8][64];
            i32 nitems = sh_tokenise(list, items, 8);
            for (i32 i = 0; i < nitems; i++) {
                sh_var_set(var, items[i]);
                sh_run_stmt(body, out, cap);
            }
        }
        return;
    }

    /* pipe + redirect: split at first '|', then handle '>' on RHS too */
    char left[128] = {0}, right[128] = {0};
    bool has_pipe = false;
    i32 pi = sh_find(line, "|");
    if (pi >= 0) {
        for (i32 i = 0; i < pi && i < 127; i++) left[i] = line[i];
        i32 r = 0; for (i32 i = pi + 1; line[i] && r < 127; i++) right[r++] = line[i];
        has_pipe = true;
    } else {
        k_strcpy(left, line);
    }

    /* output redirect on left side */
    char redir_name[SH_PATHLEN] = {0};
    bool append = false;
    {
        i32 gg = sh_find(left, ">>");
        if (gg >= 0) {
            append = true;
            char fn[64]; i32 k = 0;
            for (i32 i = gg + 2; left[i] && k < 63; i++) if (!sh_isspace(left[i]) || k) fn[k++] = left[i];
            fn[k] = 0;
            /* strip trailing spaces in name */
            while (k > 0 && sh_isspace(fn[k - 1])) fn[--k] = 0;
            sh_path_join(fn, redir_name);
            left[gg] = 0;
        } else {
            i32 g = sh_find(left, ">");
            if (g >= 0) {
                char fn[64]; i32 k = 0;
                for (i32 i = g + 1; left[i] && k < 63; i++) if (!sh_isspace(left[i]) || k) fn[k++] = left[i];
                fn[k] = 0;
                while (k > 0 && sh_isspace(fn[k - 1])) fn[--k] = 0;
                sh_path_join(fn, redir_name);
                left[g] = 0;
            }
        }
    }

    /* run left */
    char larg[8][64];
    i32 lac = sh_tokenise(left, larg, 8);
    char lout[256];
    sh_run_argv(lac, larg, lout, sizeof lout);

    if (redir_name[0]) {
        shfile_t *f = sh_file_open_w(redir_name, append);
        if (f) {
            u32 n = f->len;
            for (i32 i = 0; lout[i] && n < SH_FBYTES - 2; i++) f->data[n++] = lout[i];
            f->data[n++] = '\n';
            f->data[n] = 0;
            f->len = n;
        }
        return;
    }

    if (has_pipe) {
        /* feed lout as $_ to right side */
        sh_var_set("_", lout);
        char rarg[8][64];
        i32 rac = sh_tokenise(right, rarg, 8);
        char rout[256];
        /* if right is `cat`/`grep`/`wc`, treat lout as input. We
         * simplify: if right is `wc`, count words; if `grep <pat>`,
         * filter lines containing pat; otherwise fall back to running
         * the right command and concatenating its own output.        */
        if (rac > 0 && k_strcmp(rarg[0], "wc") == 0) {
            i32 nw = 0; bool in = false;
            for (i32 i = 0; lout[i]; i++) {
                if (sh_isspace(lout[i])) in = false;
                else if (!in) { in = true; nw++; }
            }
            char num[16]; k_itoa((u32)nw, num, 10);
            sh_emit(num, out, cap);
        } else if (rac >= 2 && k_strcmp(rarg[0], "grep") == 0) {
            const char *pat = rarg[1];
            i32 i = 0; bool any = false;
            while (lout[i]) {
                i32 j = i; while (lout[j] && lout[j] != '\n') j++;
                /* line is lout[i..j) */
                bool match = false;
                for (i32 s = i; s < j && !match; s++) {
                    bool ok = true;
                    for (i32 t = 0; pat[t] && ok; t++)
                        if (s + t >= j || lout[s + t] != pat[t]) ok = false;
                    if (ok) match = true;
                }
                if (match) {
                    if (any) sh_emit("\n", out, cap);
                    char tmp[160]; i32 k = 0;
                    for (i32 s = i; s < j && k < 159; s++) tmp[k++] = lout[s];
                    tmp[k] = 0;
                    sh_emit(tmp, out, cap);
                    any = true;
                }
                i = j; if (lout[i]) i++;
            }
        } else {
            sh_run_argv(rac, rarg, rout, sizeof rout);
            sh_emit(rout, out, cap);
        }
        return;
    }

    sh_emit(lout, out, cap);
}

static void sh_run_line(const char *line)
{
    /* split on ';' */
    char buf[128];
    i32 bi = 0;
    for (i32 i = 0; line[i] && i < 127; i++) {
        if (line[i] == ';') {
            buf[bi] = 0;
            char outbuf[256]; outbuf[0] = 0;
            sh_run_stmt(buf, outbuf, sizeof outbuf);
            if (outbuf[0]) {
                /* split on '\n' so multi-line output scrolls properly */
                i32 s = 0;
                for (i32 j = 0; ; j++) {
                    if (outbuf[j] == '\n' || outbuf[j] == 0) {
                        char tmp[TERM_COLS];
                        i32 k = 0;
                        for (i32 t = s; t < j && k < TERM_COLS - 1; t++) tmp[k++] = outbuf[t];
                        tmp[k] = 0;
                        if (k) term_push(tmp);
                        if (outbuf[j] == 0) break;
                        s = j + 1;
                    }
                }
            }
            bi = 0;
        } else {
            buf[bi++] = line[i];
        }
    }
    buf[bi] = 0;
    if (bi == 0) return;
    char outbuf[256]; outbuf[0] = 0;
    sh_run_stmt(buf, outbuf, sizeof outbuf);
    if (outbuf[0]) {
        i32 s = 0;
        for (i32 j = 0; ; j++) {
            if (outbuf[j] == '\n' || outbuf[j] == 0) {
                char tmp[TERM_COLS];
                i32 k = 0;
                for (i32 t = s; t < j && k < TERM_COLS - 1; t++) tmp[k++] = outbuf[t];
                tmp[k] = 0;
                if (k) term_push(tmp);
                if (outbuf[j] == 0) break;
                s = j + 1;
            }
        }
    }
}

static void render_term(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame;
    term_init();
    /* dark terminal panel for contrast against light theme */
    gfx_round_rect_a(wx + 20, wy + 8, ww - 40, wh - 28, 10, 0x101218, 255);
    gfx_round_outline(wx + 20, wy + 8, ww - 40, wh - 28, 10, PAL_HAIRLINE);

    gfx_text(wx + 30, wy + 18,
             T("FalconOS shell — POSIX subset", "FalconOS kabuğu — POSIX alt kümesi"),
             0xCFE6FF);
    gfx_text(wx + 30, wy + 38,
             T("Try: help · hwinfo · prg · update check — cat readme.txt",
              "Deneyin: help · hwinfo · prg · update check — cat readme.txt"),
             0x8AAACE);

    for (i32 i = 0; i < TERM_LINES; i++) {
        gfx_text(wx + 32, wy + 64 + i * 18, term_buf[i], 0xDEEEFF);
    }
    /* current input line */
    char line[TERM_COLS + 24];
    k_strcpy(line, "[");
    const falcon_user_t *u = users_at(SET.active_user);
    k_strcat(line, u ? u->name : "falcon");
    k_strcat(line, "@falcon ");
    /* show only the basename of CWD to keep prompt short */
    const char *base = sh_cwd; for (i32 i = 0; sh_cwd[i]; i++) if (sh_cwd[i] == '/') base = &sh_cwd[i + 1];
    k_strcat(line, base[0] ? base : "/");
    k_strcat(line, "]$ ");
    k_strcat(line, term_input);
    if ((g_ticks / 50) & 1) k_strcat(line, "_");
    gfx_text(wx + 32, wy + 64 + TERM_LINES * 18, line, COL_OK);
}

static void term_input_key(i32 key)
{
    term_init();
    if (key == KEY_ENTER) {
        char prompt[TERM_COLS];
        k_strcpy(prompt, "$ ");
        k_strcat(prompt, term_input);
        term_push(prompt);
        sh_run_line(term_input);
        term_input_len = 0;
        term_input[0]  = 0;
        return;
    }
    if (key == KEY_BACKSPACE) {
        sh_buf_pop_utf8(term_input, &term_input_len);
        return;
    }
    (void)sh_buf_append_key(term_input, &term_input_len, TERM_COLS, key);
}

/* --- Files: real shfs browser ------------------------------------------- *
 *  Files is a UI on top of the same sh_files[] array Terminal exposes —
 *  whatever you mkdir / touch from Terminal shows up here, and any folder
 *  / file you create via F2/F3 in the Files window is immediately listed
 *  in `ls` and openable with `cat`.  No second backing store.            */
#define FILES_MAX_ENTRIES 96
static char files_cwd[SH_PATHLEN] = "/home/falcon";
static i32  files_sel             = 0;
static i32  files_mode            = 0;   /* 0 browse, 1 new-dir, 2 new-file, 3 view */
static char files_input[64]       = "";
static i32  files_input_len       = 0;
static char files_status[128]     = "";
static i32  files_view_slot       = -1;  /* file index for view mode      */

static i32 files_collect(i32 out_slots[FILES_MAX_ENTRIES])
{
    i32 plen = k_strlen(files_cwd);
    bool root = (plen == 1 && files_cwd[0] == '/');
    i32 n = 0;
    for (i32 i = 0; i < SH_FILES && n < FILES_MAX_ENTRIES; i++) {
        if (!sh_files[i].used) continue;
        const char *p = sh_files[i].name;
        i32 j = 0;
        while (j < plen && p[j] == files_cwd[j]) j++;
        if (j != plen) continue;
        if (!root && p[j] != '/') continue;
        const char *child = root ? &p[1] : &p[j + 1];
        if (!*child) continue;
        bool nested = false;
        for (const char *q = child; *q; q++) if (*q == '/') { nested = true; break; }
        if (nested) continue;
        out_slots[n++] = i;
    }
    /* sort: dirs first, then by name */
    for (i32 a = 0; a < n - 1; a++) {
        for (i32 b = a + 1; b < n; b++) {
            shfile_t *fa = &sh_files[out_slots[a]];
            shfile_t *fb = &sh_files[out_slots[b]];
            bool swap = false;
            if (fa->is_dir != fb->is_dir) swap = (!fa->is_dir && fb->is_dir);
            else swap = (k_strcmp(fa->name, fb->name) > 0);
            if (swap) { i32 t = out_slots[a]; out_slots[a] = out_slots[b]; out_slots[b] = t; }
        }
    }
    return n;
}

static void render_files(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame;
    section(wx, wy, T("Files", "Dosyalar"),
            T("F2 new folder, F3 new file, Del delete, Enter open",
              "F2 yeni klasör, F3 yeni dosya, Del sil, Enter aç"));

    /* current path bar */
    i32 px = wx + 22, py = wy + 56, pw = ww - 44, ph = 28;
    gfx_round_rect_a(px, py, pw, ph, 8, PAL_PANEL_DEEP, 255);
    gfx_round_outline(px, py, pw, ph, 8, PAL_HAIRLINE);
    char pathline[SH_PATHLEN + 4];
    k_strcpy(pathline, " ");
    k_strcat(pathline, files_cwd);
    gfx_text(px + 8, py + 6, pathline, PAL_TEXT);

    /* directory listing */
    i32 lx = wx + 22, ly = wy + 96, lw = ww - 44, lh = wh - 180;
    gfx_round_rect_a(lx, ly, lw, lh, 8, PAL_PANEL_DEEP, 255);
    gfx_round_outline(lx, ly, lw, lh, 8, PAL_HAIRLINE);

    if (files_mode == 3 && files_view_slot >= 0 &&
        files_view_slot < SH_FILES && sh_files[files_view_slot].used) {
        /* file viewer */
        shfile_t *f = &sh_files[files_view_slot];
        gfx_text(lx + 12, ly + 10, sh_path_basename(f->name), PAL_ACCENT);
        gfx_text(lx + 12, ly + 30,
                 T("Esc to return", "Esc geri dön"), PAL_TEXT_DIM);
        i32 row = 0;
        i32 max_rows = (lh - 60) / 16;
        i32 i = 0;
        while ((u32)i < f->len && row < max_rows) {
            char line[80]; i32 lk = 0;
            while ((u32)i < f->len && f->data[i] != '\n' && lk < 78) {
                line[lk++] = f->data[i++];
            }
            line[lk] = 0;
            if ((u32)i < f->len && f->data[i] == '\n') i++;
            gfx_text(lx + 12, ly + 54 + row * 16, line, PAL_TEXT);
            row++;
        }
    } else {
        i32 slots[FILES_MAX_ENTRIES];
        i32 n = files_collect(slots);
        if (files_sel >= n) files_sel = n > 0 ? n - 1 : 0;
        if (files_sel < 0)  files_sel = 0;
        i32 max_rows = (lh - 16) / 22;
        i32 first = 0;
        if (files_sel >= max_rows) first = files_sel - max_rows + 1;
        if (n == 0) {
            gfx_text(lx + 12, ly + 14,
                     T("(empty folder — press F2 / F3 to create)",
                       "(boş klasör — F2 / F3 ile oluştur)"),
                     PAL_TEXT_DIM);
        }
        for (i32 i = 0; i < n && i - first < max_rows; i++) {
            if (i < first) continue;
            i32 y = ly + 8 + (i - first) * 22;
            shfile_t *f = &sh_files[slots[i]];
            if (i == files_sel) {
                gfx_round_rect_a(lx + 4, y - 2, lw - 8, 20, 6, PAL_ACCENT, 60);
            }
            u32 icon_col = f->is_dir ? 0xF6B144 : 0x6AAFFF;
            if (f->is_dir) {
                gfx_rect(lx + 16, y + 4, 16, 11, icon_col);
                gfx_rect(lx + 16, y + 1, 7,  4,  icon_col);
            } else {
                gfx_rect(lx + 18, y + 1, 12, 14, icon_col);
            }
            gfx_text(lx + 40, y + 1, sh_path_basename(f->name), PAL_TEXT);
            if (!f->is_dir) {
                char sz[16]; k_itoa(f->len, sz, 10);
                k_strcat(sz, "B");
                gfx_text(lx + lw - 80, y + 1, sz, PAL_TEXT_DIM);
            } else {
                gfx_text(lx + lw - 80, y + 1, "DIR", PAL_TEXT_DIM);
            }
        }
    }

    /* input prompt when creating */
    if (files_mode == 1 || files_mode == 2) {
        i32 ix = wx + 22, iy = wy + wh - 70, iw = ww - 44, ih = 36;
        gfx_round_rect_a(ix, iy, iw, ih, 10, PAL_PANEL, 220);
        gfx_round_outline(ix, iy, iw, ih, 10, PAL_ACCENT);
        gfx_text(ix + 10, iy + 8,
                 files_mode == 1
                    ? T("New folder: ", "Yeni klasör: ")
                    : T("New file: ",   "Yeni dosya: "),
                 PAL_TEXT_DIM);
        char buf[80];
        k_strcpy(buf, files_input);
        if ((g_ticks / 50) & 1) k_strcat(buf, "_");
        gfx_text(ix + 130, iy + 8, buf, PAL_TEXT);
    }
    /* status bar */
    gfx_text(wx + 22, wy + wh - 28, files_status, PAL_TEXT_DIM);
}

static void files_set_status(const char *s)
{
    i32 i = 0;
    while (s[i] && i < (i32)sizeof files_status - 1) {
        files_status[i] = s[i]; i++;
    }
    files_status[i] = 0;
}

static void files_input_key(i32 key)
{
    if (files_mode == 3) {
        if (key == KEY_ESC || key == KEY_BACKSPACE) {
            files_mode = 0; files_view_slot = -1;
        }
        return;
    }
    if (files_mode == 1 || files_mode == 2) {
        if (key == KEY_ESC) { files_mode = 0; files_input_len = 0; files_input[0] = 0; return; }
        if (key == KEY_BACKSPACE) {
            if (files_input_len > 0) {
                files_input_len--;
                files_input[files_input_len] = 0;
            }
            return;
        }
        if (key == KEY_ENTER) {
            if (files_input_len > 0) {
                char abs[SH_PATHLEN];
                /* join cwd + input */
                i32 ci = 0;
                while (files_cwd[ci] && ci < SH_PATHLEN - 1) { abs[ci] = files_cwd[ci]; ci++; }
                if (ci > 0 && abs[ci - 1] != '/' && ci < SH_PATHLEN - 1) abs[ci++] = '/';
                i32 j = 0;
                while (files_input[j] && ci < SH_PATHLEN - 1) abs[ci++] = files_input[j++];
                abs[ci] = 0;
                if (sh_file_find(abs)) {
                    files_set_status(T("error: name already exists",
                                       "hata: bu isim zaten var"));
                } else {
                    i32 slot = -1;
                    for (i32 i = 0; i < SH_FILES; i++) if (!sh_files[i].used) { slot = i; break; }
                    if (slot < 0) {
                        files_set_status(T("error: shfs full",
                                           "hata: dosya sistemi dolu"));
                    } else {
                        sh_files[slot].used   = true;
                        sh_files[slot].is_dir = (files_mode == 1);
                        sh_files[slot].len    = 0;
                        sh_files[slot].data[0] = 0;
                        k_strcpy(sh_files[slot].name, abs);
                        files_set_status(files_mode == 1
                            ? T("created folder",  "klasör oluşturuldu")
                            : T("created file",    "dosya oluşturuldu"));
                    }
                }
            }
            files_mode = 0; files_input_len = 0; files_input[0] = 0;
            return;
        }
        if (key >= ' ' && key < 0x7F && files_input_len < (i32)sizeof files_input - 1) {
            char c = (char)key;
            /* forbid slashes inside names */
            if (c != '/') {
                files_input[files_input_len++] = c;
                files_input[files_input_len]   = 0;
            }
        }
        return;
    }
    /* browse mode */
    if (key == KEY_F2 || key == 'N' || key == 'n') {
        files_mode = 1; files_input_len = 0; files_input[0] = 0;
        files_set_status("");
        return;
    }
    if (key == KEY_F3 || key == 'F' || key == 'f') {
        files_mode = 2; files_input_len = 0; files_input[0] = 0;
        files_set_status("");
        return;
    }
    if (key == KEY_UP)   { files_sel--;  return; }
    if (key == KEY_DOWN) { files_sel++;  return; }
    if (key == KEY_BACKSPACE) {
        if (k_strcmp(files_cwd, "/") != 0) {
            char p[SH_PATHLEN];
            sh_path_parent(files_cwd, p);
            k_strcpy(files_cwd, p);
            files_sel = 0;
        }
        return;
    }
    if (key == KEY_DEL) {
        i32 slots[FILES_MAX_ENTRIES];
        i32 n = files_collect(slots);
        if (files_sel >= 0 && files_sel < n) {
            shfile_t *f = &sh_files[slots[files_sel]];
            if (f->is_dir) {
                /* must be empty */
                i32 alen = k_strlen(f->name);
                bool empty = true;
                for (i32 i = 0; i < SH_FILES; i++) {
                    if (!sh_files[i].used || &sh_files[i] == f) continue;
                    const char *p = sh_files[i].name;
                    if (k_strncmp(p, f->name, alen) == 0 &&
                        (p[alen] == '/' || (alen == 1 && p[0] == '/'))) {
                        empty = false; break;
                    }
                }
                if (!empty) {
                    files_set_status(T("folder not empty",
                                       "klasör boş değil"));
                    return;
                }
            }
            f->used = false;
            files_set_status(T("deleted",
                               "silindi"));
            if (files_sel >= n - 1 && files_sel > 0) files_sel--;
        }
        return;
    }
    if (key == KEY_ENTER) {
        i32 slots[FILES_MAX_ENTRIES];
        i32 n = files_collect(slots);
        if (files_sel >= 0 && files_sel < n) {
            shfile_t *f = &sh_files[slots[files_sel]];
            if (f->is_dir) {
                k_strcpy(files_cwd, f->name);
                files_sel = 0;
                files_set_status("");
            } else {
                files_mode = 3;
                files_view_slot = slots[files_sel];
            }
        }
        return;
    }
}

/* --- Calculator ---------------------------------------------------------- */
static i32  calc_acc      = 0;
static i32  calc_buf      = 0;
static char calc_op       = '+';
static bool calc_has_buf  = false;
static char calc_disp[24] = "0";

static void calc_refresh_disp(void)
{
    i32 v = calc_has_buf ? calc_buf : calc_acc;
    char tmp[16];
    bool neg = (v < 0);
    if (neg) v = -v;
    k_itoa((u32)v, tmp, 10);
    k_strcpy(calc_disp, neg ? "-" : "");
    k_strcat(calc_disp, tmp);
}
static void calc_apply(void)
{
    if (!calc_has_buf) return;
    switch (calc_op) {
    case '+': calc_acc += calc_buf; break;
    case '-': calc_acc -= calc_buf; break;
    case '*': calc_acc *= calc_buf; break;
    case '/': calc_acc = calc_buf ? calc_acc / calc_buf : 0; break;
    }
    calc_buf = 0;
    calc_has_buf = false;
    calc_refresh_disp();
}
static void calc_input_key(i32 key)
{
    if (key >= '0' && key <= '9') {
        calc_buf = calc_buf * 10 + (key - '0');
        calc_has_buf = true;
        calc_refresh_disp();
        return;
    }
    if (key == '+' || key == '-' || key == '*' || key == '/') {
        calc_apply();
        calc_op = (char)key;
        return;
    }
    if (key == KEY_ENTER || key == '=') { calc_apply(); return; }
    if (key == 'c' || key == 'C')        {
        calc_acc = calc_buf = 0; calc_has_buf = false; calc_op = '+';
        calc_refresh_disp(); return;
    }
}
static void render_calc(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame; (void)wh;
    section(wx, wy, T("Calculator", "Hesap makinesi"),
            T("+ − × ÷ digits  c=clear  Enter =", "+ − × ÷ rakamlar c=temizle Enter ="));

    /* readout */
    i32 dx = wx + 24, dy = wy + 60, dw = ww - 48, dh = 60;
    gfx_round_rect_a(dx, dy, dw, dh, 12, PAL_PANEL_DEEP, 255);
    gfx_round_outline(dx, dy, dw, dh, 12, PAL_HAIRLINE);
    gfx_text(dx + dw - gfx_text_width(calc_disp) - 16, dy + 22, calc_disp, PAL_TEXT);

    /* op + acc indicator */
    char hint[40] = "acc ";
    char tmp[12];
    bool neg = (calc_acc < 0);
    k_itoa(neg ? (u32)(-calc_acc) : (u32)calc_acc, tmp, 10);
    if (neg) k_strcat(hint, "-");
    k_strcat(hint, tmp);
    k_strcat(hint, "   op ");
    char opbuf[2] = {calc_op, 0};
    k_strcat(hint, opbuf);
    gfx_text(dx + 16, dy + 22, hint, PAL_TEXT_DIM);

    /* on-screen keypad (visual only) */
    const char *keys = "789+456-123*0=c/";
    i32 bx = wx + 24, by = wy + 140, bw = (ww - 48) / 4 - 6, bh = 36;
    for (i32 i = 0; i < 16; i++) {
        i32 r = i / 4, c = i % 4;
        i32 x = bx + c * (bw + 8), y = by + r * (bh + 8);
        bool op = (c == 3) || keys[i] == '=' || keys[i] == 'c';
        gfx_round_rect_a(x, y, bw, bh, 8, op ? PAL_ACCENT_DIM : PAL_PANEL_DEEP, 255);
        gfx_round_outline(x, y, bw, bh, 8, PAL_HAIRLINE);
        char b[2] = {keys[i], 0};
        gfx_text_centered(x + bw / 2, y + bh / 2 - 8, b, PAL_TEXT);
    }
}

/* --- Settings (v5: live, persistent across the kernel) ------------------- */
/*  Settings is a multi-row form; the user selects a row with up/down,
 *  and uses left/right (or Enter) to toggle / cycle the value.            */
typedef enum {
    SR_THEME = 0, SR_ACCENT, SR_AERO, SR_LANG, SR_KBD, SR_TZ, SR_DOCK,
    SR_ANIM, SR_WIDGETS, SR_VIEWPORT, SR_PASSWORD, SR_USERS, SR_DRIVERS,
    SR_NETWORK, SR_SAVE, SR_LOCK, SR_COUNT
} set_row_t;

static i32 set_row = 0;
static const char *VIEWPORT_NAMES[] = {
    "native", "1280x800", "1920x1080", "2560x1440", "1024x768",
};
static const i32 VIEWPORT_W[] = { 0, 1280, 1920, 2560, 1024 };
static const i32 VIEWPORT_H[] = { 0,  800, 1080, 1440,  768 };
static i32 set_viewport_idx = 0;

/* Curated timezone presets — covers the cities our target users live in
 * without dragging in a full zoneinfo / DST database.                   */
typedef struct { const char *label; i32 minutes; } tz_preset_t;
static const tz_preset_t TZ_PRESETS[] = {
    { "Honolulu (UTC-10)",  -600 },
    { "Los Angeles (UTC-8)", -480 },
    { "New York (UTC-5)",    -300 },
    { "Sao Paulo (UTC-3)",   -180 },
    { "London (UTC+0)",         0 },
    { "Paris (UTC+1)",         60 },
    { "Istanbul (UTC+3)",     180 },
    { "Dubai (UTC+4)",        240 },
    { "Mumbai (UTC+5:30)",    330 },
    { "Bangkok (UTC+7)",      420 },
    { "Beijing (UTC+8)",      480 },
    { "Tokyo (UTC+9)",        540 },
    { "Sydney (UTC+10)",      600 },
};
#define TZ_PRESET_COUNT ((i32)(sizeof TZ_PRESETS / sizeof *TZ_PRESETS))

static i32 tz_preset_index(void)
{
    for (i32 i = 0; i < TZ_PRESET_COUNT; i++)
        if (TZ_PRESETS[i].minutes == SET.tz_minutes) return i;
    return 6; /* Istanbul */
}

static char set_pwd[24];
static i32  set_pwd_len = 0;
static bool set_pwd_editing = false;

static void set_input_key(i32 key)
{
    if (set_pwd_editing) {
        if (key == KEY_BACKSPACE) { sh_buf_pop_utf8(set_pwd, &set_pwd_len); return; }
        if (key == KEY_ENTER)     {
            set_pwd[set_pwd_len] = 0;
            k_strcpy(SET.password, set_pwd);
            set_pwd_editing = false;
            return;
        }
        if (key == KEY_ESC)       { set_pwd_editing = false; return; }
        (void)sh_buf_append_key(set_pwd, &set_pwd_len, 24, key);
        return;
    }

    if (key == KEY_UP   && set_row > 0)             set_row--;
    if (key == KEY_DOWN && set_row < SR_COUNT - 1)  set_row++;

    if (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_ENTER || key == ' ') {
        i32 d = (key == KEY_LEFT) ? -1 : 1;
        switch (set_row) {
        case SR_THEME: {
            i32 v = (i32)SET.theme + d;
            if (v < 0)             v = THEME_COUNT - 1;
            if (v >= THEME_COUNT)  v = 0;
            SET.theme = (theme_t)v;
            break;
        }
        case SR_ACCENT:
            { i32 v = (i32)SET.accent + d;
              if (v < 0)             v = ACC_COUNT - 1;
              if (v >= ACC_COUNT)    v = 0;
              SET.accent = (accent_t)v; }
            break;
        case SR_AERO:
            SET.aero_enabled = !SET.aero_enabled;
            break;
        case SR_LANG:
            { i32 v = (i32)SET.lang + d;
              if (v < 0)             v = LANG_COUNT - 1;
              if (v >= LANG_COUNT)   v = 0;
              SET.lang = (lang_t)v; }
            break;
        case SR_KBD:
            { i32 v = (i32)SET.kbd_layout + d;
              if (v < 0)               v = KBD_COUNT - 1;
              if (v >= KBD_COUNT)      v = 0;
              SET.kbd_layout = (kbd_layout_t)v; }
            break;
        case SR_TZ:
            { i32 v = (tz_preset_index() + d + TZ_PRESET_COUNT) % TZ_PRESET_COUNT;
              SET.tz_minutes = TZ_PRESETS[v].minutes; }
            break;
        case SR_DOCK:
            { i32 v = SET.dock_size + d; if (v < 0) v = 0; if (v > 4) v = 4;
              SET.dock_size = v; }
            break;
        case SR_ANIM:
            SET.animations = !SET.animations;
            break;
        case SR_WIDGETS:
            SET.widgets_shown = !SET.widgets_shown;
            break;
        case SR_VIEWPORT:
            { i32 n = (i32)(sizeof VIEWPORT_NAMES / sizeof *VIEWPORT_NAMES);
              i32 v = (set_viewport_idx + d + n) % n;
              set_viewport_idx = v;
              SET.viewport_w = VIEWPORT_W[v];
              SET.viewport_h = VIEWPORT_H[v]; }
            break;
        case SR_PASSWORD:
            set_pwd_editing = true;
            set_pwd_len = 0;
            set_pwd[0] = 0;
            break;
        case SR_USERS:
            /* cycle which user is "default" (auto-focused on next boot)   */
            for (i32 i = 0, hops = 0; i < FALCON_MAX_USERS && hops < 2; i++) {
                i32 idx = (SET.default_user + d + FALCON_MAX_USERS) % FALCON_MAX_USERS;
                if (SET.users[idx].in_use) { users_set_default(idx); break; }
                d += (d > 0) ? 1 : -1;
                hops++;
            }
            break;
        case SR_SAVE:
            diskdb_save();
            break;
        case SR_LOCK:
            lockscreen_lock();
            break;
        case SR_NETWORK:
            /* Enter requests DHCP. Left/Right toggle adapter present
             * (helps when you're walking through Settings without a
             * network device — also unlocks the Network app for demos). */
            if (d == 0) {
                (void)net_dhcp();        /* Enter pressed */
            }
            break;
        }
    }
}

#define SR_BOX_H 32

static void s_row(i32 x, i32 y, i32 w, const char *label, const char *val,
                  bool active, u32 valcolor)
{
    gfx_round_rect_a(x, y, w, SR_BOX_H, 9,
                     active ? PAL_ACCENT_DIM : PAL_PANEL_DEEP, 255);
    gfx_round_outline(x, y, w, SR_BOX_H, 9, active ? PAL_ACCENT : PAL_HAIRLINE);
    gfx_text(x + 14, y + 9, label, PAL_TEXT);
    gfx_text(x + w - gfx_text_width(val) - 14, y + 9, val, valcolor);
}

static void render_settings(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame; (void)wh;
    section(wx, wy, T("Settings", "Ayarlar"),
                    T("up/down  pick row    left/right  change",
                      "yukarı/aşağı  satır   sol/sağ  değiştir"));

    i32 sx = wx + 24, sy = wy + 56, sw = ww - 48;
    i32 step = 36;     /* row vertical pitch                            */
    char val[40];

    /* Theme ------------------------------------------------------------ */
    {
        const char *theme_names[THEME_COUNT] = {
            "Lumen (Light)", "Nox (Dark)", "Liquid Glass",
            "Nordic", "Rose Gold"
        };
        s_row(sx, sy + SR_THEME * step, sw,
              T("Theme", "Tema"),
              theme_names[SET.theme],
              set_row == SR_THEME, PAL_TEXT);
    }

    /* Accent ----------------------------------------------------------- */
    {
        const char *names[ACC_COUNT] = { "Blue", "Purple", "Green", "Pink", "Graphite" };
        s_row(sx, sy + SR_ACCENT * step, sw,
              T("Accent", "Vurgu"), names[SET.accent],
              set_row == SR_ACCENT, PAL_ACCENT);
        gfx_circle(sx + sw - 14, sy + SR_ACCENT * step + SR_BOX_H / 2, 6, PAL_ACCENT);
    }

    /* Aero --- frosted glass toggle ------------------------------------ */
    s_row(sx, sy + SR_AERO * step, sw,
          T("Aero (transparency)", "Aero (şeffaflık)"),
          SET.aero_enabled ? T("on", "açık") : T("off", "kapalı"),
          set_row == SR_AERO,
          SET.aero_enabled ? COL_OK : PAL_TEXT_DIM);

    /* Language --------------------------------------------------------- */
    s_row(sx, sy + SR_LANG * step, sw,
          TX("Language", "Dil", "Sprache", "Langue", "Idioma"),
          lang_name(SET.lang),
          set_row == SR_LANG, PAL_TEXT);

    /* Keyboard layout -------------------------------------------------- */
    s_row(sx, sy + SR_KBD * step, sw,
          T("Keyboard layout", "Klavye düzeni"),
          kbd_layout_name(SET.kbd_layout),
          set_row == SR_KBD, PAL_TEXT);

    /* Timezone --------------------------------------------------------- */
    s_row(sx, sy + SR_TZ * step, sw,
          T("Timezone", "Zaman dilimi"),
          TZ_PRESETS[tz_preset_index()].label,
          set_row == SR_TZ, PAL_TEXT);

    /* Dock size -------------------------------------------------------- */
    {
        char num[8]; k_itoa(50 + SET.dock_size * 9, num, 10);
        k_strcpy(val, num); k_strcat(val, " px");
        s_row(sx, sy + SR_DOCK * step, sw,
              T("Dock size", "Dock boyutu"), val,
              set_row == SR_DOCK, PAL_TEXT);
    }

    /* Animations ------------------------------------------------------- */
    s_row(sx, sy + SR_ANIM * step, sw,
          T("Animations", "Animasyonlar"),
          SET.animations ? T("on", "açık") : T("off", "kapalı"),
          set_row == SR_ANIM, SET.animations ? COL_OK : PAL_TEXT_DIM);

    /* Widgets ---------------------------------------------------------- */
    s_row(sx, sy + SR_WIDGETS * step, sw,
          T("Desktop widgets", "Masaüstü widgetlar"),
          SET.widgets_shown ? T("shown", "açık") : T("hidden", "gizli"),
          set_row == SR_WIDGETS,
          SET.widgets_shown ? COL_OK : PAL_TEXT_DIM);

    /* Viewport / resolution ------------------------------------------- */
    s_row(sx, sy + SR_VIEWPORT * step, sw,
          T("Resolution", "Çözünürlük"),
          VIEWPORT_NAMES[set_viewport_idx],
          set_row == SR_VIEWPORT, PAL_TEXT);

    /* Password --------------------------------------------------------- */
    {
        const char *p;
        if (set_pwd_editing) {
            static char masked[24];
            for (i32 i = 0; i < set_pwd_len; i++) masked[i] = '*';
            masked[set_pwd_len] = 0;
            p = masked;
        } else {
            p = (k_strlen(SET.password) ? T("set", "var") : T("none", "yok"));
        }
        s_row(sx, sy + SR_PASSWORD * step, sw,
              T("Password", "Parola"), p,
              set_row == SR_PASSWORD,
              k_strlen(SET.password) ? COL_OK : PAL_TEXT_DIM);
    }

    /* Users — list count + which user is default ----------------------- */
    {
        char ubuf[40];
        k_itoa((u32)SET.user_count, ubuf, 10);
        k_strcat(ubuf, T(" users  -  default: ", " kullanıcı  -  varsayılan: "));
        if (SET.default_user >= 0 && SET.default_user < FALCON_MAX_USERS &&
            SET.users[SET.default_user].in_use) {
            k_strcat(ubuf, SET.users[SET.default_user].name);
        } else {
            k_strcat(ubuf, "?");
        }
        s_row(sx, sy + SR_USERS * step, sw,
              T("Users", "Kullanıcılar"), ubuf,
              set_row == SR_USERS, PAL_ACCENT);
    }

    /* Drivers status — one-line health summary built from runtime
     * counters in kbd.c / linux/ata_pio.c.  The label gets a green dot
     * when nothing has gone wrong, an amber/red one when a driver
     * reports retries or hard failures.                              */
    {
        u32 ks, kk, kd; kbd_stats(&ks, &kk, &kd);
        u32 ms, md, mc; mouse_stats(&ms, &md, &mc);
        u32 ar, aw, art, af; ata_stats(&ar, &aw, &art, &af);

        char dbuf[120];
        char tmp[16];
        /* K: keys (drops) | M: clicks (drops) | D: io (retries / FAIL)   */
        k_strcpy(dbuf, "K:");
        k_itoa(kk, tmp, 10); k_strcat(dbuf, tmp);
        if (kd) { k_strcat(dbuf, " drop "); k_itoa(kd, tmp, 10); k_strcat(dbuf, tmp); }
        k_strcat(dbuf, "  M:");
        k_itoa(mc, tmp, 10); k_strcat(dbuf, tmp);
        if (md) { k_strcat(dbuf, " drop "); k_itoa(md, tmp, 10); k_strcat(dbuf, tmp); }
        k_strcat(dbuf, "  D:");
        k_itoa(ar + aw, tmp, 10); k_strcat(dbuf, tmp);
        if (art) { k_strcat(dbuf, " retry "); k_itoa(art, tmp, 10); k_strcat(dbuf, tmp); }
        if (af)  { k_strcat(dbuf, " FAIL ");  k_itoa(af,  tmp, 10); k_strcat(dbuf, tmp); }

        u32 status_color = COL_OK;
        if (kd || md || art) status_color = COL_WARN;
        if (af)              status_color = COL_ERR;

        s_row(sx, sy + SR_DRIVERS * step, sw,
              T("Drivers", "Sürücüler"), dbuf,
              set_row == SR_DRIVERS, status_color);
    }

    /* Network status — virtio-net link state, MAC, IPv4. The driver in
     * linux/virtio_net.c does not currently probe PCI on bare-metal; on
     * QEMU it reports 'down' until DHCP is requested. We reflect the
     * real reported state instead of inventing one.                     */
    {
        char nbuf[120];
        u32  net_color;
        if (!net_present()) {
            k_strcpy(nbuf, T("no adapter found", "kart bulunamadı"));
            net_color = COL_ERR;
        } else if (!net_connected()) {
            k_strcpy(nbuf, T("link down", "bağlı değil"));
            net_color = COL_WARN;
        } else {
            k_strcpy(nbuf, net_ip_addr());
            k_strcat(nbuf, "  ");
            k_strcat(nbuf, net_summary());
            net_color = COL_OK;
        }
        s_row(sx, sy + SR_NETWORK * step, sw,
              T("Network", "Ağ"), nbuf,
              set_row == SR_NETWORK, net_color);
    }

    /* Save to disk ----------------------------------------------------- */
    s_row(sx, sy + SR_SAVE * step, sw,
          T("Save to disk", "Diske kaydet"),
          T("Enter", "Enter"),
          set_row == SR_SAVE, COL_OK);

    /* Lock ------------------------------------------------------------- */
    s_row(sx, sy + SR_LOCK * step, sw,
          T("Lock screen now", "Kilit ekranı"),
          T("Enter", "Enter"),
          set_row == SR_LOCK, COL_WARN);
}

/* --- Notes --------------------------------------------------------------- */
#define NOTES_MAX 256
static char  notes_buf[NOTES_MAX] = "Welcome to Notes.\n\nType freely, this buffer\nlives in BSS until reboot.\n\n- ";
static i32   notes_len = 0;
static void  notes_init_once(void) {
    if (notes_len == 0) notes_len = k_strlen(notes_buf);
}
static void notes_input_key(i32 key)
{
    notes_init_once();
    if (key == KEY_BACKSPACE) {
        sh_buf_pop_utf8(notes_buf, &notes_len);
        return;
    }
    if (key == KEY_ENTER) {
        if (notes_len < NOTES_MAX - 1) {
            notes_buf[notes_len++] = '\n';
            notes_buf[notes_len] = 0;
        }
        return;
    }
    (void)sh_buf_append_key(notes_buf, &notes_len, NOTES_MAX, key);
}
static void render_notes(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame;
    notes_init_once();
    section(wx, wy, T("Notes", "Notlar"),
            T("Type freely · Backspace · Enter newline",
              "Özgür yazın · Geri Sil · Enter satır"));

    i32 px = wx + 24, py = wy + 60, pw = ww - 48, ph = wh - 84;
    gfx_round_rect_a(px, py, pw, ph, 12, PAL_PANEL_DEEP, 255);
    gfx_round_outline(px, py, pw, ph, 12, PAL_HAIRLINE);

    /* render buf with naive line wrapping at \n */
    i32 ty = py + 12;
    i32 line_start = 0;
    for (i32 i = 0; i <= notes_len; i++) {
        if (notes_buf[i] == '\n' || i == notes_len) {
            char tmp[80];
            i32 n = i - line_start;
            if (n > 78) n = 78;
            k_memcpy(tmp, &notes_buf[line_start], n);
            tmp[n] = 0;
            gfx_text(px + 14, ty, tmp, PAL_TEXT);
            ty += 18;
            line_start = i + 1;
            if (ty > py + ph - 24) break;
        }
    }
    /* caret */
    if ((g_ticks / 50) & 1) {
        i32 cols = sh_utf8_chars_between(notes_buf, line_start, notes_len);
        gfx_text(px + 14 + cols * 8, ty, "_", PAL_ACCENT);
    }
}

/* --- Calendar ------------------------------------------------------------ */
static void render_calendar(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame; (void)wh;
    section(wx, wy, T("Calendar", "Takvim"),
            T("Month grid driven by uptime", "Aydınlık ızgarası (uptime ile)"));

    /* fake "today" derived from uptime hours, so it changes if you wait */
    u32 H, M, S; pit_uptime(&H, &M, &S);
    (void)M; (void)S;
    i32 today = (i32)(H % 28) + 1;

    /* month grid: 4 rows x 7 cols = 28 days */
    i32 cw = (ww - 60) / 7;
    i32 ch = (wh - 90) / 4;
    if (ch > 60) ch = 60;
    static const char *DOW[] = { "S","M","T","W","T","F","S" };
    for (i32 c = 0; c < 7; c++) {
        i32 x = wx + 30 + c * cw;
        gfx_text_centered(x + cw / 2, wy + 60, DOW[c], PAL_TEXT_DIM);
    }
    for (i32 d = 0; d < 28; d++) {
        i32 r = d / 7, c = d % 7;
        i32 x = wx + 30 + c * cw;
        i32 y = wy + 84 + r * ch;
        bool isToday = ((d + 1) == today);
        gfx_round_rect_a(x + 2, y + 2, cw - 4, ch - 4, 8,
                         isToday ? PAL_ACCENT : PAL_PANEL_DEEP, 255);
        gfx_round_outline(x + 2, y + 2, cw - 4, ch - 4, 8, PAL_HAIRLINE);
        char buf[4]; k_itoa(d + 1, buf, 10);
        gfx_text_centered(x + cw / 2, y + ch / 2 - 8, buf,
                          isToday ? 0xFFFFFF : PAL_TEXT);
    }
}

/* --- Gallery (Lumen palette) -------------------------------------------- */
static void render_gallery(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame; (void)wh;
    section(wx, wy, T("Gallery", "Galeri"),
            T("Lumen palette swatches", "Lumen palet kartelası"));

    struct { u32 c; const char *n; } SW[] = {
        { PAL_ACCENT,    "Blue 50"   },
        { COL_OK,        "Green 50"  },
        { COL_WARN,      "Amber 50"  },
        { COL_ERR,       "Red 50"    },
        { COL_PURPLE,    "Purple 50" },
        { COL_TEAL,      "Teal 50"   },
        { PAL_BG_TOP,    "BG Top"    },
        { PAL_BG_BOT,    "BG Bottom" },
        { PAL_PANEL,     "Panel"     },
        { PAL_PANEL_HI,  "Hairline"  },
    };
    i32 n = (i32)(sizeof SW / sizeof *SW);
    i32 cw = (ww - 60) / 5;
    i32 ch = 110;
    for (i32 i = 0; i < n; i++) {
        i32 r = i / 5, c = i % 5;
        i32 x = wx + 24 + c * (cw + 4);
        i32 y = wy + 70 + r * (ch + 12);
        gfx_round_rect(x, y, cw, ch - 28, 8, SW[i].c);
        gfx_round_outline(x, y, cw, ch - 28, 8, PAL_HAIRLINE);
        gfx_text(x + 4, y + ch - 24, SW[i].n, PAL_TEXT_DIM);

        char hex[16] = "0x";
        char tmp[8]; k_itoa(SW[i].c, tmp, 16);
        i32 pad = 6 - k_strlen(tmp);
        for (i32 p = 0; p < pad; p++) k_strcat(hex, "0");
        k_strcat(hex, tmp);
        gfx_text(x + 4, y + ch - 8, hex, PAL_TEXT_FAINT);
    }
}

/* --- Falco (native Falcon browser/search surface) ------------------------
 *  Falco is a first-party browser shell designed for FalconOS. With no
 *  network stack yet, it runs a real indexed search over curated web cards
 *  and ships API-ready connectors (DuckDuckGo / Wikipedia / HN) that can be
 *  wired to HTTP once net drivers are live.                                  */
typedef struct {
    const char *title;
    const char *url;
    const char *desc;
} falco_doc_t;

static const falco_doc_t FALCO_INDEX[] = {
    { "FalconOS repository", "https://github.com/hanefimert2016-oss/FalconOS", "Source, issues, roadmap and docs." },
    { "DuckDuckGo Instant API", "https://duckduckgo.com/api", "Free JSON endpoint for search snippets." },
    { "Wikipedia API", "https://www.mediawiki.org/wiki/API:Main_page", "Open encyclopedia API for summaries." },
    { "Stack Exchange API", "https://api.stackexchange.com/docs", "Developer Q&A endpoint with public quotas." },
    { "GitHub REST API", "https://docs.github.com/rest", "Repository and release metadata API." },
    { "Heroic Games Launcher", "https://heroicgameslauncher.com/", "Open launcher for Epic/GOG libraries." },
    { "Google Chrome", "https://www.google.com/chrome/", "Browser compatibility target package." },
    { "QEMU documentation", "https://www.qemu.org/docs/master/", "Virtual machine setup and acceleration." },
    { "Kernel Dev Notes", "falco://docs/kernel", "Local Falcon docs bundle for OS internals." },
    { "Falcon package index", "falco://packages", "Installed + available prg packages." },
};

static char falco_query[80] = "FalconOS";
static i32  falco_query_len = 8;
static i32  falco_sel = 0;

static void falco_set_query(const char *q)
{
    falco_query_len = 0;
    falco_query[0] = 0;
    if (!q) return;
    while (q[falco_query_len] && falco_query_len < 79) {
        falco_query[falco_query_len] = q[falco_query_len];
        falco_query_len++;
    }
    falco_query[falco_query_len] = 0;
    falco_sel = 0;
}

static i32 falco_collect_hits(i32 out_idx[], i32 cap)
{
    i32 n = 0;
    i32 total = (i32)(sizeof(FALCO_INDEX) / sizeof(FALCO_INDEX[0]));
    bool empty = (falco_query[0] == 0);
    for (i32 i = 0; i < total && n < cap; i++) {
        if (empty ||
            sh_contains_ci(FALCO_INDEX[i].title, falco_query) ||
            sh_contains_ci(FALCO_INDEX[i].url,   falco_query) ||
            sh_contains_ci(FALCO_INDEX[i].desc,  falco_query)) {
            out_idx[n++] = i;
        }
    }
    return n;
}

static void falco_input_key(i32 key)
{
    if (key == KEY_BACKSPACE) {
        sh_buf_pop_utf8(falco_query, &falco_query_len);
        return;
    }
    if (key == KEY_UP && falco_sel > 0) { falco_sel--; return; }
    if (key == KEY_DOWN) { falco_sel++; return; }
    if (key == KEY_ENTER) return;
    (void)sh_buf_append_key(falco_query, &falco_query_len, 80, key);
}

static void render_falco(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame;
    section(wx, wy, "Falco",
            T("Local index search — virtio-net ile tam web araması planlanır",
              "Yerel indeks araması — tam internet araması için virtio-net planlanır"));

    /* provider chips */
    {
        const char *chips[3] = { "DuckDuckGo API", "Wikipedia API", "HN API" };
        i32 x = wx + 24, y = wy + 42;
        for (i32 i = 0; i < 3; i++) {
            i32 cw = gfx_text_width(chips[i]) + 18;
            gfx_round_rect_a(x, y, cw, 20, 10, PAL_PANEL_DEEP, 255);
            gfx_round_outline(x, y, cw, 20, 10, PAL_HAIRLINE);
            gfx_text(x + 9, y + 5, chips[i], PAL_TEXT_DIM);
            x += cw + 8;
        }
    }

    /* search box */
    i32 sx = wx + 24, sy = wy + 70, sw = ww - 48;
    gfx_round_rect_a(sx, sy, sw, 34, 17, PAL_PANEL_DEEP, 255);
    gfx_round_outline(sx, sy, sw, 34, 17, PAL_ACCENT);
    gfx_circle(sx + 16, sy + 17, 6, PAL_ACCENT);
    if (falco_query_len == 0) {
        gfx_text(sx + 30, sy + 10,
                 T("Search local index… (no live internet in this build)",
                   "Yerel indekste ara… (bu sürümde canlı internet yok)"),
                 PAL_TEXT_FAINT);
    } else {
        gfx_text(sx + 30, sy + 10, falco_query, PAL_TEXT);
    }
    if ((g_ticks / 50) & 1) {
        i32 cx = sx + 30 + gfx_text_width(falco_query);
        gfx_rect(cx, sy + 8, 2, 18, PAL_ACCENT);
    }

    i32 hits[12];
    i32 hn = falco_collect_hits(hits, 12);
    if (hn <= 0) falco_sel = 0;
    else if (falco_sel >= hn) falco_sel = hn - 1;

    i32 ry = sy + 42;
    if (hn == 0) {
        gfx_round_rect_a(sx, ry, sw, 32, 8, PAL_PANEL_DEEP, 255);
        gfx_round_outline(sx, ry, sw, 32, 8, PAL_HAIRLINE);
        gfx_text(sx + 12, ry + 9,
                 T("No local hit. Real web search needs virtio-net/TCP (not bundled).",
                   "Yerel sonuç yok. Gerçek web araması virtio-net/TCP gerektirir (bu sürümde yok)."),
                 PAL_TEXT_DIM);
    } else {
        i32 rows = (wh - 140) / 44;
        if (rows < 1) rows = 1;
        if (rows > hn) rows = hn;
        for (i32 i = 0; i < rows; i++) {
            i32 idx = hits[i];
            bool sel = (i == falco_sel);
            i32 y = ry + i * 44;
            gfx_round_rect_a(sx, y, sw, 38, 8, sel ? PAL_ACCENT_DIM : PAL_PANEL_DEEP, 255);
            gfx_round_outline(sx, y, sw, 38, 8, sel ? PAL_ACCENT : PAL_HAIRLINE);
            gfx_text(sx + 10, y + 7,  FALCO_INDEX[idx].title, PAL_TEXT);
            gfx_text(sx + 10, y + 22, FALCO_INDEX[idx].url,   PAL_TEXT_DIM);
        }
        if (falco_sel >= 0 && falco_sel < hn) {
            const falco_doc_t *d = &FALCO_INDEX[hits[falco_sel]];
            gfx_text(wx + 24, wy + wh - 24, d->desc, PAL_TEXT_FAINT);
        }
    }
}

/* --- Chrome (visual browser app) ----------------------------------------
 *  Goal: read like a real Chrome window. The window chrome (title bar +
 *  traffic lights) is drawn by the dispatcher; this routine paints
 *      [tab bar]  [address bar + nav buttons]  [bookmark bar]  [page]
 *  in roughly Chrome's layout, with a Google-search-style landing page.
 *  No real network — but the surface area, typography and component
 *  spacing are correct for screenshots and demos.                        */
static const char *CHROME_TABS[3] = {
    "Welcome",
    "Falcon Docs",
    "Source",
};
static i32 chrome_active_tab = 0;

static void chrome_input_key(i32 key)
{
    /* Tab and arrow keys cycle tabs. */
    if (key == KEY_TAB || key == KEY_RIGHT) {
        chrome_active_tab = (chrome_active_tab + 1) % 3;
    } else if (key == KEY_LEFT) {
        chrome_active_tab = (chrome_active_tab + 2) % 3;
    }
}

static void render_browser(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame; (void)wh;

    /* ---- tab strip ----------------------------------------------------- */
    i32 tx = wx + 12, ty = wy + 8;
    i32 tw = (ww - 24) / 4;     /* leave room for a "+" button on the right */
    for (i32 i = 0; i < 3; i++) {
        i32 x = tx + i * (tw + 4);
        u32 fill = (i == chrome_active_tab) ? PAL_PANEL : PAL_PANEL_DEEP;
        gfx_round_rect_a(x, ty, tw, 32, 8, fill, 255);
        gfx_round_outline(x, ty, tw, 32, 8, PAL_HAIRLINE);
        gfx_circle(x + 14, ty + 16, 5,
                   i == 0 ? 0x4285F4 :
                   i == 1 ? 0x34A853 : 0xFBBC04);
        gfx_text(x + 26, ty + 9, CHROME_TABS[i],
                 i == chrome_active_tab ? PAL_TEXT : PAL_TEXT_DIM);
        gfx_text(x + tw - 16, ty + 9, "x", PAL_TEXT_FAINT);
    }
    /* "+" new-tab button                                                  */
    {
        i32 nx = tx + 3 * (tw + 4);
        gfx_round_rect_a(nx, ty + 4, 28, 24, 6, PAL_PANEL_DEEP, 255);
        gfx_text_centered(nx + 14, ty + 9, "+", PAL_TEXT);
    }

    /* ---- toolbar (back / fwd / reload / address / star / menu) -------- */
    i32 by = wy + 50;
    gfx_rect(wx, by, ww, 44, PAL_PANEL_HI);

    i32 bx = wx + 12;
    /* nav buttons */
    const char *NAV[3] = { "<", ">", "C" };
    for (i32 i = 0; i < 3; i++) {
        gfx_circle(bx + 14 + i * 32, by + 22, 12, PAL_PANEL);
        gfx_text_centered(bx + 14 + i * 32, by + 16, NAV[i], PAL_TEXT);
    }

    /* address bar */
    i32 ax = bx + 110, aw = ww - 110 - 80;
    gfx_round_rect_a(ax, by + 6, aw, 32, 16, PAL_PANEL, 255);
    gfx_round_outline(ax, by + 6, aw, 32, 16, PAL_HAIRLINE);
    gfx_circle(ax + 16, by + 22, 6, COL_OK);   /* lock icon            */
    const char *URL[3] = {
        "https://www.google.com/",
        "falcon.os/docs",
        "github.com/hanefimert2016-oss/FalconOS",
    };
    gfx_text(ax + 32, by + 16, URL[chrome_active_tab], PAL_TEXT);
    gfx_text(ax + aw - 18, by + 16, "*", PAL_TEXT_DIM);  /* bookmark star */

    /* menu button */
    gfx_circle(wx + ww - 28, by + 22, 12, PAL_PANEL);
    gfx_text_centered(wx + ww - 28, by + 16, ":", PAL_TEXT);

    /* ---- bookmark bar -------------------------------------------------- */
    i32 mb = wy + 96;
    gfx_rect(wx, mb, ww, 28, PAL_PANEL_DEEP);
    const char *BM[5] = { "Falcon Docs", "Source", "Issues",
                          "PRs", "Lumen Notes" };
    i32 bmx = wx + 16;
    for (i32 i = 0; i < 5; i++) {
        gfx_circle(bmx + 6, mb + 14, 4, PAL_ACCENT);
        gfx_text(bmx + 16, mb + 8, BM[i], PAL_TEXT);
        bmx += k_strlen(BM[i]) * 8 + 36;
    }

    /* ---- page content (per-tab) --------------------------------------- */
    i32 px = wx + 24, py = wy + 132, pw = ww - 48;
    gfx_round_rect_a(px, py, pw, 200, 12, PAL_PANEL, 255);
    gfx_round_outline(px, py, pw, 200, 12, PAL_HAIRLINE);

    if (chrome_active_tab == 0) {
        /* Google-style landing page. */
        i32 cx = px + pw / 2;
        gfx_text_lg_centered(cx - 70, py + 28, "G", 0x4285F4);
        gfx_text_lg_centered(cx - 38, py + 28, "o", 0xEA4335);
        gfx_text_lg_centered(cx -  6, py + 28, "o", 0xFBBC04);
        gfx_text_lg_centered(cx + 26, py + 28, "g", 0x4285F4);
        gfx_text_lg_centered(cx + 58, py + 28, "l", 0x34A853);
        gfx_text_lg_centered(cx + 90, py + 28, "e", 0xEA4335);

        /* search box */
        gfx_round_rect_a(cx - 200, py + 80, 400, 36, 18, PAL_PANEL_DEEP, 255);
        gfx_round_outline(cx - 200, py + 80, 400, 36, 18, PAL_HAIRLINE);
        gfx_circle(cx - 184, py + 98, 6, PAL_TEXT_DIM);
        gfx_text(cx - 168, py + 92, "Search Google or type a URL",
                 PAL_TEXT_DIM);

        /* search / lucky buttons */
        gfx_round_rect_a(cx - 90, py + 134, 80, 28, 6, PAL_PANEL_DEEP, 255);
        gfx_text_centered(cx - 50, py + 138, "Search", PAL_TEXT);
        gfx_round_rect_a(cx + 10, py + 134, 80, 28, 6, PAL_PANEL_DEEP, 255);
        gfx_text_centered(cx + 50, py + 138, "I'm Lucky", PAL_TEXT);
    } else if (chrome_active_tab == 1) {
        gfx_text_lg(px + 16, py + 12, "FalconOS Docs", PAL_TEXT);
        gfx_text(px + 16, py + 56,
                 "Bare-metal x86_64 OS with multi-user, PBKDF2 hashing,",
                 PAL_TEXT);
        gfx_text(px + 16, py + 76,
                 "antialiased UI text and a Linux-derived ATA / HID layer.",
                 PAL_TEXT);
        gfx_text(px + 16, py + 110, "  - make run        run in QEMU",
                 PAL_TEXT_DIM);
        gfx_text(px + 16, py + 130, "  - make run-disk   persistent disk",
                 PAL_TEXT_DIM);
        gfx_text(px + 16, py + 150, "  - F1              toggle dev kernel",
                 PAL_TEXT_DIM);
        gfx_text(px + 16, py + 170, "  - F2              Launchpad",
                 PAL_TEXT_DIM);
    } else {
        gfx_text_lg(px + 16, py + 12, "github.com/hanefimert2016-oss/FalconOS",
                    PAL_TEXT);
        const char *FILES[5] = {
            "kernel/", "linux/", "boot/", "tools/", "README.md"
        };
        for (i32 i = 0; i < 5; i++)
            gfx_text(px + 16, py + 60 + i * 20, FILES[i], PAL_TEXT);
    }

    /* ---- status hint --------------------------------------------------- */
    gfx_text(wx + 24, wy + 350,
             "Tab: switch tabs   Esc: close   (no network stack)",
             PAL_TEXT_FAINT);
}

/* --- Video (software demo player) ---------------------------------------- */
static bool vid_play = true;
static u32  vid_epoch_ms = 0;
static u32  vid_freeze_ms = 0;
static i32  vid_clip = 0;   /* 0..2 */

static u32 vid_now_ms(void)
{
    if (vid_play) return pit_ms() - vid_epoch_ms;
    return vid_freeze_ms;
}

static void video_input_key(i32 key)
{
    if (key == ' ' || key == 'p' || key == 'P') {
        if (vid_play) {
            vid_freeze_ms = vid_now_ms();
            vid_play = false;
        } else {
            vid_epoch_ms = pit_ms() - vid_freeze_ms;
            vid_play = true;
        }
        return;
    }
    if (key == KEY_RIGHT) { vid_freeze_ms = vid_now_ms() + 1500; if (vid_play) vid_epoch_ms = pit_ms() - vid_freeze_ms; return; }
    if (key == KEY_LEFT)  {
        vid_freeze_ms = vid_now_ms();
        if (vid_freeze_ms > 1500) vid_freeze_ms -= 1500;
        else                      vid_freeze_ms = 0;
        if (vid_play) vid_epoch_ms = pit_ms() - vid_freeze_ms;
        return;
    }
    if (key == KEY_TAB || key == 'n' || key == 'N') {
        vid_clip = (vid_clip + 1) % 3;
    }
}

static void render_video(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame;
    section(wx, wy, T("Video", "Video"),
            T("Space play/pause  ← / → seek  Tab clip",
              "Boşluk duraklat  ← / → sar  Sekme klip"));

    i32 vx = wx + 24, vy = wy + 56, vw = ww - 48, vh = wh - 120;
    if (vh < 120) vh = 120;
    gfx_round_rect_a(vx, vy, vw, vh, 12, 0x0D1118, 255);
    gfx_round_outline(vx, vy, vw, vh, 12, PAL_HAIRLINE);

    u32 t = vid_now_ms();
    u32 sec = t / 1000;
    u32 ms = t % 1000;

    /* Procedural "video" clips rendered in real-time on the framebuffer. */
    if (vid_clip == 0) {
        for (i32 y = 8; y < vh - 8; y += 2) {
            u8 a = (u8)(30 + (y * 60) / vh);
            gfx_rect_a(vx + 8, vy + y, vw - 16, 2, PAL_ACCENT, a);
        }
        i32 bx = vx + 24 + (i32)(sec % (u32)(vw > 80 ? (vw - 80) : 1));
        gfx_round_rect_a(bx, vy + vh / 2 - 18, 56, 36, 8, 0xFFFFFF, 190);
    } else if (vid_clip == 1) {
        i32 cx = vx + vw / 2, cy = vy + vh / 2;
        for (i32 r = 18; r < (vh < vw ? vh : vw) / 2 - 8; r += 18) {
            u8 a = (u8)(140 - (r * 100) / (vh > 0 ? vh : 1));
            gfx_circle_a(cx, cy, r + (i32)(sec % 12), PAL_ACCENT, a);
        }
        gfx_circle(cx, cy, 14, 0xFFFFFF);
    } else {
        for (i32 x = vx + 10; x < vx + vw - 10; x += 18) {
            i32 h = 20 + (i32)((((u32)x + sec * 37u) % (u32)(vh - 34)));
            gfx_round_rect_a(x, vy + vh - h - 8, 10, h, 3, 0x34A853, 220);
        }
    }

    /* Transport row */
    i32 tx = vx + 12, ty = vy + vh + 10, tw = vw - 24;
    gfx_round_rect_a(tx, ty, tw, 28, 12, PAL_PANEL_DEEP, 255);
    gfx_round_outline(tx, ty, tw, 28, 12, PAL_HAIRLINE);
    i32 prog = (i32)(ms % (u32)(tw > 16 ? tw - 16 : 1));
    gfx_round_rect_a(tx + 8, ty + 10, prog, 8, 4, PAL_ACCENT, 255);
    gfx_text(tx + tw - 210, ty + 7, vid_play ? "playing" : "paused", vid_play ? COL_OK : COL_WARN);
    char ts[32], n[8];
    k_strcpy(ts, "t=");
    k_itoa(sec, n, 10); k_strcat(ts, n); k_strcat(ts, ".");
    if (ms < 100) k_strcat(ts, "0");
    if (ms < 10)  k_strcat(ts, "0");
    k_itoa(ms, n, 10); k_strcat(ts, n); k_strcat(ts, "s");
    gfx_text(tx + 12, ty + 7, ts, PAL_TEXT);
}

/* --- Heroic Launcher (Linux app bridge mock) ----------------------------- */
static i32 heroic_sel = 0;
static const char *HERO_GAMES[] = {
    "Hades", "Celeste", "Dead Cells", "Vampire Survivors", "Hollow Knight"
};

static void heroic_input_key(i32 key)
{
    i32 n = (i32)(sizeof(HERO_GAMES) / sizeof(HERO_GAMES[0]));
    if (key == KEY_UP && heroic_sel > 0) heroic_sel--;
    if (key == KEY_DOWN && heroic_sel < n - 1) heroic_sel++;
}

static void render_heroic(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame;
    section(wx, wy,
            T("Heroic Launcher", "Heroic Başlatıcı"),
            T("Compatibility layer demo", "Uyumluluk katmanı (demo)"));
    i32 n = (i32)(sizeof(HERO_GAMES) / sizeof(HERO_GAMES[0]));
    i32 lx = wx + 24, ly = wy + 62, lw = ww - 48;
    for (i32 i = 0; i < n; i++) {
        i32 y = ly + i * 36;
        bool sel = (i == heroic_sel);
        gfx_round_rect_a(lx, y, lw, 30, 8, sel ? PAL_ACCENT_DIM : PAL_PANEL_DEEP, 255);
        gfx_round_outline(lx, y, lw, 30, 8, sel ? PAL_ACCENT : PAL_HAIRLINE);
        gfx_text(lx + 12, y + 8, HERO_GAMES[i], PAL_TEXT);
        gfx_text(lx + lw - 140, y + 8, "Epic/GOG bridge", PAL_TEXT_DIM);
    }
    gfx_text(wx + 24, wy + wh - 26,
             "Note: runtime bridge only, no native Linux ELF execution yet.",
             PAL_TEXT_FAINT);
}

/* ===== app table & dispatch ============================================= */
typedef void (*app_render_fn)(i32 x, i32 y, i32 w, i32 h, u32 f);
typedef void (*app_input_fn)(i32 key);

typedef struct {
    const char     *name;
    const char     *subtitle;
    u32             tint;
    app_render_fn   render;
    app_input_fn    input;       /* may be NULL */
    void          (*draw_icon)(i32 cx, i32 cy);
} app_def_t;

static app_def_t APPS[] = {
    { "Home",       "quick links",         0x3070FF, render_home,     NULL,             icon_home     },
    { "Files",      "real shfs browser",   0xF59F1A, render_files,    files_input_key,  icon_files    },
    { "Store",      "prg packages",        0x2BB673, render_store,    store_input_key,  icon_store    },
    { "Settings",   "system + theme",      0x6E7884, render_settings, set_input_key,    icon_settings },
    { "Sistem Güncellemeleri", "prg + FalconFS özeti", 0x05B897, render_updates,
      updates_input_key, icon_updates },
    { "Terminal",   "POSIX shell + prg",   0x14181F, render_term,     term_input_key,   icon_term     },
    { "Calculator", "+ - * /",             0xA45EE5, render_calc,     calc_input_key,   icon_calc     },
    { "Notes",      "free-form pad",       0xFFB547, render_notes,    notes_input_key,  icon_notes    },
    { "Clock",      "PIT analog dial",     0x16B5A8, render_clock,    NULL,             icon_clock    },
    { "Stats",      "system telemetry",    0xE53935, render_stats,    NULL,             icon_stats    },
    { "Calendar",   "month view",          0x3070FF, render_calendar, NULL,             icon_calendar },
    { "Gallery",    "palette swatches",    0xC084FC, render_gallery,  NULL,             icon_gallery  },
    { "Video",      "software player",     0x16B5A8, render_video,    video_input_key,  icon_video    },
    { "Falco",      "native web search",   0x2A66F5, render_falco,    falco_input_key,  icon_falco    },
    { "Chrome",     "Tab to switch tabs",  0x4285F4, render_browser, chrome_input_key,  icon_browser  },
    { "Heroic",     "linux game launcher", 0x6D5BFF, render_heroic,  heroic_input_key, icon_heroic   },
    { "Jarvis",     "AI assistant",        0x6D5BFF, jarvis_render,  jarvis_input,     jarvis_icon   },
    { "About",      "FalconOS 1",      0xA45EE5, render_about,    NULL,             icon_about    },
};

i32 apps_count(void) { return (i32)(sizeof APPS / sizeof *APPS); }
const char *apps_name(i32 i)     { return APPS[i].name; }
const char *apps_subtitle(i32 i) { return APPS[i].subtitle; }

const char *apps_display_name(i32 i)
{
    if (i < 0 || i >= apps_count()) return "?";
    if (SET.lang != LANG_TR)
        return APPS[i].name;
    switch (i) {
        case 0:  return "Ana Sayfa";
        case 1:  return "Dosyalar";
        case 2:  return "Mağaza";
        case 3:  return "Ayarlar";
        case 4:  return "Sistem Güncellemeleri";
        case 5:  return "Terminal";
        case 6:  return "Hesap Makinesi";
        case 7:  return "Notlar";
        case 8:  return "Saat";
        case 9:  return "İstatistik";
        case 10: return "Takvim";
        case 11: return "Galeri";
        case 12: return "Video";
        case 13: return "Falco";
        case 14: return "Chrome";
        case 15: return "Heroic";
        case 16: return "Jarvis";
        case 17: return "Hakkında";
        default: return APPS[i].name;
    }
}

const char *apps_display_subtitle(i32 i)
{
    if (i < 0 || i >= apps_count()) return "";
    if (SET.lang != LANG_TR)
        return APPS[i].subtitle;
    switch (i) {
        case 0:  return "Hızlı bağlantılar";
        case 1:  return "Örnek dosya listesi";
        case 2:  return "prg paket merkezi";
        case 3:  return "sistem + tema";
        case 4:  return "prg + FalconFS özeti";
        case 5:  return "POSIX kabuğu + prg";
        case 6:  return "+ − × ÷";
        case 7:  return "Serbest not";
        case 8:  return "Analog saat";
        case 9:  return "RAM / CPU / ekran";
        case 10: return "Ay görünümü";
        case 11: return "Renk paleti";
        case 12: return "Yazılım oynatıcı";
        case 13: return "Yerel indeks arama";
        case 14: return "Sekme görünümü (demo)";
        case 15: return "Oyun başlatıcı (uyum)";
        case 16: return "Yapay asistan";
        case 17: return "FalconOS bilgisi";
        default: return APPS[i].subtitle;
    }
}

u32         apps_tint(i32 i)     { return APPS[i].tint; }

void apps_draw_icon(i32 i, i32 cx, i32 cy)
{
    if (APPS[i].draw_icon) APPS[i].draw_icon(cx, cy);
}

void apps_input_active(i32 key)
{
    if (active_app < 0) return;
    if (key == KEY_ESC) { apps_close(); return; }
    if (APPS[active_app].input) APPS[active_app].input(key);
}

/* Compute the active window's rect, taking the WM state into account.
 * Returns false when the rect is invalid (no active app).             */
static bool wm_window_rect(i32 *out_x, i32 *out_y, i32 *out_w, i32 *out_h)
{
    if (active_app < 0) return false;
    i32 W = (i32)FB.width, H = (i32)FB.height;
    if (wm_max) {
        *out_x = 8; *out_y = 36;                   /* below the menu bar */
        *out_w = W - 16; *out_h = H - 76;          /* leave dock visible */
        return true;
    }
    i32 ww = W - 280; if (ww > 920) ww = 920; if (ww < 600) ww = 600;
    i32 wh = H - 220; if (wh > 580) wh = 580; if (wh < 380) wh = 380;
    ww += wm_dw; wh += wm_dh;
    if (ww < 480) ww = 480;
    if (wh < 300) wh = 300;
    if (ww > W - 32) ww = W - 32;
    if (wh > H - 80) wh = H - 80;
    i32 wx = (W - ww) / 2 + wm_dx;
    i32 wy = (H - wh) / 2 - 10 + wm_dy;
    if (wx < 4)  wx = 4;
    if (wy < 32) wy = 32;
    if (wx + ww > W - 4) wx = W - 4 - ww;
    if (wy + wh > H - 4) wy = H - 4 - wh;
    *out_x = wx; *out_y = wy; *out_w = ww; *out_h = wh;
    return true;
}

/* Mouse-driven window manager. Called once per frame from main.c right
 * after mouse_get(). Handles title-bar drag, corner resize and traffic
 * lights. Returns true when it consumed the click (so the underlying
 * app shouldn't see it).                                              */
bool apps_wm_handle_mouse(i32 mx, i32 my, bool left_held, bool click_edge)
{
    if (active_app < 0) return false;

    i32 wx, wy, ww, wh;
    if (!wm_window_rect(&wx, &wy, &ww, &wh)) return false;

    /* Continue an in-flight gesture first.                              */
    if (wm_dragging) {
        if (!left_held) { wm_dragging = false; return true; }
        wm_dx += mx - wm_drag_grab_x;
        wm_dy += my - wm_drag_grab_y;
        wm_drag_grab_x = mx; wm_drag_grab_y = my;
        return true;
    }
    if (wm_resizing) {
        if (!left_held) { wm_resizing = false; return true; }
        wm_dw = (mx - wm_resize_grab_x) + (wm_resize_start_w - ((i32)FB.width  - 280));
        wm_dh = (my - wm_resize_grab_y) + (wm_resize_start_h - ((i32)FB.height - 220));
        return true;
    }

    if (!click_edge) return false;

    /* traffic lights live at title-bar y ± 10px, x within radius 9.
     *   red    → close (×)
     *   yellow → minimise to dock
     *   green  → toggle maximised (+)                                  */
    i32 ty = wy + 18;
    if (my >= ty - 10 && my <= ty + 10) {
        if (mx >= wx + 9  && mx <= wx + 27) { apps_close(); return true; }
        if (mx >= wx + 29 && mx <= wx + 47) {
            minimized_app = active_app;
            active_app = -1;
            wm_dragging = false;
            wm_resizing = false;
            return true;
        }
        if (mx >= wx + 49 && mx <= wx + 67) { wm_max = !wm_max; return true; }
    }

    /* resize handle: 18×18 square at the bottom-right, only visible
     * when not maximised. Maximised windows are not resizable. */
    if (!wm_max &&
        mx >= wx + ww - 22 && mx <= wx + ww - 2 &&
        my >= wy + wh - 22 && my <= wy + wh - 2) {
        wm_resizing = true;
        wm_resize_grab_x = mx; wm_resize_grab_y = my;
        wm_resize_start_w = ww; wm_resize_start_h = wh;
        return true;
    }

    /* title bar: anywhere in the top 36 px not covered by the buttons */
    if (my >= wy && my <= wy + 36 &&
        mx >= wx + 80 && mx <= wx + ww - 40) {
        wm_dragging = true;
        wm_drag_grab_x = mx; wm_drag_grab_y = my;
        return true;
    }

    return false;
}

/* renders the active app's window with a slide-in animation */
void apps_render_active(u32 frame)
{
    if (active_app < 0) return;
    const app_def_t *a = &APPS[active_app];

    i32 wx, wy, ww, wh;
    wm_window_rect(&wx, &wy, &ww, &wh);
    /* Full-screen blur was expensive on QEMU/TCG; keep depth by dimming the
     * whole desktop and blurring only a small halo around the active window. */
    if (SET.aero_enabled) {
        i32 hx = wx - 24, hy = wy - 24, hw = ww + 48, hh = wh + 48;
        if (SET.theme == THEME_LIQUID) gfx_blur_rect(hx, hy, hw, hh, 4);
        gfx_rect_a(0, 0, FB.width, FB.height, COL_SHADOW, 68);
    } else {
        gfx_rect_a(0, 0, FB.width, FB.height, COL_SHADOW, 60);
    }

    /* slide-in: 200 ms — only on first open, not while dragging */
    if (!wm_dragging && !wm_resizing) {
        u32 dt = pit_ms() - open_at_ms;
        if (dt > 200) dt = 200;
        i32 off = (i32)((200 - dt) * 60 / 200);
        wy += off;
    }

    /* card — Aero dims the desktop / dock / widgets behind the window  
     * so the chrome feels lifted. Window body remains solid because most
     * apps render their own opaque content into it.                      */
    gfx_round_rect_a(wx + 4, wy + 12, ww, wh, 18, COL_SHADOW, 70);   /* shadow */
    gfx_round_rect_a(wx, wy, ww, wh, 18, PAL_PANEL, SET.aero_enabled ? 230 : 245);
    gfx_round_outline(wx, wy, ww, wh, 18, PAL_HAIRLINE);

    /* title bar — macOS-spec traffic lights on the left.
     *   x+18  red    close       (#FF5F57)
     *   x+38  yellow minimise    (#FEBC2E)
     *   x+58  green  maximise    (#28C840)
     *
     * Each light is rendered as a sphere: inner disc + soft top
     * highlight + thin dark outline.  When the cursor is over the
     * traffic-light cluster, the hover glyph (× / − / +) is drawn
     * inside its circle — same as Big Sur.                          */
    {
        i32 mx, my; bool ml;
        mouse_get(&mx, &my, &ml);
        bool hover_cluster =
            (my >= wy +  8 && my <= wy + 28 &&
             mx >= wx +  8 && mx <= wx + 68);

        const i32   LX[3]   = { wx + 18, wx + 38, wx + 58 };
        const u32   FILL[3] = { 0xFF5F57u, 0xFEBC2Eu, 0x28C840u };
        const u32   RIM[3]  = { 0xCB4B43u, 0xC79624u, 0x21A434u };
        const char *GLYPH[3]= { "x", "-", "+" };

        for (i32 b = 0; b < 3; b++) {
            gfx_circle(LX[b], wy + 18, 7, FILL[b]);
            /* faint inner highlight on top half so the disc reads as
             * a sphere lit from above (the macOS look).              */
            gfx_circle_a(LX[b], wy + 16, 4, 0xFFFFFFu, 90);
            /* 1-px outer rim — slightly darker than the fill         */
            gfx_circle_outline(LX[b], wy + 18, 7, RIM[b]);

            if (hover_cluster) {
                gfx_text_centered(LX[b], wy + 12, GLYPH[b], 0x202020u);
            }
        }
    }
    /* app-tint pip on the right keeps the chrome symmetric */
    gfx_circle(wx + ww - 26, wy + 18, 8, a->tint);
    gfx_text_centered(wx + ww / 2, wy + 12, apps_display_name(active_app), PAL_TEXT_DIM);

    /* body offset by 44 px for title strip */
    a->render(wx, wy + 44, ww, wh - 44, frame);

    /* resize handle (bottom-right) — three little diagonal pips */
    if (!wm_max) {
        i32 hx = wx + ww - 14, hy = wy + wh - 14;
        for (i32 i = 0; i < 3; i++) {
            gfx_rect(hx - i*4, hy + i*4, 3, 3, PAL_TEXT_FAINT);
        }
    }

    /* hint */
    gfx_text_centered(wx + ww / 2, wy + wh - 24,
        T("drag title-bar  ·  resize corner  ·  yellow=minimize  ·  green=max",
          "başlık çubuğunu sürükleyin · sağ alttan yeniden boyutlandırın"),
        PAL_TEXT_FAINT);
}
