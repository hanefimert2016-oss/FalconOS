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

void apps_open(i32 i)  { active_app = i; open_at_ms = pit_ms(); }
void apps_close(void)  { active_app = -1; wm_max = false;
                          wm_dragging = false; wm_resizing = false; }
i32  apps_active(void) { return active_app; }

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
static void icon_store(i32 cx, i32 cy)
{
    /* shopping bag with "P" */
    gfx_round_rect(cx - 12, cy - 8, 24, 18, 3, COL_PANEL);
    gfx_line(cx - 8, cy - 8, cx - 8, cy - 14, COL_PANEL);
    gfx_line(cx + 8, cy - 8, cx + 8, cy - 14, COL_PANEL);
    gfx_line(cx - 8, cy - 14, cx + 8, cy - 14, COL_PANEL);
    gfx_text_centered(cx, cy - 6, "P", COL_ACCENT);
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
    section(wx, wy, "Welcome to FalconOS", "Quick links");

    const char *cards[]    = { "Recent docs", "System info", "Network", "Theme" };
    const char *subs[]     = { "open last 5", "uptime/ram",  "no network",  "Lumen blue" };
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

/* --- Files --------------------------------------------------------------- */
static const char *FAKE_FILES[] = {
    "README.md",       "boot/multiboot2.asm",  "kernel/main.c",
    "kernel/gfx.c",    "kernel/personal.c",    "kernel/dev.c",
    "kernel/idt.c",    "kernel/apps.c",        "kernel/launchpad.c",
    "kernel/repl.c",   "Makefile",             "linker.ld",
};
static void render_files(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame; (void)ww; (void)wh;
    section(wx, wy, "Files", "/ (root)");
    i32 n = (i32)(sizeof FAKE_FILES / sizeof *FAKE_FILES);
    for (i32 i = 0; i < n; i++) {
        i32 y = wy + 60 + i * 22;
        if (i % 2 == 0)
            gfx_round_rect_a(wx + 22, y - 2, ww - 44, 20, 6, PAL_PANEL_DEEP, 255);
        gfx_circle(wx + 36, y + 8, 5, PAL_ACCENT);
        gfx_text(wx + 50, y + 2, FAKE_FILES[i], PAL_TEXT);
    }
}

/* --- Clock: analog dial -------------------------------------------------- */
static void render_clock(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame;
    section(wx, wy, "Clock", "synced to PIT IRQ0");
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
    section(wx, wy, "Stats", "live system telemetry");

    u64 t = rdtsc();
    u32 hi = (u32)(t >> 32), lo = (u32)t;
    u32 h, m, s; pit_uptime(&h, &m, &s);

    char hex[20], line[80];

    k_strcpy(line, "tsc       0x");
    k_itoa(hi, hex, 16); k_strcat(line, hex);
    k_itoa(lo, hex, 16); k_strcat(line, hex);
    gfx_text(wx + 24, wy + 60, line, PAL_TEXT);

    char th[8], tm[8], ts[8];
    k_itoa(h, th, 10); k_itoa(m, tm, 10); k_itoa(s, ts, 10);
    k_strcpy(line, "uptime    ");
    k_strcat(line, th); k_strcat(line, "h ");
    k_strcat(line, tm); k_strcat(line, "m ");
    k_strcat(line, ts); k_strcat(line, "s");
    gfx_text(wx + 24, wy + 86, line, PAL_TEXT);

    k_strcpy(line, "ticks     ");
    k_itoa(g_ticks, hex, 10); k_strcat(line, hex);
    gfx_text(wx + 24, wy + 110, line, PAL_TEXT);

    k_strcpy(line, "ram (KiB) ");
    k_itoa(RAM_TOTAL_KB, hex, 10); k_strcat(line, hex);
    gfx_text(wx + 24, wy + 134, line, PAL_TEXT);

    k_strcpy(line, "fb        ");
    k_itoa(FB.width, hex, 10); k_strcat(line, hex); k_strcat(line, "x");
    k_itoa(FB.height, hex, 10); k_strcat(line, hex);
    gfx_text(wx + 24, wy + 158, line, PAL_TEXT);

    /* pulse bar */
    i32 pw = (i32)((g_ticks % 100) * (ww - 48) / 100);
    gfx_round_rect(wx + 24, wy + 188, ww - 48, 6, 3, PAL_HAIRLINE);
    gfx_round_rect(wx + 24, wy + 188, pw,      6, 3, PAL_ACCENT);
}

/* --- About --------------------------------------------------------------- */
static void render_about(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame; (void)wh;
    section(wx, wy, T("About FalconOS", "FalconOS Hakkinda"), "FalconOS 1");

    i32 cx = wx + ww / 2;
    gfx_circle(cx, wy + 110, 56, PAL_ACCENT);
    gfx_circle(cx, wy + 110, 36, PAL_PANEL);
    gfx_circle(cx, wy + 110, 18, PAL_ACCENT);

    gfx_text_centered(cx, wy + 190, "FalconOS 1",                            PAL_TEXT);
    gfx_text_centered(cx, wy + 210, "bare-metal x86_64 - personal & developer", PAL_ACCENT);
    gfx_text_centered(cx, wy + 232,
        T("two kernels, one binary - F1 to flip - F2 Launchpad",
          "iki cekirdek, tek ikili - F1 ile gec - F2 Launchpad"),
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
          "bare-metal mikrocekirdek  -  prg paketleri  -  Magaza  -  POSIX kabuk"),
        PAL_TEXT_FAINT);
}

