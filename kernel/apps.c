/* =============================================================================
 *  FalconOS — application framework (Personal kernel)
 * -----------------------------------------------------------------------------
 *  A handful of self-contained mini-apps surfaced through the dock.  Each
 *  app has a name, dock tint, and a render() callback that fills a window
 *  rectangle.  An optional input() callback receives keys while the app is
 *  active.  Esc closes; F1 still hot-switches to the Developer Kernel.
 * =============================================================================
 *  Built-in apps:
 *    Home    — shortcut card grid
 *    Files   — fake "in-memory" file browser
 *    Clock   — analog dial driven by PIT uptime
 *    Stats   — live tsc / uptime / RAM card
 *    About   — system info & credits
 * ============================================================================= */
#include "falcon.h"

static i32 active_app = -1;
static u32 open_at_ms = 0;     /* used for slide-in animation */

void apps_open(i32 i)  { active_app = i; open_at_ms = pit_ms(); }
void apps_close(void)  { active_app = -1; }
i32  apps_active(void) { return active_app; }

/* ===== app implementations ================================================ */
static void icon_home(i32 cx, i32 cy)
{
    gfx_circle(cx, cy, 18, COL_BG_TOP);
    gfx_rect(cx - 12, cy - 4, 24, 16, COL_BG_TOP);
    gfx_circle(cx, cy - 8, 4, COL_TEXT);
}

static void icon_files(i32 cx, i32 cy)
{
    gfx_round_rect(cx - 14, cy - 10, 28, 20, 3, COL_BG_TOP);
    gfx_rect(cx - 14, cy - 10, 14, 4, COL_BG_TOP);
}

static void icon_clock(i32 cx, i32 cy)
{
    gfx_circle(cx, cy, 16, COL_BG_TOP);
    gfx_circle(cx, cy, 13, COL_TEXT);
    gfx_line(cx, cy, cx, cy - 9, COL_BG_TOP);
    gfx_line(cx, cy, cx + 7, cy + 2, COL_BG_TOP);
    gfx_circle(cx, cy, 2, COL_BG_TOP);
}

static void icon_stats(i32 cx, i32 cy)
{
    for (i32 i = 0; i < 5; i++) {
        i32 h = 4 + (i * 3);
        gfx_rect(cx - 12 + i * 5, cy + 8 - h, 3, h, COL_BG_TOP);
    }
}

static void icon_about(i32 cx, i32 cy)
{
    gfx_circle(cx, cy, 16, COL_BG_TOP);
    gfx_text_centered(cx, cy - 7, "i", COL_TEXT);
}

/* ---- Home: card grid --------------------------------------------------- */
static void render_home(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame;
    gfx_text(wx + 24, wy + 16, "Welcome to FalconOS", COL_TEXT);
    gfx_text(wx + 24, wy + 38, "Quick links", COL_TEXT_DIM);

    const char *cards[] = {
        "Recent docs",  "System info",
        "Network",      "Theme",
    };
    const u32 tints[] = { COL_ACCENT, COL_OK, COL_WARN, 0xC084FC };

    i32 cw = (ww - 72) / 2;
    i32 ch = 80;
    for (i32 i = 0; i < 4; i++) {
        i32 r = i / 2, c = i % 2;
        i32 x = wx + 24 + c * (cw + 16);
        i32 y = wy + 64 + r * (ch + 16);
        gfx_round_rect_a(x, y, cw, ch, 12, COL_PANEL_HI, 220);
        gfx_round_outline(x, y, cw, ch, 12, COL_PANEL_HI);
        gfx_circle(x + 22, y + 22, 10, tints[i]);
        gfx_text(x + 44, y + 14, cards[i], COL_TEXT);
        gfx_text(x + 44, y + 38, "Tap to explore",  COL_TEXT_DIM);
    }
}

/* ---- Files: fake list -------------------------------------------------- */
static const char *FAKE_FILES[] = {
    "README.md",  "boot/multiboot2.asm",  "kernel/main.c",
    "kernel/gfx.c", "kernel/personal.c", "kernel/dev.c",
    "kernel/idt.c", "Makefile",
};

