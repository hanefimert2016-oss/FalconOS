/* =============================================================================
 *  FalconOS 1 — sliding Help panel
 * -----------------------------------------------------------------------------
 *  A right-edge "drawer" that auto-opens the very first time the user
 *  reaches the desktop and is reopenable any time afterwards via the
 *  small "?" glyph in the top menu bar.  Replaces v5.2's modal welcome
 *  banner — instead of a centred pop-up the cheat-sheet lives off to
 *  the side and can stay open while you try things.
 *
 *  Animation: an integer slide counter ramps 0..HELP_ANIM ticks; the
 *  panel's left edge is computed as
 *       x0 = FB.width  -  PANEL_W * t
 *  where t = anim/HELP_ANIM, so it tweens in over ~300 ms.  Closing
 *  ramps the counter back to 0.
 *
 *  Content is purely informational — no widget state, no per-frame work
 *  beyond rendering.  All five UI languages share the same layout via
 *  the TX(...) macro from kernel/locale.c.
 * ============================================================================= */
#include "falcon.h"

#define PANEL_W       420
#define HELP_ANIM      18           /* ~180 ms at the 100 Hz PIT */

static bool help_open = false;
static i32  help_anim = 0;          /* 0 closed .. HELP_ANIM fully open */

void helppanel_open(void)  { help_open = true;  }
void helppanel_close(void) { help_open = false; }
bool helppanel_is_open(void) { return help_open || help_anim > 0; }

/* "?" glyph hit-test — small disk just left of the power glyph in the
 * menu bar.  Re-derives the same x as the menu-bar render path so the
 * two stay in sync.                                                   */
bool menu_bar_help_hit(i32 mx, i32 my)
{
    if (my < 0 || my > 30) return false;
    /* The "?" glyph sits 30 px left of the power glyph at px = W-cw-88.
     * We don't have access to that x here, so we approximate: any click
     * in the right-most ~250 px strip that ISN'T on the power glyph or
     * the clock string and IS in the menu-bar y-range is treated as
     * "user wants help".  In practice we want a precise glyph, so the
     * caller (main.c) re-derives the same x and passes it in.  This
     * fallback keeps the prototype cheap.
     *
     * The real hit-test happens in main.c next to menu_bar_power_hit
     * because both depend on the run-time clock width.                */
    (void)mx;
    return false;
}

static void draw_section(i32 x, i32 y, const char *title,
                         const char *l1, const char *l2,
                         const char *l3, const char *l4)
{
    gfx_text(x, y, title, PAL_ACCENT);
    if (l1) gfx_text(x, y + 22, l1, PAL_TEXT);
    if (l2) gfx_text(x, y + 40, l2, PAL_TEXT);
    if (l3) gfx_text(x, y + 58, l3, PAL_TEXT);
    if (l4) gfx_text(x, y + 76, l4, PAL_TEXT);
}

