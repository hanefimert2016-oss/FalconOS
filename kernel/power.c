/* =============================================================================
 *  FalconOS — Power menu  (FalconOS 1)
 * -----------------------------------------------------------------------------
 *  Click the power glyph at the right edge of the menu bar (or press Ctrl+P)
 *  to open a large modal with four tiles:
 *
 *      [ Shut down ]   [ Restart ]
 *      [   Sleep   ]   [ Sign out ]
 *
 *  - Shut down: ACPI shutdown via QEMU's documented IO ports (0xB004 for
 *               i440fx, 0x604 for q35).  On real hardware those ports are
 *               unmapped, so we fall back to a halt loop (the user can power
 *               off manually).
 *  - Restart:   8042 keyboard-controller reset pulse on port 0x64; if that
 *               doesn't take effect, we trigger a triple-fault by loading a
 *               null IDT and executing INT3.
 *  - Sleep:     dim the screen for ~600 ms then jump back to the lockscreen.
 *               (No ACPI S3 — that needs ASL parsing & paging save/restore.)
 *  - Sign out:  same effect as Sleep with no dim animation: lockscreen.
 *
 *  Sized at ~520x420 px so it dominates the screen the way the user asked
 *  for ("Zorin'inkinden büyük olsun"); centred over a 70%-darkened wallpaper.
 * ============================================================================= */
#include "falcon.h"

static bool g_open    = false;
static i32  g_cursor  = 0;            /* 0..3, keyboard nav */
static u32  g_open_at = 0;            /* g_ticks when opened (for slide-in) */

bool power_menu_is_open(void) { return g_open; }

void power_menu_open(void)
{
    g_open    = true;
    g_cursor  = 0;
    g_open_at = g_ticks;
}

void power_menu_close(void)
{
    g_open = false;
}

/* ----- raw ACPI / 8042 actions ------------------------------------------- */
static void do_shutdown(void)
{
    /* QEMU q35:    0x0604 */
    /* QEMU i440fx: 0xB004 */
    /* VirtualBox:  0x4004 */
    outw(0x0604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);
    /* If we get here, ACPI didn't take. Halt indefinitely so the user can
     * pull the power.  We keep interrupts off so nothing repaints.        */
    __asm__ volatile ("cli");
    for (;;) __asm__ volatile ("hlt");
}

static void do_restart(void)
{
    /* Drain the 8042 controller's input buffer, then issue the reset pulse */
    for (i32 i = 0; i < 32; i++) {
        u8 s = inb(0x64);
        if (!(s & 0x02)) break;
        (void)inb(0x60);
    }
    outb(0x64, 0xFE);

    /* Fallback: triple-fault by loading a null IDT then raising INT3.    */
    struct __attribute__((packed)) { u16 limit; u64 base; } null_idt = { 0, 0 };
    __asm__ volatile ("lidt %0\n\tint3" :: "m"(null_idt));

    /* If everything failed, halt. */
    __asm__ volatile ("cli");
    for (;;) __asm__ volatile ("hlt");
}

static void do_sleep_to_lockscreen(void)
{
    /* Quick fade-to-black animation, then lock. We can't truly suspend
     * without ACPI S3 + paging save/restore, so this is the closest UX
     * approximation: screen goes dark, requires password to come back. */
    for (i32 a = 0; a < 30; a++) {
        gfx_dim((u8)(a * 8 > 240 ? 240 : a * 8));
        gfx_present();
        u32 t0 = g_ticks; while (g_ticks - t0 < 1) __asm__ volatile ("hlt");
    }
    lockscreen_lock();
    g_open = false;
}

static void do_signout(void)
{
    /* Same effect — back to the chooser. The active_user is reset so the
     * cursor lands on the default; another user can take over.           */
    SET.active_user = SET.default_user;
    lockscreen_lock();
    g_open = false;
}

static void execute_action(i32 a)
{
    switch (a) {
        case 0: do_shutdown();           break;
        case 1: do_restart();            break;
        case 2: do_sleep_to_lockscreen();break;
        case 3: do_signout();            break;
    }
}

/* ----- layout helpers ---------------------------------------------------- */
typedef struct { i32 x, y, w, h; const char *label; const char *sub; u32 col; } tile_t;

