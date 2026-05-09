/* =============================================================================
 *  FalconOS — Personal Kernel UI (FalconOS 1)
 * -----------------------------------------------------------------------------
 *  v5 desktop layout (tema-aware Lumen *and* Nox):
 *    - top: 30px frosted menu bar  (handled in main.c)
 *    - left edge: pinned desktop shortcuts (kernel/desktop_pins.c)
 *    - centre: 6-card widget grid     (kernel/widgets.c)
 *    - top corners: uptime + display cards
 *    - bottom: macOS-style dock
 *
 *  The v4 hero circle has been removed at the user's request — the desktop is
 *  now "dolu dolu" (busy) by default.
 *
 *  Mouse-driven dock; arrow keys + Enter as keyboard fallback; F2 opens
 *  Launchpad; right-click on any dock tile pins it to the desktop.
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

/* --- top-left uptime card ------------------------------------------------- */
static void draw_uptime_card(void)
{
    i32 x = 24, y = 50, w = 240, h = 84;
    gfx_round_glass(x, y, w, h, 16);
    gfx_text(x + 18, y + 14, T("Up-time", "Çalışma"), PAL_TEXT_DIM);

    u32 H, M, S; pit_uptime(&H, &M, &S);
    char buf[32], tmp[8];
    k_strcpy(buf, "");
    k_itoa(H, tmp, 10); if (H < 10) k_strcat(buf, "0");
    k_strcat(buf, tmp); k_strcat(buf, ":");
    k_itoa(M, tmp, 10); if (M < 10) k_strcat(buf, "0");
    k_strcat(buf, tmp); k_strcat(buf, ":");
    k_itoa(S, tmp, 10); if (S < 10) k_strcat(buf, "0");
    k_strcat(buf, tmp);
    gfx_text(x + 18, y + 38, buf, PAL_TEXT);

    gfx_circle(x + w - 22, y + 22, 5, COL_OK);
    gfx_text(x + w - 92, y + 38,
             T("online", "çalışıyor"), COL_OK);
    gfx_text(x + 18, y + 62,
             T("all systems nominal",
               "tüm sistemler normal"), PAL_TEXT_FAINT);
}

/* --- top-right display card ----------------------------------------------- */
static void draw_res_card(void)
{
    i32 w = 240, h = 84;
    i32 x = (i32)FB.width - w - 24, y = 50;
    gfx_round_glass(x, y, w, h, 16);
    gfx_text(x + 18, y + 14, T("Display", "Ekran"), PAL_TEXT_DIM);

    char buf[32], tmp[8];
    k_strcpy(buf, "");
    if (SET.viewport_w && SET.viewport_h) {
        k_itoa(SET.viewport_w, tmp, 10); k_strcat(buf, tmp);
        k_strcat(buf, " x ");
        k_itoa(SET.viewport_h, tmp, 10); k_strcat(buf, tmp);
    } else {
        k_itoa(FB.width, tmp, 10);  k_strcat(buf, tmp);
        k_strcat(buf, " x ");
        k_itoa(FB.height, tmp, 10); k_strcat(buf, tmp);
    }
    gfx_text(x + 18, y + 38, buf, PAL_TEXT);

    gfx_text(x + 18, y + 62,
             SET.theme == THEME_DARK ? "Nox  -  x86_64"
                                     : "Lumen  -  x86_64",
             PAL_TEXT_FAINT);

    gfx_circle(x + w - 22, y + 22, 5, PAL_ACCENT);
}