/* --- Store (prg package browser, GUI) ----------------------------------- */
static i32 store_cursor = 0;
static i32 store_filter = 0;     /* 0 all, 1 installed */

static void store_input_key(i32 key)
{
    i32 n = prg_count();
    if (key == KEY_UP   && store_cursor > 0)     store_cursor--;
    if (key == KEY_DOWN && store_cursor < n - 1) store_cursor++;
    if (key == KEY_LEFT  || key == KEY_RIGHT)    store_filter ^= 1;
    if (key == KEY_ENTER || key == ' ' || key == 'i' || key == 'I') {
        prg_install(store_cursor);
    }
    if (key == 'r' || key == 'R' || key == KEY_BACKSPACE) {
        prg_remove(store_cursor);
    }
}

static void render_store(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame;
    section(wx, wy,
            T("Store",  "Magaza"),
            T("up/down  pick    Enter/I install    R/Backspace remove",
              "yukari/asagi  sec    Enter/I yukle    R/Backspace kaldir"));

    char hdr[80], num[12];
    k_strcpy(hdr, T("packages: ", "paket: "));
    k_itoa((u32)prg_installed_count(), num, 10);
    k_strcat(hdr, num);
    k_strcat(hdr, "/");
    k_itoa((u32)prg_count(), num, 10);
    k_strcat(hdr, num);
    k_strcat(hdr, T("  installed", "  kurulu"));
    gfx_text(wx + 24, wy + 38, hdr, PAL_TEXT_DIM);

    i32 lx = wx + 24, ly = wy + 60, lw = ww - 48;
    i32 row_h = 32;
    i32 visible = (wh - 80) / row_h;
    i32 first = store_cursor - visible / 2;
    if (first < 0) first = 0;
    if (first > prg_count() - visible) first = prg_count() - visible;
    if (first < 0) first = 0;

    for (i32 i = first; i < first + visible && i < prg_count(); i++) {
        const prg_pkg_t *p = prg_at(i);
        i32 y = ly + (i - first) * row_h;
        bool active = (i == store_cursor);
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
        /* name + version */
        gfx_text(lx + 30, y + 8, p->name, PAL_TEXT);
        gfx_text(lx + 30 + gfx_text_width(p->name) + 8,
                 y + 8, p->version, PAL_TEXT_FAINT);
        /* summary */
        gfx_text(lx + 30 + gfx_text_width(p->name) + 8 + gfx_text_width(p->version) + 14,
                 y + 8, p->summary, PAL_TEXT_DIM);
        /* installed badge */
        const char *badge =
            p->builtin             ? T("built-in",  "yerlesik") :
            prg_is_installed(i)    ? T("installed", "kurulu")   :
                                     T("get",       "yukle");
        u32 badge_c =
            p->builtin             ? COL_OK :
            prg_is_installed(i)    ? COL_OK :
                                     PAL_ACCENT;
        gfx_text(lx + lw - gfx_text_width(badge) - 14, y + 8, badge, badge_c);
    }

    /* footer status */
    const prg_pkg_t *cur = prg_at(store_cursor);
    char foot[80];
    k_strcpy(foot, T("category: ", "kategori: "));
    k_strcat(foot, cur->category);
    gfx_text(wx + 24, wy + wh - 22, foot, PAL_TEXT_FAINT);
}

