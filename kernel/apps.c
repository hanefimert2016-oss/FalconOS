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

void apps_open(i32 i)  { active_app = i; open_at_ms = pit_ms(); }
void apps_close(void)  { active_app = -1; }
i32  apps_active(void) { return active_app; }

/* ===== icon glyphs ======================================================== */
static void icon_home(i32 cx, i32 cy)
{
    /* roof + house */
    gfx_line(cx - 14, cy - 2, cx,        cy - 14, COL_PANEL);
    gfx_line(cx,      cy - 14, cx + 14, cy - 2,  COL_PANEL);
    gfx_rect(cx - 11, cy - 2,  22, 14, COL_PANEL);
    gfx_rect(cx -  3, cy + 4,   6,  8, 0x000000);
}
static void icon_files(i32 cx, i32 cy)
{
    gfx_round_rect(cx - 14, cy - 10, 28, 20, 3, COL_PANEL);
    gfx_rect(cx - 14, cy - 14, 14, 6, COL_PANEL);
    gfx_rect(cx - 10, cy - 4, 20, 1, COL_ACCENT);
    gfx_rect(cx - 10, cy + 0, 16, 1, COL_ACCENT);
    gfx_rect(cx - 10, cy + 4, 12, 1, COL_ACCENT);
}
static void icon_clock(i32 cx, i32 cy)
{
    gfx_circle(cx, cy, 16, COL_PANEL);
    gfx_circle(cx, cy, 13, 0xFFFFFF);
    gfx_line(cx, cy, cx, cy - 9, COL_TEXT);
    gfx_line(cx, cy, cx + 7, cy + 2, COL_ACCENT);
    gfx_circle(cx, cy, 2, COL_TEXT);
}
static void icon_stats(i32 cx, i32 cy)
{
    for (i32 i = 0; i < 5; i++) {
        i32 h = 4 + (i * 3);
        gfx_rect(cx - 12 + i * 5, cy + 8 - h, 3, h, COL_PANEL);
    }
}
static void icon_about(i32 cx, i32 cy)
{
    gfx_circle(cx, cy, 16, COL_PANEL);
    gfx_text_centered(cx, cy - 7, "i", COL_TEXT);
}
static void icon_term(i32 cx, i32 cy)
{
    gfx_round_rect(cx - 16, cy - 12, 32, 24, 3, 0x101218);
    gfx_text(cx - 12, cy - 6, ">_", COL_OK);
}
static void icon_calc(i32 cx, i32 cy)
{
    gfx_round_rect(cx - 14, cy - 14, 28, 28, 4, COL_PANEL);
    gfx_rect(cx - 10, cy - 10, 20, 6, COL_TEXT);
    for (i32 i = 0; i < 3; i++)
        for (i32 j = 0; j < 3; j++)
            gfx_rect(cx - 10 + j * 7, cy - 1 + i * 5, 4, 3, COL_ACCENT);
}
static void icon_settings(i32 cx, i32 cy)
{
    gfx_circle(cx, cy, 12, COL_PANEL);
    gfx_circle(cx, cy, 5, COL_ACCENT);
    /* gear teeth */
    for (i32 a = 0; a < 8; a++) {
        i32 ax = (a == 0 || a == 4) ? 0 : (a < 4 ? 12 : -12);
        i32 ay = (a == 2 || a == 6) ? 0 : (a < 2 || a > 6 ? -12 : 12);
        gfx_rect(cx + ax / 2 - 1, cy + ay / 2 - 1, 3, 3, COL_PANEL);
    }
}
static void icon_notes(i32 cx, i32 cy)
{
    gfx_round_rect(cx - 13, cy - 14, 26, 28, 2, COL_PANEL);
    gfx_rect(cx - 10, cy - 9, 20, 1, COL_TEXT_FAINT);
    gfx_rect(cx - 10, cy - 5, 16, 1, COL_TEXT_FAINT);
    gfx_rect(cx - 10, cy - 1, 18, 1, COL_TEXT_FAINT);
    gfx_rect(cx - 10, cy + 3, 14, 1, COL_TEXT_FAINT);
}
static void icon_calendar(i32 cx, i32 cy)
{
    gfx_round_rect(cx - 14, cy - 13, 28, 26, 3, COL_PANEL);
    gfx_rect(cx - 14, cy - 13, 28, 8, COL_ERR);
    /* day grid dots */
    for (i32 r = 0; r < 3; r++)
        for (i32 c = 0; c < 5; c++)
            gfx_pixel(cx - 10 + c * 4, cy - 1 + r * 4, COL_TEXT_DIM);
}
static void icon_gallery(i32 cx, i32 cy)
{
    gfx_rect(cx - 14, cy - 12, 12, 12, COL_ACCENT);
    gfx_rect(cx +  2, cy - 12, 12, 12, COL_OK);
    gfx_rect(cx - 14, cy + 0,  12, 12, COL_WARN);
    gfx_rect(cx +  2, cy + 0,  12, 12, COL_PURPLE);
}
static void icon_browser(i32 cx, i32 cy)
{
    gfx_circle(cx, cy, 16, COL_PANEL);
    gfx_circle_outline(cx, cy, 12, COL_ACCENT);
    gfx_line(cx - 12, cy, cx + 12, cy, COL_ACCENT);
    gfx_line(cx, cy - 12, cx, cy + 12, COL_ACCENT);
}