static void compute_tiles(tile_t out[4])
{
    /* Modal is intentionally large — Zorin's power menu is roughly 360x260;
     * we go to ~520x420 with chunky 220x150 tiles for a cinematic feel.   */
    i32 W = (i32)FB.width, H = (i32)FB.height;
    i32 mw = 520, mh = 420;
    i32 mx = (W - mw) / 2;
    i32 my = (H - mh) / 2;

    i32 tw = 220, th = 150;
    i32 gap = 24;
    i32 col0_x = mx + (mw - (tw * 2 + gap)) / 2;
    i32 col1_x = col0_x + tw + gap;
    i32 row0_y = my + 80;
    i32 row1_y = row0_y + th + gap;

    static const char *L0[] = { "Shut down", "Restart", "Sleep", "Sign out" };
    static const char *L0_TR[] = { "Kapat", "Yeniden baslat", "Uyku", "Oturumu kapat" };
    static const char *S0[] = {
        "Power off the system",
        "Reboot the kernel",
        "Lock with screen dim",
        "Return to lockscreen",
    };
    static const char *S0_TR[] = {
        "Sistemi tamamen kapat",
        "Cekirdegi yeniden yukle",
        "Ekrani karart ve kilitle",
        "Kilit ekranina don",
    };
    bool tr = (SET.lang == LANG_TR);

    out[0] = (tile_t){ col0_x, row0_y, tw, th, tr ? L0_TR[0] : L0[0], tr ? S0_TR[0] : S0[0], COL_ERR };
    out[1] = (tile_t){ col1_x, row0_y, tw, th, tr ? L0_TR[1] : L0[1], tr ? S0_TR[1] : S0[1], COL_WARN };
    out[2] = (tile_t){ col0_x, row1_y, tw, th, tr ? L0_TR[2] : L0[2], tr ? S0_TR[2] : S0[2], PAL_ACCENT };
    out[3] = (tile_t){ col1_x, row1_y, tw, th, tr ? L0_TR[3] : L0[3], tr ? S0_TR[3] : S0[3], COL_OK };
}

static void glyph_shutdown(i32 cx, i32 cy)   /* circle with vertical stroke */
{
    gfx_circle_outline(cx, cy, 18, 0xFFFFFF);
    gfx_circle_outline(cx, cy, 17, 0xFFFFFF);
    gfx_rect(cx - 1, cy - 22, 3, 14, 0xFFFFFF);
    gfx_rect(cx - 1, cy - 22, 3, 14, COL_ERR);   /* tint top stroke */
}
static void glyph_restart(i32 cx, i32 cy)    /* circular arrow */
{
    gfx_circle_outline(cx, cy, 18, 0xFFFFFF);
    gfx_circle_outline(cx, cy, 17, 0xFFFFFF);
    /* arrow tip — three pixels */
    gfx_rect(cx + 14, cy - 12, 8, 2, 0xFFFFFF);
    gfx_rect(cx + 18, cy - 16, 2, 8, 0xFFFFFF);
    /* break the circle to imply rotation */
    for (i32 dy = -3; dy <= 3; dy++) gfx_pixel(cx + 18, cy + dy, 0);
}
static void glyph_sleep(i32 cx, i32 cy)      /* crescent moon */
{
    gfx_circle(cx, cy, 18, 0xFFFFFF);
    gfx_circle(cx + 8, cy - 4, 16, PAL_ACCENT);  /* mask out one side */
}
static void glyph_signout(i32 cx, i32 cy)    /* arrow exiting box */
{
    gfx_round_rect(cx - 18, cy - 14, 22, 28, 3, 0xFFFFFF);
    gfx_round_rect(cx - 14, cy - 10, 14, 20, 2, COL_OK);
    gfx_rect(cx - 2, cy - 1, 18, 3, 0xFFFFFF);
    gfx_rect(cx + 12, cy - 5, 3, 11, 0xFFFFFF);
    gfx_rect(cx + 14, cy - 3, 3,  7, 0xFFFFFF);
    gfx_rect(cx + 16, cy - 1, 3,  3, 0xFFFFFF);
}