/* --- Terminal (POSIX shell subset, FalconOS 1) ---------------------------
 * A compact, real shell. Supports:
 *   - built-ins: pwd cd ls cat echo env set unset clear help true false
 *                exit uname whoami date rm touch cp mv
 *   - variable assignment   X=value         (no spaces around =)
 *   - variable expansion    $X              (single-token form)
 *   - simple pipes          a | b
 *   - output redirect       > file   >> file
 *   - inline conditionals   if cmd; then cmd; fi
 *   - inline loops          for x in a b c; do echo $x; done
 *
 * I/O backed by a tiny RAM filesystem (`shfs`) - 16 files * 512 bytes,
 * single flat directory, fits trivially in BSS. */
#define TERM_LINES 12
#define TERM_COLS  80
#define SH_VARS    16
#define SH_FILES   16
#define SH_FBYTES  512

typedef struct { char name[16]; u32 len; char data[SH_FBYTES]; bool used; } shfile_t;
typedef struct { char name[16]; char value[64]; bool used; } shvar_t;

static char term_buf[TERM_LINES][TERM_COLS];
static i32  term_init_done = 0;
static i32  term_input_len = 0;
static char term_input[TERM_COLS];
static char sh_cwd[32]   = "/home/falcon";
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

static void term_init(void)
{
    if (term_init_done) return;
    term_init_done = 1;
    for (i32 i = 0; i < TERM_LINES; i++) term_buf[i][0] = 0;
    term_push("FalconOS shell  -  POSIX subset (cd/ls/cat/echo/if/for/| / > )");
    term_push("type 'help' for the command list");
    term_push("");
    /* seed a couple of files so 'ls' / 'cat' have content out of the box */
    k_strcpy(sh_files[0].name, "readme.txt");
    k_strcpy(sh_files[0].data,
        "Welcome to FalconOS 1.\nThis shell understands the basics:\n"
        "  pwd, cd, ls, cat, echo, env, set X=val, $X expansion,\n"
        "  | pipe, > redirect, if/then/fi, for ... in ... do ... done\n");
    sh_files[0].len = k_strlen(sh_files[0].data);
    sh_files[0].used = true;
    k_strcpy(sh_files[1].name, "hello.sh");
    k_strcpy(sh_files[1].data, "echo hello world\n");
    sh_files[1].len  = k_strlen(sh_files[1].data);
    sh_files[1].used = true;
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
    } else if (!append) {
        f->len = 0;
        f->data[0] = 0;
    }
    return f;
}