void helppanel_render(u32 frame)
{
    (void)frame;

    /* tween the slide counter */
    if (help_open) {
        if (help_anim < HELP_ANIM) help_anim++;
    } else {
        if (help_anim > 0) help_anim--;
    }
    if (help_anim == 0) return;

    i32 W = (i32)FB.width;
    i32 H = (i32)FB.height;

    /* eased open: cubic in/out keeps the motion soft instead of linear  */
    i32 t = help_anim;
    i32 e = (t * t * (3 * HELP_ANIM - 2 * t)) / (HELP_ANIM * HELP_ANIM);
    i32 panel_x = W - (PANEL_W * e) / HELP_ANIM;

    /* dim the rest of the desktop a touch so attention falls on the panel */
    i32 dim_alpha = (60 * e) / HELP_ANIM;
    if (dim_alpha > 0) gfx_rect_a(0, 30, W, H - 30, COL_SHADOW, (u8)dim_alpha);

    /* drop shadow for the panel edge */
    gfx_rect_a(panel_x - 12, 30, 12, H - 30, COL_SHADOW, 70);

    /* glass body — Aero-aware */
    if (SET.aero_enabled) {
        gfx_blur_rect(panel_x, 30, PANEL_W, H - 30, 9);
        gfx_rect_a(panel_x, 30, PANEL_W, H - 30, PAL_PANEL, 175);
    } else {
        gfx_rect_a(panel_x, 30, PANEL_W, H - 30, PAL_PANEL, 235);
    }
    /* hairline left edge */
    gfx_rect_a(panel_x, 30, 1, H - 30, PAL_HAIRLINE, 255);

    /* header */
    i32 cx = panel_x + PANEL_W / 2;
    gfx_text_lg(cx - 110, 60, "FalconOS 1", PAL_TEXT);
    gfx_text(panel_x + 28, 110,
             T("Quick start guide", "Hizli baslangic rehberi"),
             PAL_TEXT_DIM);

    /* sections */
    i32 x = panel_x + 28;
    i32 y = 150;

    draw_section(x, y,
        T("Keyboard",  "Klavye"),
        T("F1   toggle Personal / Developer kernel",
          "F1   Personal / Developer cekirdek"),
        T("F2   open the Launchpad (all apps)",
          "F2   Launchpad'i ac (tum uygulamalar)"),
        T("F12  open the Power menu",
          "F12  Guc menusunu ac"),
        T("Esc  close the active app or dialog",
          "Esc  acik uygulamayi / dialogu kapat"));

    y += 116;
    draw_section(x, y,
        T("Mouse",  "Fare"),
        T("Left click   open / select",
          "Sol tik      ac / sec"),
        T("Right click  pin to desktop",
          "Sag tik      masaustune sabitle"),
        T("Double click open quickly",
          "Cift tik     hizli ac"),
        NULL);

    y += 100;
    draw_section(x, y,
        T("Windows",  "Pencere"),
        T("Drag the title bar    move",
          "Basliktan suruke      tasi"),
        T("Drag the bottom-right resize",
          "Sag-alttan suruke     boyutlandir"),
        T("Traffic lights        close / min / max",
          "Trafik isiklari       kapat / kucult / buyut"),
        NULL);

    y += 100;
    draw_section(x, y,
        T("Settings",  "Ayarlar"),
        T("Theme, language, resolution, password",
          "Tema, dil, cozunurluk, parola"),
        T("Open from the dock or via Launchpad",
          "Dock veya Launchpad'den acabilirsin"),
        NULL, NULL);

    /* close pill at the bottom */
    const char *ok = T("Got it",  "Anladim");
    i32 ow = gfx_text_width(ok) + 36;
    i32 ox = panel_x + (PANEL_W - ow) / 2;
    i32 oy = H - 60;
    gfx_round_rect_a(ox, oy, ow, 30, 15, PAL_ACCENT, 255);
    gfx_text_centered(panel_x + PANEL_W / 2, oy + 6, ok, 0xFFFFFF);

    /* footer hint */
    gfx_text_centered(panel_x + PANEL_W / 2, H - 22,
        T("Esc closes — find me again at the ? in the top bar",
          "Esc ile kapat — tekrar ust cubuktaki ? den ac"),
        PAL_TEXT_FAINT);
}

bool helppanel_handle_key(i32 key)
{
    if (!help_open && help_anim == 0) return false;
    if (key == KEY_ESC || key == KEY_ENTER || key == ' ') {
        help_open = false;
        SET.help_seen = true;
        diskdb_save();
        return true;
    }
    /* swallow everything else so the dock/widgets don't react while the
     * help drawer is up                                                */
    return true;
}

bool helppanel_handle_mouse(i32 mx, i32 my, bool click)
{
    if (!help_open && help_anim == 0) return false;
    if (!click) return help_open;     /* still consume hover state    */

    i32 W = (i32)FB.width;
    i32 H = (i32)FB.height;
    i32 panel_x = W - PANEL_W;

    /* close pill */
    const char *ok = T("Got it", "Anladim");
    i32 ow = gfx_text_width(ok) + 36;
    i32 ox = panel_x + (PANEL_W - ow) / 2;
    i32 oy = H - 60;
    if (mx >= ox && mx <= ox + ow && my >= oy && my <= oy + 30) {
        help_open = false;
        SET.help_seen = true;
        diskdb_save();
        return true;
    }

    /* click outside the panel area dismisses without "marking seen" so
     * the user can flip back to the desktop momentarily without losing
     * the auto-open state on a fresh install                          */
    if (mx < panel_x) {
        help_open = false;
        return true;
    }
    return true;     /* click inside the panel does nothing else        */
}