static void render_files(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame; (void)ww; (void)wh;
    gfx_text(wx + 24, wy + 16, "Files", COL_TEXT);
    gfx_text(wx + 24, wy + 36, "/ (root)", COL_TEXT_DIM);
    i32 n = (i32)(sizeof FAKE_FILES / sizeof *FAKE_FILES);
    for (i32 i = 0; i < n; i++) {
        i32 y = wy + 64 + i * 22;
        gfx_round_rect_a(wx + 22, y - 2, ww - 44, 20, 6, COL_PANEL_HI, 200);
        gfx_circle(wx + 36, y + 8, 5, COL_ACCENT);
        gfx_text(wx + 50, y + 2, FAKE_FILES[i], COL_TEXT);
    }
}

/* ---- Clock: analog dial ------------------------------------------------ */
static void render_clock(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame;
    gfx_text(wx + 24, wy + 16, "Clock", COL_TEXT);
    u32 h, m, s; pit_uptime(&h, &m, &s);

    i32 cx = wx + ww / 2;
    i32 cy = wy + wh / 2 + 10;
    i32 R  = 110;
    gfx_circle(cx, cy, R + 6, COL_PANEL_HI);
    gfx_circle(cx, cy, R, COL_PANEL);

    /* hour ticks */
    for (i32 i = 0; i < 12; i++) {
        i32 a = i * 30;
        /* simple cos/sin via lookup of a few angles wouldn't be
         * worth the bytes — use the fact tick marks live at cardinal
         * + 30/60/120/150 degrees: approximate with two-segment fan */
        static const i8 SX[12] = {  0, 50, 87,100, 87, 50,  0,-50,-87,-100,-87,-50};
        static const i8 SY[12] = {-100,-87,-50,  0, 50, 87,100, 87, 50,   0,-50,-87};
        i32 ex = cx + (i32)SX[i] * R / 100;
        i32 ey = cy + (i32)SY[i] * R / 100;
        gfx_circle(ex, ey, 3, COL_TEXT_DIM);
        (void)a;
    }

    /* approximate hand vectors using a 60-step LUT */
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

    /* hour hand */
    gfx_line(cx, cy, cx + (i32)LX[hi] * (R - 40) / 100,
                      cy + (i32)LY[hi] * (R - 40) / 100, COL_TEXT);
    /* minute hand */
    gfx_line(cx, cy, cx + (i32)LX[mi] * (R - 16) / 100,
                      cy + (i32)LY[mi] * (R - 16) / 100, COL_ACCENT);
    /* seconds hand */
    gfx_line(cx, cy, cx + (i32)LX[si] * (R - 8) / 100,
                      cy + (i32)LY[si] * (R - 8) / 100, COL_ERR);
    gfx_circle(cx, cy, 5, COL_TEXT);

    /* digital readout */
    char buf[16] = "00:00:00";
    char tmp[8];
    k_itoa(h, tmp, 10); if (h < 10) { buf[0]='0'; buf[1]=tmp[0]; } else { buf[0]=tmp[0]; buf[1]=tmp[1]; }
    k_itoa(m, tmp, 10); if (m < 10) { buf[3]='0'; buf[4]=tmp[0]; } else { buf[3]=tmp[0]; buf[4]=tmp[1]; }
    k_itoa(s, tmp, 10); if (s < 10) { buf[6]='0'; buf[7]=tmp[0]; } else { buf[6]=tmp[0]; buf[7]=tmp[1]; }
    buf[8]=0;
    gfx_text_centered(cx, cy + R + 24, buf, COL_TEXT);
}

/* ---- Stats: live numbers ---------------------------------------------- */
static void render_stats(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame; (void)wh;
    gfx_text(wx + 24, wy + 16, "Stats", COL_TEXT);

    u64 t = rdtsc();
    u32 hi = (u32)(t >> 32), lo = (u32)t;
    u32 h, m, s; pit_uptime(&h, &m, &s);

    char hex[16];
    char line[64];

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

    /* pulse bar */
    i32 pw = (i32)((g_ticks % 100) * (ww - 48) / 100);
    gfx_round_rect(wx + 24, wy + 168, ww - 48, 6, 3, COL_PANEL_HI);
    gfx_round_rect(wx + 24, wy + 168, pw,      6, 3, COL_ACCENT);
}

