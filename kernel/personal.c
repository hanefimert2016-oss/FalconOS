/* =============================================================================
 *  FalconOS — Personal Kernel UI (FalconOS 1)
 * -----------------------------------------------------------------------------
 *  v6 desktop layout (KDE/GNOME hybrid bottom panel):
 *    - top: 30px frosted menu bar  (handled in main.c)
 *    - left edge: pinned desktop shortcuts (kernel/desktop_pins.c)
 *    - centre: 6-card widget grid     (kernel/widgets.c)
 *    - top corners: uptime + display cards
 *    - bottom: KDE/GNOME-style panel with app launcher, taskbar, system tray
 *
 *  The v4 hero circle has been removed at the user's request — the desktop is
 *  now "dolu dolu" (busy) by default.
 *
 *  Bottom panel: Start button + taskbar + clock + system tray
 *  F2 opens Launchpad
 * ============================================================================= */
#include "falcon.h"

static i32 dock_idx = 2;        /* keyboard hover */

/* up to this many tiles fit on the taskbar */
#define TASKBAR_MAX 12

void mode_personal_input(i32 key)
{
    if (apps_active() >= 0) { apps_input_active(key); return; }

    i32 n = apps_count();
    if (n > TASKBAR_MAX) n = TASKBAR_MAX;
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

/* --- Windows 11-style centred bottom taskbar ----------------------------
 *
 * FalconOS 1.2: re-skinned to match the modern Windows taskbar — centred
 * Start + open-app stack with a true frosted-glass strip, real tray
 * glyphs (network bars / volume waves / battery cell) and a far-right
 * "show desktop" hover zone. The legacy "x" close glyph on each tile is
 * gone in favour of a Win11-style middle-click close (or right-click
 * menu, future). Hover background uses a soft accent wash so the whole
 * panel feels alive.                                                     */
static void draw_panel(void)
{
    i32 mx, my; bool ml; mouse_get(&mx, &my, &ml);
    bool clicked = false;
    bool rclicked = false;
    if (apps_active() < 0) {
        clicked  = mouse_consume_click();
        rclicked = mouse_consume_right();
    }
    (void)ml; (void)rclicked;

    /* Panel dimensions: 52 px (Win11 default-ish, scales with text)        */
    const i32 ph = 52;
    i32 py  = (i32)FB.height - ph;
    i32 pw  = (i32)FB.width;
    i32 cy  = py + ph / 2;

    /* Panel background — frosted strip with subtle top hairline + a 1-px
     * sub-hairline 1 px below for a sense of depth.                       */
    if (SET.aero_enabled) {
        gfx_blur_rect(0, py, pw, ph, 6);
        if (SET.theme == THEME_LIQUID) {
            gfx_rect_a(0, py, pw, ph, 0xE8F6FF, 110);
        } else {
            gfx_rect_a(0, py, pw, ph, PAL_PANEL, 165);
        }
    } else {
        gfx_rect_a(0, py, pw, ph, PAL_PANEL, 232);
    }
    gfx_rect_a(0, py + 0, pw, 1, 0xFFFFFF,    65);
    gfx_rect_a(0, py + 1, pw, 1, PAL_HAIRLINE, 255);

    /* ----- compute centred run of: [Start] [App] [App] ... -------------- */
    const i32 start_w = 40;
    const i32 tile_w  = 40;            /* icon-only Win11-style tiles */
    const i32 gap     = 6;

    i32 n = apps_count();
    if (n > TASKBAR_MAX) n = TASKBAR_MAX;
    i32 run_w = start_w + (n > 0 ? gap + n * tile_w + (n - 1) * gap : 0);
    i32 run_x = (pw - run_w) / 2;
    i32 ty    = py + (ph - 36) / 2;

    /* === Start button — rounded-square Falcon glyph (Win11 vibe) ====== */
    i32 sx = run_x;
    bool start_hover = (mx >= sx && mx < sx + start_w &&
                        my >= ty && my < ty + 36);
    if (start_hover) {
        gfx_round_rect_a(sx, ty, start_w, 36, 8, PAL_ACCENT, 70);
    } else {
        gfx_round_rect_a(sx, ty, start_w, 36, 8, PAL_PANEL_HI, 60);
    }
    /* mini Falcon: stylised wing chevron + dot                            */
    {
        i32 g_cx = sx + start_w / 2;
        i32 g_cy = ty + 18;
        gfx_circle(g_cx, g_cy - 2, 9, PAL_ACCENT);
        gfx_circle(g_cx, g_cy - 2, 6, 0xFFFFFF);
        gfx_circle(g_cx, g_cy - 2, 3, PAL_ACCENT);
        /* tiny wing flick */
        gfx_rect(g_cx - 7, g_cy + 8, 14, 2, PAL_ACCENT);
        gfx_rect(g_cx - 4, g_cy + 11, 8, 2, PAL_ACCENT);
    }
    if (start_hover && clicked) launchpad_open();

    /* === Open-app tiles (icon-only, colour bar underneath when active) === */
    i32 tx = sx + start_w + gap;
    for (i32 i = 0; i < n; i++, tx += tile_w + gap) {
        bool is_active    = (apps_active()    == i);
        bool is_minimized = (apps_minimized() == i);
        bool hover        = (mx >= tx && mx < tx + tile_w &&
                             my >= ty && my < ty + 36);

        /* hover wash + active wash                                        */
        if (is_active) {
            gfx_round_rect_a(tx, ty, tile_w, 36, 8, PAL_ACCENT, 60);
        } else if (hover) {
            gfx_round_rect_a(tx, ty, tile_w, 36, 8, 0xFFFFFF, 50);
        }

        apps_draw_icon(i, tx + tile_w / 2, ty + 18);

        /* Win11 underline: thin coloured pip below tile to indicate
         * running / focused state. 16 px wide if active, 6 px if just
         * running, hidden if neither (impossible here since the loop
         * walks all running apps).                                       */
        i32 bar_w = is_active ? 18 : (is_minimized ? 6 : 10);
        i32 bar_x = tx + (tile_w - bar_w) / 2;
        u32 bar_c = is_active ? PAL_ACCENT : PAL_TEXT_DIM;
        gfx_rect_a(bar_x, ty + 36 - 2, bar_w, 2, bar_c, 220);

        if (hover && clicked) {
            if (is_active)        apps_close();
            else                  apps_open(i);
        }
    }

    /* === Right tray: network / volume / battery + clock ================ */
    i32 trx = pw - 14;          /* current right cursor                    */

    /* "Show desktop" hover zone — last 6 px of the panel. Win11 trick:
     * clicking it minimises the active app.                              */
    {
        bool sd_hover = (mx >= pw - 6 && my >= py);
        if (sd_hover) gfx_rect_a(pw - 6, py, 6, ph, PAL_ACCENT, 80);
        if (sd_hover && clicked && apps_active() >= 0) apps_close();
    }
    trx -= 4;

    /* date + time stack -------------------------------------------------- */
    {
        rtc_time_t now; rtc_local(&now);
        char time_str[16], date_str[20], tmp[8];
        k_strcpy(time_str, "");
        k_itoa(now.hour, tmp, 10); if (now.hour < 10) k_strcat(time_str, "0"); k_strcat(time_str, tmp);
        k_strcat(time_str, ":");
        k_itoa(now.min,  tmp, 10); if (now.min  < 10) k_strcat(time_str, "0"); k_strcat(time_str, tmp);

        k_strcpy(date_str, "");
        k_itoa(now.day, tmp, 10); k_strcat(date_str, tmp);
        k_strcat(date_str, " ");
        k_strcat(date_str, loc_month_short(now.month));

        i32 tw1 = gfx_text_width(time_str);
        i32 tw2 = gfx_text_width(date_str);
        i32 tw  = (tw1 > tw2 ? tw1 : tw2);
        trx -= tw;
        gfx_text(trx, py + 8,  time_str, PAL_TEXT);
        gfx_text(trx, py + 26, date_str, PAL_TEXT_DIM);
        trx -= 14;
    }

    /* battery — outline + 60% fill                                       */
    {
        i32 bx = trx - 22, by = cy - 6;
        gfx_round_outline(bx, by, 22, 12, 3, PAL_TEXT_DIM);
        gfx_rect(bx + 22, by + 4, 2, 4, PAL_TEXT_DIM);
        gfx_rect_a(bx + 2, by + 2, 12, 8, COL_OK, 220);
        trx = bx - 12;
    }

    /* network — 4 ascending bars; colour green if connected             */
    {
        u32 col = net_connected() ? COL_OK : PAL_TEXT_FAINT;
        i32 nx = trx - 22, by = cy + 8;
        for (i32 i = 0; i < 4; i++) {
            i32 bh = 3 + i * 2;
            gfx_rect_a(nx + i * 5, by - bh, 3, bh, col, 230);
        }
        trx = nx - 12;
    }

    /* volume — speaker glyph + 2 sound waves                             */
    {
        i32 vx = trx - 18, by = cy;
        gfx_rect(vx, by - 4, 4, 8, PAL_TEXT_DIM);
        gfx_rect(vx + 4, by - 6, 5, 12, PAL_TEXT_DIM);
        /* waves (simple arcs as 2 px circles on right edge)             */
        gfx_circle_outline(vx + 10, by, 4, PAL_TEXT_DIM);
        gfx_circle_outline(vx + 10, by, 7, PAL_TEXT_DIM);
        trx = vx - 14;
    }
}

/* --- legacy dock (for compatibility) ------------------------------------ */
static void draw_dock(void)
{
    /* Redirect to new panel */
    draw_panel();
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
    /* desktop chrome — order matters (back to front)
     *
     * FalconOS 1.2: clean first-boot desktop. The Big-Sur uptime + display
     * cards and the central 6-card widget grid are no longer drawn by
     * default — desktop = wallpaper + the two panels only, like Windows /
     * GNOME / Plasma. Widgets can be re-enabled from Settings (which sets
     * SET.widgets_shown=true and persists). The legacy "App minimized"
     * floating hint and the keyboard cheat-sheet at the bottom edge were
     * also removed: the same info is in the Help drawer (?) glyph.       */
    if (SET.widgets_shown) {
        draw_uptime_card();
        draw_res_card();
        widgets_render(frame);
    }
    desktop_pins_render(frame);
    draw_welcome_banner();
    draw_dock();

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