/* small helpers --------------------------------------------------------- */
static bool sh_isspace(char c) { return c == ' ' || c == '\t'; }

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
        k_strcpy(out, "pwd cd ls cat echo env set X=v rm touch cp mv  | > >>  if/then/fi  for in do done");
        return 0;
    }
    if (k_strcmp(cmd, "pwd") == 0)    { k_strcpy(out, sh_cwd); return 0; }
    if (k_strcmp(cmd, "uname") == 0)  { k_strcpy(out, "FalconOS 1 x86_64 bare-metal"); return 0; }
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
        if (argc < 2) { k_strcpy(sh_cwd, "/home/falcon"); return 0; }
        /* very simple: absolute path overrides, else append */
        if (argv[1][0] == '/') k_strcpy(sh_cwd, argv[1]);
        else if (k_strcmp(argv[1], "..") == 0) {
            i32 n = k_strlen(sh_cwd);
            while (n > 1 && sh_cwd[n - 1] != '/') n--;
            if (n > 1) n--;
            sh_cwd[n] = 0;
        } else {
            i32 n = k_strlen(sh_cwd);
            if (n + 1 + (i32)k_strlen(argv[1]) < (i32)sizeof sh_cwd) {
                if (sh_cwd[n - 1] != '/') sh_cwd[n++] = '/';
                k_strcpy(sh_cwd + n, argv[1]);
            }
        }
        return 0;
    }
    if (k_strcmp(cmd, "ls") == 0) {
        out[0] = 0; i32 first = 1;
        for (i32 i = 0; i < SH_FILES; i++) if (sh_files[i].used) {
            if (!first) k_strcat(out, "  ");
            k_strcat(out, sh_files[i].name);
            first = 0;
        }
        return 0;
    }
    if (k_strcmp(cmd, "cat") == 0) {
        if (argc < 2) return 1;
        shfile_t *f = sh_file_find(argv[1]);
        if (!f) { k_strcpy(out, "cat: not found: "); k_strcat(out, argv[1]); return 1; }
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
        if (argc < 2) return 1;
        shfile_t *f = sh_file_find(argv[1]);
        if (f) f->used = false;
        return 0;
    }
    if (k_strcmp(cmd, "touch") == 0) {
        if (argc < 2) return 1;
        sh_file_open_w(argv[1], true);
        return 0;
    }
    if (k_strcmp(cmd, "cp") == 0 || k_strcmp(cmd, "mv") == 0) {
        if (argc < 3) return 1;
        shfile_t *src = sh_file_find(argv[1]);
        if (!src) { k_strcpy(out, "no such file"); return 1; }
        shfile_t *dst = sh_file_open_w(argv[2], false);
        if (!dst) return 1;
        for (u32 i = 0; i < src->len && i < SH_FBYTES - 1; i++) dst->data[i] = src->data[i];
        dst->len = src->len;
        dst->data[dst->len] = 0;
        if (k_strcmp(cmd, "mv") == 0) src->used = false;
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
    char redir_name[16] = {0};
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
            k_strcpy(redir_name, fn);
            left[gg] = 0;
        } else {
            i32 g = sh_find(left, ">");
            if (g >= 0) {
                char fn[64]; i32 k = 0;
                for (i32 i = g + 1; left[i] && k < 63; i++) if (!sh_isspace(left[i]) || k) fn[k++] = left[i];
                fn[k] = 0;
                while (k > 0 && sh_isspace(fn[k - 1])) fn[--k] = 0;
                k_strcpy(redir_name, fn);
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

    gfx_text(wx + 30, wy + 18, "FalconOS shell  -  POSIX subset", 0xCFE6FF);
    gfx_text(wx + 30, wy + 38,
             "type 'help' or 'cat readme.txt' to start",
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
        if (term_input_len > 0) {
            term_input_len--;
            term_input[term_input_len] = 0;
        }
        return;
    }
    if (key >= 0x20 && key <= 0x7E && term_input_len < TERM_COLS - 16) {
        term_input[term_input_len++] = (char)key;
        term_input[term_input_len] = 0;
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
    section(wx, wy, "Calculator", "+ - * /  digits  c=clear  Enter==");

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
    SR_SAVE, SR_LOCK, SR_COUNT
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
        if (key == KEY_BACKSPACE) { if (set_pwd_len) set_pwd[--set_pwd_len] = 0; return; }
        if (key == KEY_ENTER)     {
            set_pwd[set_pwd_len] = 0;
            k_strcpy(SET.password, set_pwd);
            set_pwd_editing = false;
            return;
        }
        if (key == KEY_ESC)       { set_pwd_editing = false; return; }
        if (key >= 0x20 && key < 0x7F && set_pwd_len < 23) {
            set_pwd[set_pwd_len++] = (char)key;
            set_pwd[set_pwd_len] = 0;
        }
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
                      "yukari/asagi  satir   sol/sag  degistir"));

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
          T("Aero (transparency)", "Aero (seffaflik)"),
          SET.aero_enabled ? T("on", "acik") : T("off", "kapali"),
          set_row == SR_AERO,
          SET.aero_enabled ? COL_OK : PAL_TEXT_DIM);

    /* Language --------------------------------------------------------- */
    s_row(sx, sy + SR_LANG * step, sw,
          TX("Language", "Dil", "Sprache", "Langue", "Idioma"),
          lang_name(SET.lang),
          set_row == SR_LANG, PAL_TEXT);

    /* Keyboard layout -------------------------------------------------- */
    s_row(sx, sy + SR_KBD * step, sw,
          T("Keyboard layout", "Klavye duzeni"),
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
          SET.animations ? T("on", "acik") : T("off", "kapali"),
          set_row == SR_ANIM, SET.animations ? COL_OK : PAL_TEXT_DIM);

    /* Widgets ---------------------------------------------------------- */
    s_row(sx, sy + SR_WIDGETS * step, sw,
          T("Desktop widgets", "Masaustu widgetlar"),
          SET.widgets_shown ? T("shown", "acik") : T("hidden", "gizli"),
          set_row == SR_WIDGETS,
          SET.widgets_shown ? COL_OK : PAL_TEXT_DIM);

    /* Viewport / resolution ------------------------------------------- */
    s_row(sx, sy + SR_VIEWPORT * step, sw,
          T("Resolution", "Cozunurluk"),
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
        k_strcat(ubuf, T(" users  -  default: ", " kullanici  -  varsayilan: "));
        if (SET.default_user >= 0 && SET.default_user < FALCON_MAX_USERS &&
            SET.users[SET.default_user].in_use) {
            k_strcat(ubuf, SET.users[SET.default_user].name);
        } else {
            k_strcat(ubuf, "?");
        }
        s_row(sx, sy + SR_USERS * step, sw,
              T("Users", "Kullanicilar"), ubuf,
              set_row == SR_USERS, PAL_ACCENT);
    }

    /* Drivers status — one-line health summary built from runtime
     * counters in kbd.c / linux/ata_pio.c.  The label gets a green dot
     * when nothing has gone wrong, an amber/red one when a driver
     * reports retries or hard failures.                              */
    {
        u32 ks, kk, kd; kbd_stats(&ks, &kk, &kd);
        u32 ar, aw, art, af; ata_stats(&ar, &aw, &art, &af);

        char dbuf[80];
        char tmp[16];
        k_strcpy(dbuf, "K:");
        k_itoa(kk, tmp, 10); k_strcat(dbuf, tmp);
        if (kd) { k_strcat(dbuf, " drop "); k_itoa(kd, tmp, 10); k_strcat(dbuf, tmp); }
        k_strcat(dbuf, "  D:");
        k_itoa(ar + aw, tmp, 10); k_strcat(dbuf, tmp);
        if (art) { k_strcat(dbuf, " retry "); k_itoa(art, tmp, 10); k_strcat(dbuf, tmp); }
        if (af)  { k_strcat(dbuf, " FAIL ");  k_itoa(af,  tmp, 10); k_strcat(dbuf, tmp); }

        u32 status_color = COL_OK;
        if (kd || art) status_color = COL_WARN;
        if (af)        status_color = COL_ERR;

        s_row(sx, sy + SR_DRIVERS * step, sw,
              T("Drivers", "Suruculer"), dbuf,
              set_row == SR_DRIVERS, status_color);
    }

    /* Save to disk ----------------------------------------------------- */
    s_row(sx, sy + SR_SAVE * step, sw,
          T("Save to disk", "Diske kaydet"),
          T("Enter", "Enter"),
          set_row == SR_SAVE, COL_OK);

    /* Lock ------------------------------------------------------------- */
    s_row(sx, sy + SR_LOCK * step, sw,
          T("Lock screen now", "Kilit ekrani"),
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
        if (notes_len > 0) {
            notes_len--;
            notes_buf[notes_len] = 0;
        }
        return;
    }
    if (key == KEY_ENTER) {
        if (notes_len < NOTES_MAX - 1) {
            notes_buf[notes_len++] = '\n';
            notes_buf[notes_len] = 0;
        }
        return;
    }
    if (key >= 0x20 && key <= 0x7E && notes_len < NOTES_MAX - 1) {
        notes_buf[notes_len++] = (char)key;
        notes_buf[notes_len] = 0;
    }
}
static void render_notes(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame;
    notes_init_once();
    section(wx, wy, "Notes", "type freely - Backspace to delete - Enter for newline");

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
    if ((g_ticks / 50) & 1) gfx_text(px + 14 + (notes_len - line_start) * 8, ty, "_", PAL_ACCENT);
}