/* --- bottom dock ---------------------------------------------------------- */
static void draw_dock(void)
{
    i32 mx, my; bool ml; mouse_get(&mx, &my, &ml);
    bool clicked = false;
    bool rclicked = false;
    /* When an app window is open, keep the dock visually alive but do not
     * consume click edges here; the active window / app gets first shot. */
    if (apps_active() < 0) {
        clicked  = mouse_consume_click();
        rclicked = mouse_consume_right();
    }
    (void)ml;

    i32 n = apps_count();
    if (n > DOCK_MAX) n = DOCK_MAX;

    /* dock tile size scales with SET.dock_size */
    i32 tile = 50 + SET.dock_size * 9;       /* 50 .. 86 */
    i32 gap  = 14 + SET.dock_size * 2;
    i32 dw   = n * tile + (n - 1) * gap + 36;
    i32 dh   = tile + 32;
    i32 dx   = ((i32)FB.width - dw) / 2;
    i32 dy   = (i32)FB.height - dh - 22;

    /* glass tray — Aero blur underneath when enabled, flat overlay when
     * the user has turned Aero off in Settings.                         */
    if (SET.aero_enabled) {
        gfx_round_rect_a(dx + 2, dy + 8, dw, dh, 26, COL_SHADOW, 60);
        if (SET.theme == THEME_LIQUID) {
            gfx_blur_rect(dx, dy, dw, dh, 6);
            gfx_round_rect_a(dx, dy, dw, dh, 26, 0xEAF7FF, 110);
            gfx_round_rect_a(dx + 6, dy + 2, dw - 12, 6, 4, 0xFFFFFF, 120);
        } else {
            gfx_blur_rect(dx, dy, dw, dh, 7);
            gfx_round_rect_a(dx, dy, dw, dh, 26, PAL_PANEL, 140);
        }
        gfx_round_outline(dx, dy, dw, dh, 26, PAL_HAIRLINE);
    } else {
        gfx_round_rect_a(dx + 2, dy + 8, dw, dh, 26, COL_SHADOW, 60);
        gfx_round_rect_a(dx, dy, dw, dh, 26, PAL_PANEL, 220);
        gfx_round_outline(dx, dy, dw, dh, 26, PAL_HAIRLINE);
    }

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
        /* pinned indicator dot */
        if (desktop_pin_is_pinned(i)) {
            gfx_circle(ix2 + rad - 4, iy2 - rad + 4, 4, COL_OK);
        }
        /* yellow "minimized" marker for the app hidden by the traffic light */
        if (apps_minimized() == i) {
            gfx_round_rect(ix2 - 9, iy2 + rad + 6, 18, 4, 2, COL_WARN);
        }
        /* highlight ring + label on hovered tile */
        if (hov) {
            gfx_circle_outline(ix2, iy2, rad + 4, PAL_ACCENT);
            char label[40];
            k_strcpy(label, apps_display_name(i));
            if (apps_minimized() == i) k_strcat(label, T(" (min)", " (küçük)"));
            i32 lw = gfx_text_width(label) + 16;
            i32 lx = ix2 - lw / 2;
            i32 ly = iy2 + rad + 6;
            gfx_round_rect_a(lx, ly, lw, 18, 9, PAL_PANEL, 240);
            gfx_round_outline(lx, ly, lw, 18, 9, PAL_HAIRLINE);
            gfx_text_centered(ix2, ly + 2, label, PAL_TEXT);
        }

        if (hov_m && clicked)  apps_open(i);
        if (hov_m && rclicked) desktop_pin_toggle(i);
    }

    /* "more" indicator if there are more apps in the launchpad */
    if (apps_count() > DOCK_MAX) {
        i32 mx2 = dx + dw - 14;
        gfx_circle(mx2,     dy + dh - 8, 2, PAL_TEXT_FAINT);
        gfx_circle(mx2 - 6, dy + dh - 8, 2, PAL_TEXT_FAINT);
        gfx_circle(mx2 - 12,dy + dh - 8, 2, PAL_TEXT_FAINT);
    }
}

/* --- first-run welcome banner -------------------------------------------- */
/*  Shown once on the desktop after a fresh install completes so a "normal
 *  user" sees a friendly greeting + the three keyboard shortcuts that get
 *  them productive immediately. Dismissed by clicking the OK pill or
 *  pressing Esc — both clear SET.welcome_shown and persist to disk so it
 *  never reappears.                                                       */