/* ===== app render functions ============================================== */

/* shared section header */
static void section(i32 wx, i32 wy, const char *title, const char *subtitle)
{
    gfx_text(wx + 24, wy + 6,  title,    COL_TEXT);
    gfx_text(wx + 24, wy + 28, subtitle, COL_TEXT_DIM);
}

/* --- Home: quick-link cards ---------------------------------------------- */
static void render_home(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame;
    section(wx, wy, "Welcome to FalconOS", "Quick links");

    const char *cards[]    = { "Recent docs", "System info", "Network", "Theme" };
    const char *subs[]     = { "open last 5", "uptime/ram",  "no network",  "Lumen blue" };
    const u32   tints[]    = { COL_ACCENT, COL_OK, COL_WARN, COL_PURPLE };

    i32 cw = (ww - 72) / 2;
    i32 ch = (wh - 100) / 2;
    for (i32 i = 0; i < 4; i++) {
        i32 r = i / 2, c = i % 2;
        i32 x = wx + 24 + c * (cw + 16);
        i32 y = wy + 60 + r * (ch + 16);
        gfx_round_rect_a(x, y, cw, ch, 12, COL_PANEL_DEEP, 255);
        gfx_round_outline(x, y, cw, ch, 12, COL_HAIRLINE);
        gfx_circle(x + 26, y + 24, 12, tints[i]);
        gfx_text(x + 50, y + 12, cards[i], COL_TEXT);
        gfx_text(x + 50, y + 36, subs[i],  COL_TEXT_DIM);
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
            gfx_round_rect_a(wx + 22, y - 2, ww - 44, 20, 6, COL_PANEL_DEEP, 255);
        gfx_circle(wx + 36, y + 8, 5, COL_ACCENT);
        gfx_text(wx + 50, y + 2, FAKE_FILES[i], COL_TEXT);
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

    gfx_circle(cx, cy, R + 8, COL_HAIRLINE);
    gfx_circle(cx, cy, R,     COL_PANEL_DEEP);

    static const i8 SX[12] = {  0, 50, 87,100, 87, 50,  0,-50,-87,-100,-87,-50};
    static const i8 SY[12] = {-100,-87,-50,  0, 50, 87,100, 87, 50,   0,-50,-87};
    for (i32 i = 0; i < 12; i++) {
        i32 ex = cx + (i32)SX[i] * R / 100;
        i32 ey = cy + (i32)SY[i] * R / 100;
        gfx_circle(ex, ey, 3, COL_TEXT_DIM);
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
                      cy + (i32)LY[hi] * (R - 40) / 100, COL_TEXT);
    gfx_line(cx, cy, cx + (i32)LX[mi] * (R - 16) / 100,
                      cy + (i32)LY[mi] * (R - 16) / 100, COL_ACCENT);
    gfx_line(cx, cy, cx + (i32)LX[si] * (R -  8) / 100,
                      cy + (i32)LY[si] * (R -  8) / 100, COL_ERR);
    gfx_circle(cx, cy, 5, COL_TEXT);

    char buf[16] = "00:00:00";
    char tmp[8];
    k_itoa(h, tmp, 10); if (h < 10) { buf[0]='0'; buf[1]=tmp[0]; } else { buf[0]=tmp[0]; buf[1]=tmp[1]; }
    k_itoa(m, tmp, 10); if (m < 10) { buf[3]='0'; buf[4]=tmp[0]; } else { buf[3]=tmp[0]; buf[4]=tmp[1]; }
    k_itoa(s, tmp, 10); if (s < 10) { buf[6]='0'; buf[7]=tmp[0]; } else { buf[6]=tmp[0]; buf[7]=tmp[1]; }
    buf[8]=0;
    gfx_text_centered(cx, cy + R + 24, buf, COL_TEXT);
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
    gfx_text(wx + 24, wy + 60, line, COL_TEXT);

    char th[8], tm[8], ts[8];
    k_itoa(h, th, 10); k_itoa(m, tm, 10); k_itoa(s, ts, 10);
    k_strcpy(line, "uptime    ");
    k_strcat(line, th); k_strcat(line, "h ");
    k_strcat(line, tm); k_strcat(line, "m ");
    k_strcat(line, ts); k_strcat(line, "s");
    gfx_text(wx + 24, wy + 86, line, COL_TEXT);

    k_strcpy(line, "ticks     ");
    k_itoa(g_ticks, hex, 10); k_strcat(line, hex);
    gfx_text(wx + 24, wy + 110, line, COL_TEXT);

    k_strcpy(line, "ram (KiB) ");
    k_itoa(RAM_TOTAL_KB, hex, 10); k_strcat(line, hex);
    gfx_text(wx + 24, wy + 134, line, COL_TEXT);

    k_strcpy(line, "fb        ");
    k_itoa(FB.width, hex, 10); k_strcat(line, hex); k_strcat(line, "x");
    k_itoa(FB.height, hex, 10); k_strcat(line, hex);
    gfx_text(wx + 24, wy + 158, line, COL_TEXT);

    /* pulse bar */
    i32 pw = (i32)((g_ticks % 100) * (ww - 48) / 100);
    gfx_round_rect(wx + 24, wy + 188, ww - 48, 6, 3, COL_HAIRLINE);
    gfx_round_rect(wx + 24, wy + 188, pw,      6, 3, COL_ACCENT);
}

/* --- About --------------------------------------------------------------- */
static void render_about(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame; (void)wh;
    section(wx, wy, "About FalconOS", "v4 \"Lumen\"");

    i32 cx = wx + ww / 2;
    gfx_circle(cx, wy + 110, 56, COL_ACCENT);
    gfx_circle(cx, wy + 110, 36, COL_PANEL);
    gfx_circle(cx, wy + 110, 18, COL_ACCENT);

    gfx_text_centered(cx, wy + 190, "FalconOS",                         COL_TEXT);
    gfx_text_centered(cx, wy + 210, "v4 \"Lumen\"",                      COL_ACCENT);
    gfx_text_centered(cx, wy + 232, "two kernels in one binary",        COL_TEXT_DIM);
    gfx_text_centered(cx, wy + 252, "F1 to flip - F2 Launchpad",        COL_TEXT_DIM);
    gfx_text_centered(cx, wy + 272, "bare-metal, no libc, ~2.5 kLOC",   COL_TEXT_FAINT);
}

/* --- Terminal (visual fake prompt) --------------------------------------- */
#define TERM_LINES 12
#define TERM_COLS  80
static char term_buf[TERM_LINES][TERM_COLS] = {
    "FalconOS terminal v4 - bare metal, no shell",
    "type something and press Enter (echo only)",
    "this isn't a real shell - just a visual demo",
    "for actual commands, switch to Developer Kernel (F1)",
    "and use the REPL: help / peek / poke / regs / time",
    "",
    "user@falcon:~$ uname",
    "FalconOS Lumen i386 freestanding",
    "user@falcon:~$ echo hello",
    "hello",
    "user@falcon:~$ _",
    ""
};
static i32 term_input_len = 0;
static char term_input[TERM_COLS];

static void render_term(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame;
    /* dark terminal panel for contrast against light theme */
    gfx_round_rect_a(wx + 20, wy + 8, ww - 40, wh - 28, 10, 0x101218, 255);
    gfx_round_outline(wx + 20, wy + 8, ww - 40, wh - 28, 10, COL_HAIRLINE);

    gfx_text(wx + 30, wy + 18, "Terminal", 0xCFE6FF);
    gfx_text(wx + 30, wy + 38, "bash-stub - press keys to type, Enter echoes back",
             0x8AAACE);

    for (i32 i = 0; i < TERM_LINES; i++) {
        gfx_text(wx + 32, wy + 64 + i * 18, term_buf[i],
                 (i == TERM_LINES - 1) ? COL_OK : 0xDEEEFF);
    }
    /* current input line */
    char line[TERM_COLS + 12] = "user@falcon:~$ ";
    k_strcat(line, term_input);
    if ((g_ticks / 50) & 1) k_strcat(line, "_");
    gfx_text(wx + 32, wy + 64 + TERM_LINES * 18, line, COL_OK);
}

static void term_input_key(i32 key)
{
    if (key == KEY_ENTER) {
        /* echo input back as new line in scroll */
        for (i32 i = 1; i < TERM_LINES; i++)
            k_strcpy(term_buf[i - 1], term_buf[i]);
        char echo[TERM_COLS + 12] = "user@falcon:~$ ";
        k_strcat(echo, term_input);
        k_strcpy(term_buf[TERM_LINES - 1], echo);
        term_input_len = 0;
        term_input[0] = 0;
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
    gfx_round_rect_a(dx, dy, dw, dh, 12, COL_PANEL_DEEP, 255);
    gfx_round_outline(dx, dy, dw, dh, 12, COL_HAIRLINE);
    gfx_text(dx + dw - gfx_text_width(calc_disp) - 16, dy + 22, calc_disp, COL_TEXT);

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
    gfx_text(dx + 16, dy + 22, hint, COL_TEXT_DIM);

    /* on-screen keypad (visual only) */
    const char *keys = "789+456-123*0=c/";
    i32 bx = wx + 24, by = wy + 140, bw = (ww - 48) / 4 - 6, bh = 36;
    for (i32 i = 0; i < 16; i++) {
        i32 r = i / 4, c = i % 4;
        i32 x = bx + c * (bw + 8), y = by + r * (bh + 8);
        bool op = (c == 3) || keys[i] == '=' || keys[i] == 'c';
        gfx_round_rect_a(x, y, bw, bh, 8, op ? COL_ACCENT_DIM : COL_PANEL_DEEP, 255);
        gfx_round_outline(x, y, bw, bh, 8, COL_HAIRLINE);
        char b[2] = {keys[i], 0};
        gfx_text_centered(x + bw / 2, y + bh / 2 - 8, b, COL_TEXT);
    }
}

/* --- Settings ------------------------------------------------------------ */
static bool set_dark = false;
static i32  set_accent = 0;
static const u32 ACCENTS[] = { 0x3070FF, 0x2BB673, 0xA45EE5, 0xF59F1A, 0xE53935 };
static const char *ACCNAMES[] = { "Blue", "Green", "Purple", "Amber", "Red" };

static void set_input_key(i32 key)
{
    if (key == KEY_LEFT  && set_accent > 0) set_accent--;
    if (key == KEY_RIGHT && set_accent < 4) set_accent++;
    if (key == KEY_ENTER || key == ' ')     set_dark = !set_dark;
}
static void render_settings(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame; (void)wh;
    section(wx, wy, "Settings", "<- -> accent     Enter toggle dark    (preview only)");

    /* dark-mode card */
    i32 dx = wx + 24, dy = wy + 60, dw = ww - 48, dh = 70;
    gfx_round_rect_a(dx, dy, dw, dh, 12, COL_PANEL_DEEP, 255);
    gfx_round_outline(dx, dy, dw, dh, 12, COL_HAIRLINE);
    gfx_text(dx + 16, dy + 14, "Appearance",  COL_TEXT);
    gfx_text(dx + 16, dy + 38, set_dark ? "Dark mode" : "Light mode (active)",
             COL_TEXT_DIM);
    /* toggle pill */
    i32 tw = 50, th = 24;
    i32 tx = dx + dw - tw - 16, ty = dy + (dh - th) / 2;
    gfx_round_rect_a(tx, ty, tw, th, 12, set_dark ? COL_ACCENT : COL_HAIRLINE, 255);
    gfx_circle(set_dark ? tx + tw - 12 : tx + 12, ty + th / 2, 10, 0xFFFFFF);

    /* accent picker */
    i32 ax = wx + 24, ay = wy + 150, aw = ww - 48;
    gfx_round_rect_a(ax, ay, aw, 90, 12, COL_PANEL_DEEP, 255);
    gfx_round_outline(ax, ay, aw, 90, 12, COL_HAIRLINE);
    gfx_text(ax + 16, ay + 14, "Accent color", COL_TEXT);
    for (i32 i = 0; i < 5; i++) {
        i32 cx = ax + 32 + i * 56;
        i32 cy = ay + 56;
        gfx_circle(cx, cy, 18, ACCENTS[i]);
        if (i == set_accent) gfx_circle_outline(cx, cy, 22, COL_TEXT);
    }
    gfx_text(ax + 16 + 5 * 56 + 10, ay + 50, ACCNAMES[set_accent], COL_TEXT_DIM);
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
    gfx_round_rect_a(px, py, pw, ph, 12, COL_PANEL_DEEP, 255);
    gfx_round_outline(px, py, pw, ph, 12, COL_HAIRLINE);

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
            gfx_text(px + 14, ty, tmp, COL_TEXT);
            ty += 18;
            line_start = i + 1;
            if (ty > py + ph - 24) break;
        }
    }
    /* caret */
    if ((g_ticks / 50) & 1) gfx_text(px + 14 + (notes_len - line_start) * 8, ty, "_", COL_ACCENT);
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
        gfx_text_centered(x + cw / 2, wy + 60, DOW[c], COL_TEXT_DIM);
    }
    for (i32 d = 0; d < 28; d++) {
        i32 r = d / 7, c = d % 7;
        i32 x = wx + 30 + c * cw;
        i32 y = wy + 84 + r * ch;
        bool isToday = ((d + 1) == today);
        gfx_round_rect_a(x + 2, y + 2, cw - 4, ch - 4, 8,
                         isToday ? COL_ACCENT : COL_PANEL_DEEP, 255);
        gfx_round_outline(x + 2, y + 2, cw - 4, ch - 4, 8, COL_HAIRLINE);
        char buf[4]; k_itoa(d + 1, buf, 10);
        gfx_text_centered(x + cw / 2, y + ch / 2 - 8, buf,
                          isToday ? 0xFFFFFF : COL_TEXT);
    }
}