/* --- Calendar ------------------------------------------------------------ */
static void render_calendar(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame; (void)wh;
    section(wx, wy, "Calendar", "kernel month - day = uptime hours mod 28");

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
    section(wx, wy, "Gallery", "Lumen palette swatches");

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
    /* Tab / Shift-Tab cycle the active tab; left/right also move it.   */
    if (key == 0x09 /* TAB */ || key == 0x0E /* RIGHT */) {
        chrome_active_tab = (chrome_active_tab + 1) % 3;
    } else if (key == 0x0D /* LEFT */ || key == 0x0B /* SHIFT-TAB */) {
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
    { "Files",      "in-memory tree",      0xF59F1A, render_files,    NULL,             icon_files    },
    { "Store",      "prg packages",        0x2BB673, render_store,    store_input_key,  icon_store    },
    { "Settings",   "system + theme",      0x6E7884, render_settings, set_input_key,    icon_settings },
    { "Terminal",   "fake bash prompt",    0x14181F, render_term,     term_input_key,   icon_term     },
    { "Calculator", "+ - * /",             0xA45EE5, render_calc,     calc_input_key,   icon_calc     },
    { "Notes",      "free-form pad",       0xFFB547, render_notes,    notes_input_key,  icon_notes    },
    { "Clock",      "PIT analog dial",     0x16B5A8, render_clock,    NULL,             icon_clock    },
    { "Stats",      "system telemetry",    0xE53935, render_stats,    NULL,             icon_stats    },
    { "Calendar",   "month view",          0x3070FF, render_calendar, NULL,             icon_calendar },
    { "Gallery",    "palette swatches",    0xC084FC, render_gallery,  NULL,             icon_gallery  },
    { "Chrome",     "Tab to switch tabs",  0x4285F4, render_browser, chrome_input_key,  icon_browser  },
    { "About",      "FalconOS 1",      0xA45EE5, render_about,    NULL,             icon_about    },
};

i32 apps_count(void) { return (i32)(sizeof APPS / sizeof *APPS); }
const char *apps_name(i32 i)     { return APPS[i].name; }
const char *apps_subtitle(i32 i) { return APPS[i].subtitle; }
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
     *   yellow → recentre the window on the screen (-)  - poor man's
     *            "minimise" until we have a proper dock-stash flow.
     *   green  → toggle maximised (+)                                  */
    i32 ty = wy + 18;
    if (my >= ty - 10 && my <= ty + 10) {
        if (mx >= wx + 9  && mx <= wx + 27) { apps_close(); return true; }
        if (mx >= wx + 29 && mx <= wx + 47) {
            wm_dx = 0; wm_dy = 0;          /* recentre */
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

    /* dim + softly blur the desktop behind the window so the app card
     * lifts off the wallpaper exactly like macOS sheets. We blur in
     * horizontal strips to fit the BLUR scratch buffer; without Aero
     * we just darken at a flat alpha as before.                       */
    if (SET.aero_enabled) {
        i32 W = (i32)FB.width;
        i32 H = (i32)FB.height;
        i32 strip = 320;
        for (i32 y = 0; y < H; y += strip) {
            i32 sh = (y + strip > H) ? H - y : strip;
            gfx_blur_rect(0, y, W, sh, 5);
        }
        gfx_rect_a(0, 0, W, H, COL_SHADOW, 70);
    } else {
        gfx_rect_a(0, 0, FB.width, FB.height, COL_SHADOW, 60);
    }

    i32 wx, wy, ww, wh;
    wm_window_rect(&wx, &wy, &ww, &wh);

    /* slide-in: 200 ms — only on first open, not while dragging */
    if (!wm_dragging && !wm_resizing) {
        u32 dt = pit_ms() - open_at_ms;
        if (dt > 200) dt = 200;
        i32 off = (i32)((200 - dt) * 60 / 200);
        wy += off;
    }

    /* card — Aero blurs the desktop / dock / widgets behind the
     * window so the chrome feels lifted; the body remains solid
     * because most apps render their own opaque content into it.   */
    gfx_round_rect_a(wx + 4, wy + 12, ww, wh, 18, COL_SHADOW, 70);   /* shadow */
    if (SET.aero_enabled) {
        gfx_blur_rect(wx, wy, ww, wh, 6);
        gfx_round_rect_a(wx, wy, ww, wh, 18, PAL_PANEL, 220);
    } else {
        gfx_round_rect_a(wx, wy, ww, wh, 18, PAL_PANEL, 245);
    }
    gfx_round_outline(wx, wy, ww, wh, 18, PAL_HAIRLINE);

    /* title bar — macOS-spec traffic lights on the left.
     *   x+18  red    close       (#FF5F57)
     *   x+38  yellow minimise/   (#FEBC2E)  — we use it as "centre"
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
    gfx_text_centered(wx + ww / 2, wy + 12, a->name, PAL_TEXT_DIM);

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
        "drag title-bar  ·  resize corner  ·  Esc / red close",
        PAL_TEXT_FAINT);
}
