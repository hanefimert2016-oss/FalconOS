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

/* --- KDE/GNOME style bottom panel ---------------------------------------- */
static void draw_panel(void)
{
    i32 mx, my; bool ml; mouse_get(&mx, &my, &ml);
    bool clicked = false;
    bool rclicked = false;
    if (apps_active() < 0) {
        clicked  = mouse_consume_click();
        rclicked = mouse_consume_right();
    }
    (void)ml;

    /* Panel dimensions */
    i32 ph = 48;  /* panel height */
    i32 py = (i32)FB.height - ph;
    i32 pw = (i32)FB.width;

    /* Panel background with Aero blur */
    if (SET.aero_enabled) {
        gfx_blur_rect(0, py, pw, ph, 5);
        gfx_rect_a(0, py, pw, ph, PAL_PANEL, 150);
    } else {
        gfx_rect_a(0, py, pw, ph, PAL_PANEL, 230);
    }
    gfx_rect_a(0, py, pw, 1, PAL_HAIRLINE, 255);

    /* === Start Button (Falcon icon) === */
    i32 start_btn_x = 12;
    i32 start_btn_y = py + 8;
    i32 start_btn_size = 32;
    bool start_hover = (mx >= start_btn_x && mx <= start_btn_x + start_btn_size &&
                       my >= start_btn_y && my <= start_btn_y + start_btn_size);
    if (start_hover) {
        gfx_round_rect_a(start_btn_x - 2, start_btn_y - 2, start_btn_size + 4, start_btn_size + 4, 8, PAL_ACCENT, 60);
    }
    gfx_circle(start_btn_x + start_btn_size/2, start_btn_y + start_btn_size/2, start_btn_size/2 - 2, PAL_ACCENT);
    gfx_text(start_btn_x + 8, start_btn_y + 10, "F", 0xFFFFFF);
    if (start_hover && clicked) {
        /* Clicking start button opens Launchpad */
        launchpad_open();
    }

    /* === Taskbar (open windows) === */
    i32 taskbar_start = start_btn_x + start_btn_size + 20;
    i32 taskbar_items = 0;
    i32 taskbar_btn_w = 140;
    i32 taskbar_btn_h = 32;
    i32 taskbar_gap = 8;

    /* Show open apps in taskbar */
    for (i32 i = 0; i < apps_count() && taskbar_items < TASKBAR_MAX; i++) {
        i32 tx = taskbar_start + taskbar_items * (taskbar_btn_w + taskbar_gap);
        i32 ty = py + 8;

        bool is_active = (apps_active() == i);
        bool is_minimized = (apps_minimized() == i);
        bool task_hover = (mx >= tx && mx <= tx + taskbar_btn_w &&
                          my >= ty && my <= ty + taskbar_btn_h);

        /* Taskbar button background */
        if (is_active) {
            gfx_round_rect_a(tx, ty, taskbar_btn_w, taskbar_btn_h, 6, PAL_ACCENT, 80);
        } else if (is_minimized) {
            gfx_round_rect_a(tx, ty, taskbar_btn_w, taskbar_btn_h, 6, PAL_TEXT_DIM, 40);
        } else if (task_hover) {
            gfx_round_rect_a(tx, ty, taskbar_btn_w, taskbar_btn_h, 6, PAL_PANEL_HI, 100);
        }

        /* White line below minimized apps (like macOS) */
        if (is_minimized) {
            gfx_rect_a(tx, ty + taskbar_btn_h - 2, taskbar_btn_w, 2, 0xFFFFFF, 180);
        }

        /* App icon */
        apps_draw_icon(i, tx + 18, ty + taskbar_btn_h/2);

        /* App name */
        char name[24];
        k_strcpy(name, apps_display_name(i));
        /* Truncate if too long */
        if (k_strlen(name) > 14) {
            name[12] = '.';
            name[13] = '.';
            name[14] = '.';
            name[15] = '\0';
        }
        gfx_text(tx + 38, ty + 10, name, is_active ? PAL_TEXT : PAL_TEXT_DIM);

        /* Close button (X) */
        if (task_hover) {
            gfx_text(tx + taskbar_btn_w - 16, ty + 10, "x", is_active ? PAL_TEXT : PAL_TEXT_FAINT);
        }

        if (task_hover && clicked) {
            if (is_active) {
                apps_close();
            } else {
                apps_open(i);
            }
        }

        taskbar_items++;
    }

    /* === System Tray (right side) === */
    i32 tray_x = pw - 180;

    /* Clock */
    rtc_time_t now; rtc_local(&now);
    char time_str[32];
    char tmp[8];
    k_strcpy(time_str, "");
    k_itoa(now.hour, tmp, 10); if (now.hour < 10) k_strcat(time_str, "0"); k_strcat(time_str, tmp);
    k_strcat(time_str, ":");
    k_itoa(now.min, tmp, 10); if (now.min < 10) k_strcat(time_str, "0"); k_strcat(time_str, tmp);
    gfx_text(pw - 70, py + 16, time_str, PAL_TEXT);

    /* Date */
    k_strcpy(tmp, "");
    k_itoa(now.day, tmp, 10); if (now.day < 10) k_strcat(tmp, "0"); k_strcat(time_str, tmp);
    gfx_text(pw - 70, py + 30, tmp, PAL_TEXT_DIM);

    /* Volume icon placeholder */
    gfx_circle(pw - 160, py + 24, 6, PAL_TEXT_DIM);
    gfx_circle(pw - 164, py + 24, 3, PAL_TEXT_DIM);

    /* Network icon placeholder */
    gfx_circle(pw - 185, py + 24, 5, net_connected() ? COL_OK : PAL_TEXT_FAINT);

    /* Battery icon placeholder */
    gfx_rect(pw - 205, py + 20, 16, 10, PAL_TEXT_DIM);
    gfx_rect(pw - 205, py + 22, 12, 6, COL_OK);
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