/* --- Gallery (Lumen palette) -------------------------------------------- */
static void render_gallery(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame; (void)wh;
    section(wx, wy, "Gallery", "Lumen palette swatches");

    struct { u32 c; const char *n; } SW[] = {
        { COL_ACCENT,    "Blue 50"   },
        { COL_OK,        "Green 50"  },
        { COL_WARN,      "Amber 50"  },
        { COL_ERR,       "Red 50"    },
        { COL_PURPLE,    "Purple 50" },
        { COL_TEAL,      "Teal 50"   },
        { COL_BG_TOP,    "BG Top"    },
        { COL_BG_BOT,    "BG Bottom" },
        { COL_PANEL,     "Panel"     },
        { COL_PANEL_HI,  "Hairline"  },
    };
    i32 n = (i32)(sizeof SW / sizeof *SW);
    i32 cw = (ww - 60) / 5;
    i32 ch = 110;
    for (i32 i = 0; i < n; i++) {
        i32 r = i / 5, c = i % 5;
        i32 x = wx + 24 + c * (cw + 4);
        i32 y = wy + 70 + r * (ch + 12);
        gfx_round_rect(x, y, cw, ch - 28, 8, SW[i].c);
        gfx_round_outline(x, y, cw, ch - 28, 8, COL_HAIRLINE);
        gfx_text(x + 4, y + ch - 24, SW[i].n, COL_TEXT_DIM);

        char hex[16] = "0x";
        char tmp[8]; k_itoa(SW[i].c, tmp, 16);
        i32 pad = 6 - k_strlen(tmp);
        for (i32 p = 0; p < pad; p++) k_strcat(hex, "0");
        k_strcat(hex, tmp);
        gfx_text(x + 4, y + ch - 8, hex, COL_TEXT_FAINT);
    }
}