/* ---- About ------------------------------------------------------------- */
static void render_about(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame; (void)wh;
    gfx_text(wx + 24, wy + 16, "About FalconOS", COL_TEXT);

    i32 cx = wx + ww / 2;
    gfx_circle(cx, wy + 100, 56, COL_ACCENT);
    gfx_circle(cx, wy + 100, 36, COL_BG_TOP);
    gfx_circle(cx, wy + 100, 18, COL_ACCENT);

    gfx_text_centered(cx, wy + 180, "FalconOS",         COL_TEXT);
    gfx_text_centered(cx, wy + 200, "v3 \"Aurora\"",      COL_ACCENT);
    gfx_text_centered(cx, wy + 224, "two kernels in one binary",       COL_TEXT_DIM);
    gfx_text_centered(cx, wy + 244, "F1 to flip - mouse to interact",  COL_TEXT_DIM);
}

/* ===== app table & dispatch ============================================= */
typedef void (*app_render_fn)(i32 x, i32 y, i32 w, i32 h, u32 f);

typedef struct {
    const char     *name;
    u32             tint;
    app_render_fn   render;
    void          (*draw_icon)(i32 cx, i32 cy);
} app_def_t;

static app_def_t APPS[] = {
    { "Home",   0x4F9EFF, render_home,   icon_home   },
    { "Files",  0xFFB547, render_files,  icon_files  },
    { "Clock",  0x46D160, render_clock,  icon_clock  },
    { "Stats",  0xFF5E57, render_stats,  icon_stats  },
    { "About",  0xC084FC, render_about,  icon_about  },
};

i32 apps_count(void) { return (i32)(sizeof APPS / sizeof *APPS); }

const char *apps_name(i32 i)
{
    return APPS[i].name;
}

u32 apps_tint(i32 i) { return APPS[i].tint; }

void apps_draw_icon(i32 i, i32 cx, i32 cy)
{
    if (APPS[i].draw_icon) APPS[i].draw_icon(cx, cy);
}

void apps_input_active(i32 key)
{
    if (active_app < 0) return;
    if (key == KEY_ESC) { apps_close(); return; }
}

/* renders the active app's window with a slide-in animation */
void apps_render_active(u32 frame)
{
    if (active_app < 0) return;
    const app_def_t *a = &APPS[active_app];

    /* dim the desktop behind the window */
    gfx_rect_a(0, 0, FB.width, FB.height, 0x000000, 80);

    /* window box */
    i32 ww = 720, wh = 460;
    i32 wx = ((i32)FB.width - ww) / 2;
    i32 wy = ((i32)FB.height - wh) / 2 - 10;

    /* slide-in: 200 ms */
    u32 dt = pit_ms() - open_at_ms;
    if (dt > 200) dt = 200;
    i32 off = (i32)((200 - dt) * 60 / 200);
    wy += off;

    /* card */
    gfx_round_rect_a(wx + 4, wy + 8, ww, wh, 16, 0x000000, 90);   /* shadow */
    gfx_round_rect_a(wx,     wy,     ww, wh, 16, COL_PANEL,   245);
    gfx_round_outline(wx,     wy,     ww, wh, 16, COL_PANEL_HI);

    /* title bar */
    gfx_circle(wx + 16,  wy + 16, 6, COL_ERR);
    gfx_circle(wx + 32,  wy + 16, 6, COL_WARN);
    gfx_circle(wx + 48,  wy + 16, 6, COL_OK);
    gfx_circle(wx + ww - 24, wy + 16, 8, a->tint);
    gfx_text_centered(wx + ww / 2, wy + 10, a->name, COL_TEXT_DIM);

    /* body offset by 40 px for title strip */
    a->render(wx, wy + 40, ww, wh - 40, frame);

    /* hint */
    gfx_text_centered(wx + ww / 2, wy + wh - 22, "press Esc to close", COL_TEXT_DIM);
}