/* ----- render ----------------------------------------------------------- */
void power_menu_render(u32 frame)
{
    if (!g_open) return;
    i32 W = (i32)FB.width, H = (i32)FB.height;

    /* Heavy dim + radial wash so the modal really pops. */
    gfx_dim(170);

    /* slide-in from below: lift modal across the first ~14 frames */
    u32 dt = g_ticks - g_open_at;
    i32 lift = (dt < 14) ? (i32)(14 - dt) * 8 : 0;

    i32 mw = 520, mh = 420;
    i32 mx = (W - mw) / 2;
    i32 my = (H - mh) / 2 + lift;

    /* Frosted modal panel */
    if (SET.aero_enabled) {
        gfx_blur_rect(mx - 20, my - 20, mw + 40, mh + 40, 7);
    }
    gfx_round_glass(mx, my, mw, mh, 22);
    gfx_round_outline(mx, my, mw, mh, 22, PAL_HAIRLINE);

    /* Header */
    {
        const char *title = T("Power options",     "Guc secenekleri");
        const char *sub   = T("Pick what to do — Esc to cancel",
                              "Yapmak istedigini sec — Esc ile iptal");
        i32 tw = gfx_text_width(title);
        i32 sw = gfx_text_width(sub);
        gfx_text(mx + (mw - tw) / 2, my + 26, title, PAL_TEXT);
        gfx_text(mx + (mw - sw) / 2, my + 48, sub,   PAL_TEXT_DIM);
        gfx_rect(mx + 24, my + 70, mw - 48, 1, PAL_HAIRLINE);
    }

    tile_t T4[4]; compute_tiles(T4);
    for (i32 i = 0; i < 4; i++) {
        bool selected = (g_cursor == i);
        u32  col      = T4[i].col;
        /* tile body */
        if (selected) {
            gfx_round_rect_a(T4[i].x - 3, T4[i].y - 3,
                             T4[i].w + 6, T4[i].h + 6, 18, col, 80);
        }
        gfx_round_rect_a(T4[i].x, T4[i].y, T4[i].w, T4[i].h, 16, col, 235);
        gfx_round_outline(T4[i].x, T4[i].y, T4[i].w, T4[i].h, 16, 0xFFFFFF);
        /* glyph centred top half */
        i32 cx = T4[i].x + T4[i].w / 2;
        i32 cy = T4[i].y + 50;
        switch (i) {
            case 0: glyph_shutdown(cx, cy); break;
            case 1: glyph_restart (cx, cy); break;
            case 2: glyph_sleep   (cx, cy); break;
            case 3: glyph_signout (cx, cy); break;
        }
        /* big label below glyph */
        i32 lw = gfx_text_width(T4[i].label);
        gfx_text(T4[i].x + (T4[i].w - lw) / 2, T4[i].y + 92, T4[i].label, 0xFFFFFF);
        i32 sw = gfx_text_width(T4[i].sub);
        gfx_text(T4[i].x + (T4[i].w - sw) / 2, T4[i].y + 116, T4[i].sub, 0xE0E5EE);
    }

    /* Footer hint */
    {
        const char *hint = T(
            "Arrow keys to choose   Enter to confirm   Esc to cancel",
            "Ok tuslari ile sec   Enter ile onayla   Esc ile iptal");
        i32 hw = gfx_text_width(hint);
        gfx_text(mx + (mw - hw) / 2, my + mh - 30, hint, PAL_TEXT_DIM);
    }

    (void)frame;
}

/* ----- input ------------------------------------------------------------- */
void power_menu_handle_key(i32 k)
{
    if (!g_open) return;
    if (k == KEY_ESC) { g_open = false; return; }
    if (k == KEY_LEFT)  { g_cursor ^= 1; return; }   /* 0<->1, 2<->3 */
    if (k == KEY_RIGHT) { g_cursor ^= 1; return; }
    if (k == KEY_UP || k == KEY_DOWN) { g_cursor ^= 2; return; }
    if (k == KEY_ENTER) { execute_action(g_cursor); return; }
}

/* Returns true when the click landed on the menu (and was consumed). */
bool power_menu_handle_mouse(i32 mx, i32 my, bool click_edge)
{
    if (!g_open) return false;
    if (!click_edge) return true;     /* swallow any clicks while open */

    tile_t T4[4]; compute_tiles(T4);
    for (i32 i = 0; i < 4; i++) {
        if (mx >= T4[i].x && mx <= T4[i].x + T4[i].w &&
            my >= T4[i].y && my <= T4[i].y + T4[i].h) {
            g_cursor = i;
            execute_action(i);
            return true;
        }
    }
    /* click outside any tile but inside the dimmed backdrop = cancel */
    g_open = false;
    return true;
}