/* --- Browser (visual mock) ---------------------------------------------- */
static void render_browser(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame; (void)wh;
    section(wx, wy, "Browser", "no network - visual mock");

    /* address bar */
    i32 ax = wx + 24, ay = wy + 60, aw = ww - 48;
    gfx_round_rect_a(ax, ay, aw, 36, 18, COL_PANEL_DEEP, 255);
    gfx_round_outline(ax, ay, aw, 36, 18, COL_HAIRLINE);
    gfx_circle(ax + 18, ay + 18, 6, COL_OK);
    gfx_text(ax + 36, ay + 12, "https://falcon.os/welcome", COL_TEXT);

    /* bookmarks */
    const char *BM[] = { "Docs", "Repo", "Lumen Notes", "F1 dev" };
    const u32   BC[] = { COL_ACCENT, COL_OK, COL_PURPLE, COL_WARN };
    i32 cw = (ww - 80) / 4, cy = wy + 120, ch = 90;
    for (i32 i = 0; i < 4; i++) {
        i32 cx = wx + 24 + i * (cw + 12);
        gfx_round_rect_a(cx, cy, cw, ch, 12, COL_PANEL_DEEP, 255);
        gfx_round_outline(cx, cy, cw, ch, 12, COL_HAIRLINE);
        gfx_circle(cx + cw / 2, cy + 32, 18, BC[i]);
        gfx_text_centered(cx + cw / 2, cy + 64, BM[i], COL_TEXT);
    }

    gfx_text(wx + 24, wy + 240, "(network stack not implemented)", COL_TEXT_FAINT);
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
    { "Home",       "quick links",       0x3070FF, render_home,     NULL,            icon_home     },
    { "Files",      "in-memory tree",    0xF59F1A, render_files,    NULL,            icon_files    },
    { "Clock",      "PIT analog dial",   0x2BB673, render_clock,    NULL,            icon_clock    },
    { "Stats",      "system telemetry",  0xE53935, render_stats,    NULL,            icon_stats    },
    { "Terminal",   "fake bash prompt",  0x14181F, render_term,     term_input_key,  icon_term     },
    { "Calculator", "+ - * /",           0xA45EE5, render_calc,     calc_input_key,  icon_calc     },
    { "Settings",   "preview only",      0x6E7884, render_settings, set_input_key,   icon_settings },
    { "Notes",      "free-form pad",     0xFFB547, render_notes,    notes_input_key, icon_notes    },
    { "Calendar",   "month view",        0x16B5A8, render_calendar, NULL,            icon_calendar },
    { "Gallery",    "Lumen palette",     0xC084FC, render_gallery,  NULL,            icon_gallery  },
    { "Browser",    "no network",        0x3070FF, render_browser,  NULL,            icon_browser  },
    { "About",      "v4 Lumen",          0xA45EE5, render_about,    NULL,            icon_about    },
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

/* renders the active app's window with a slide-in animation */
void apps_render_active(u32 frame)
{
    if (active_app < 0) return;
    const app_def_t *a = &APPS[active_app];

    /* dim the desktop behind the window */
    gfx_rect_a(0, 0, FB.width, FB.height, COL_SHADOW, 60);

    /* window box — sized for HD+, scaled up for higher resolutions */
    i32 ww = (i32)FB.width  - 280;  if (ww > 920) ww = 920; if (ww < 600) ww = 600;
    i32 wh = (i32)FB.height - 220;  if (wh > 580) wh = 580; if (wh < 380) wh = 380;
    i32 wx = ((i32)FB.width  - ww) / 2;
    i32 wy = ((i32)FB.height - wh) / 2 - 10;

    /* slide-in: 200 ms */
    u32 dt = pit_ms() - open_at_ms;
    if (dt > 200) dt = 200;
    i32 off = (i32)((200 - dt) * 60 / 200);
    wy += off;

    /* card */
    gfx_round_rect_a(wx + 4, wy + 12, ww, wh, 18, COL_SHADOW, 70);   /* shadow */
    gfx_round_rect_a(wx,     wy,      ww, wh, 18, COL_PANEL,   245);
    gfx_round_outline(wx,    wy,      ww, wh, 18, COL_HAIRLINE);

    /* title bar */
    gfx_circle(wx + 18,  wy + 18, 7, COL_ERR);
    gfx_circle(wx + 38,  wy + 18, 7, COL_WARN);
    gfx_circle(wx + 58,  wy + 18, 7, COL_OK);
    gfx_circle(wx + ww - 26, wy + 18, 8, a->tint);
    gfx_text_centered(wx + ww / 2, wy + 12, a->name, COL_TEXT_DIM);

    /* body offset by 44 px for title strip */
    a->render(wx, wy + 44, ww, wh - 44, frame);

    /* hint */
    gfx_text_centered(wx + ww / 2, wy + wh - 24, "press Esc to close", COL_TEXT_FAINT);
}