static void draw_welcome_banner(void)
{
    /* Superseded by the sliding Help drawer (kernel/help.c) — that one
     * auto-opens on the first desktop session and is reopenable any
     * time from the menu-bar "?" glyph, so this central modal is no
     * longer drawn even when SET.welcome_shown is false.            */
    return;
    if (SET.welcome_shown) return;
    if (!SET.installed)    return;   /* installer is still active           */

    i32 W = (i32)FB.width;
    i32 w = 560, h = 180;
    i32 x = (W - w) / 2;
    i32 y = 60;

    gfx_round_rect_a(x + 4, y + 12, w, h, 18, COL_SHADOW, 70);
    gfx_round_glass(x, y, w, h, 18);                       /* Aero-aware */
    gfx_round_outline(x, y, w, h, 18, PAL_ACCENT);

    /* personalised greeting in the active language */
    char greet[64];
    k_strcpy(greet,
             TX("Welcome",  "Hoşgeldin",  "Willkommen",  "Bienvenue",  "Bienvenido"));
    if (SET.user_count > 0 &&
        SET.active_user >= 0 && SET.active_user < FALCON_MAX_USERS &&
        SET.users[SET.active_user].in_use) {
        k_strcat(greet, ", ");
        k_strcat(greet, SET.users[SET.active_user].name);
    }
    k_strcat(greet, "!");
    gfx_text_centered(x + w / 2, y + 18, greet, PAL_TEXT);

    /* three actionable hints — same order in every language */
    gfx_text_centered(x + w / 2, y + 56,
        TX("F2 opens the Launchpad with all your apps",
           "F2 tüm uygulamalari Launchpad'de acar",
           "F2 oeffnet das Launchpad mit allen Apps",
           "F2 ouvre le Launchpad avec toutes vos apps",
           "F2 abre el Launchpad con todas tus apps"), PAL_TEXT_DIM);

    gfx_text_centered(x + w / 2, y + 78,
        TX("F1 toggles to the Developer kernel",
           "F1 ile Developer çekirdeğine geçersin",
           "F1 schaltet zum Developer-Kernel",
           "F1 bascule sur le noyau Developer",
           "F1 cambia al kernel Developer"), PAL_TEXT_DIM);

    gfx_text_centered(x + w / 2, y + 100,
        TX("Esc closes any open app",
           "Esc açık uygulamayı kapatır",
           "Esc schliesst geoeffnete Apps",
           "Esc ferme l'app ouverte",
           "Esc cierra cualquier app abierta"), PAL_TEXT_DIM);

    /* dismiss pill */
    const char *ok = TX("OK, got it", "Anladım", "Verstanden", "C'est compris", "Entendido");
    i32 ow = gfx_text_width(ok) + 32;
    i32 ox = x + (w - ow) / 2;
    i32 oy = y + h - 38;
    gfx_round_rect_a(ox, oy, ow, 26, 13, PAL_ACCENT, 255);
    gfx_text_centered(x + w / 2, oy + 4, ok, 0xFFFFFF);

    /* mouse / keyboard dismiss */
    i32 mx, my; bool ml; mouse_get(&mx, &my, &ml); (void)ml;
    bool clicked = (mx >= ox && mx <= ox + ow &&
                    my >= oy && my <= oy + 26 &&
                    mouse_consume_click());
    if (clicked) {
        SET.welcome_shown = true;
        diskdb_save();
    }
}

/* --- entry point --------------------------------------------------------- */
void mode_personal_render(u32 frame)
{
    /* desktop chrome — order matters (back to front)                       */
    draw_uptime_card();
    draw_res_card();
    widgets_render(frame);
    desktop_pins_render(frame);
    draw_welcome_banner();
    draw_dock();
    if (apps_active() < 0 && apps_minimized() >= 0) {
        char hint[96];
        k_strcpy(hint, T("App minimized: ", "Uygulama küçültüldü: "));
        k_strcat(hint, apps_display_name(apps_minimized()));
        k_strcat(hint, T(" — click its dock icon to restore.",
                         " — geri getirmek için dock simgesine tıkla."));
        gfx_text_centered((i32)FB.width / 2, (i32)FB.height - 44, hint, COL_WARN);
    }

    /* nav hint */
    gfx_text_centered((i32)FB.width / 2,
                      (i32)FB.height - 18,
                      T("<- ->  navigate    Enter open    F2 Launchpad    right-click pins to desktop",
                        "<- ->  gez    Enter aç    F2 Launchpad    sağ-tık masaüstüne sabitle"),
                      PAL_TEXT_DIM);

    /* desktop-pin clicks fire BEFORE active app render so a click on a pin
     * launches an app immediately.  When an app IS active, hand the
     * mouse stream to the window manager (drag / resize / traffic
     * lights). It returns true when it consumed the click so the
     * underlying app sees nothing.                                       */
    {
        i32 mx, my; bool ml; mouse_get(&mx, &my, &ml);
        
        if (apps_active() >= 0) {
            bool edge = mouse_peek_click();
            bool wm_used = apps_wm_handle_mouse(mx, my, ml, edge);
            if (edge && wm_used) (void)mouse_consume_click();
        } else if (mouse_consume_click()) {
            desktop_pins_input_click(mx, my);
        }
    }

    /* active app window (drawn over everything except menu bar / cursor) */
    apps_render_active(frame);

    /* Any click not used by WM/app this frame must be cleared so it does not
     * stick as a permanent edge into subsequent frames. */
    if (apps_active() >= 0) {
        if (mouse_peek_click()) (void)mouse_consume_click();
        (void)mouse_consume_right();
    }
}
