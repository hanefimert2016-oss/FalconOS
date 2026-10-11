/* =============================================================================
 *  FalconOS — lock screen with multi-user picker  (v5+)
 * -----------------------------------------------------------------------------
 *  Shown immediately after the installer wizard completes (and any time the
 *  user explicitly relocks via the Settings app).  The screen always opens
 *  focused on SET.default_user (the first user created), satisfying:
 *
 *      "her pc açılınca ilk açılan kullanıcı default ana olarak açılsın"
 *
 *  Behaviour:
 *      ←/→            switch which user we're trying to log in as
 *      type chars     accumulate password input (masked)
 *      Backspace      delete last char
 *      Enter          users_verify() on the selected user (PBKDF2 compare)
 *      Wrong password →  shake animation + "Wrong password" hint
 *      Correct      →  unlock; remember active_user; hand off to desktop
 *
 *  Empty-password users (no_password=1) unlock on any Enter without typing.
 *  F1/F2 are intentionally swallowed while locked.
 * ============================================================================= */
#include "falcon.h"

static char g_input[24];
static i32  g_input_len = 0;
static bool g_unlocked  = false;
static u32  g_shake_until = 0;
static bool g_show_error  = false;
static i32  g_user_cursor = -1;        /* index into SET.users[]            */

/* ---- brute-force throttle -------------------------------------------------
 *  Three consecutive wrong passwords on the same boot lock new attempts
 *  out for THROTTLE_TICKS PIT ticks (~5 s at 100 Hz).  The counter is
 *  per-process (not persisted) — a power cycle is enough to retry, but
 *  that's already a real-world attacker friction.                          */
#define MAX_FAILS_BEFORE_THROTTLE  3
#define THROTTLE_TICKS             500     /* 100 Hz × 5 s                  */
static u32 g_fail_count   = 0;
static u32 g_throttle_end = 0;             /* g_tick value when lifted      */

static void buf_pop_utf8(char *buf, i32 *len)
{
    if (!buf || !len || *len <= 0) return;
    i32 i = *len - 1;
    while (i > 0 && (((u8)buf[i] & 0xC0u) == 0x80u)) i--;
    buf[i] = 0;
    *len = i;
}

static bool buf_append_key(char *buf, i32 *len, i32 cap, i32 key)
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

bool lockscreen_is_unlocked(void) { return g_unlocked; }
/* Auto-login is allowed ONLY for an explicitly passwordless default user.
 * It runs once during cold-boot handoff; manual Lock never silently unlocks.
 * All password-protected accounts still require interactive PBKDF2 checking. */
void lockscreen_boot_autologin(void){
    if(SET.default_user<0 || SET.default_user>=FALCON_MAX_USERS)return;
    const falcon_user_t *u=&SET.users[SET.default_user];
    if(!u->in_use || !u->no_password)return;
    if(!users_verify(SET.default_user,""))return;
    SET.active_user=SET.default_user;
    g_user_cursor=SET.default_user;
    g_unlocked=true;
    g_input_len=0;
    k_explicit_bzero(g_input,sizeof g_input);
#ifdef FALCON_QEMU_UI_GALLERY
    outb(0xE9,'a');outb(0xE9,'U');
#endif
}


static void ensure_cursor_valid(void)
{
    if (g_user_cursor >= 0 &&
        g_user_cursor < FALCON_MAX_USERS &&
        SET.users[g_user_cursor].in_use) return;

    /* default to SET.default_user, falling back to first live slot.        */
    if (SET.default_user >= 0 && SET.default_user < FALCON_MAX_USERS &&
        SET.users[SET.default_user].in_use) {
        g_user_cursor = SET.default_user;
        return;
    }
    for (i32 i = 0; i < FALCON_MAX_USERS; i++) {
        if (SET.users[i].in_use) { g_user_cursor = i; return; }
    }
    g_user_cursor = 0;
}

static i32 step_user_cursor(i32 dir)
{
    if (SET.user_count == 0) return 0;
    i32 idx = g_user_cursor;
    for (i32 t = 0; t < FALCON_MAX_USERS; t++) {
        idx += dir;
        if (idx < 0)                idx = FALCON_MAX_USERS - 1;
        if (idx >= FALCON_MAX_USERS) idx = 0;
        if (SET.users[idx].in_use)   { g_user_cursor = idx; return idx; }
    }
    return g_user_cursor;
}

void lockscreen_lock(void)
{
    g_unlocked    = false;
    g_input[0]    = 0;
    g_input_len   = 0;
    g_show_error  = false;
    g_shake_until = 0;
    g_user_cursor = -1;
    ensure_cursor_valid();
}

/* --------------------------------------------------------------------------- */
static u32 accent_for(accent_t a)
{
    static const u32 ACC[ACC_COUNT] = {
        0x3070FF, 0xA45EE5, 0x16B5A8, 0xE85D9C, 0x6E7884
    };
    if ((i32)a < 0 || (i32)a >= ACC_COUNT) return 0x3070FF;
    return ACC[a];
}

static void draw_user_strip(i32 cx, i32 cy, i32 selected)
{
    /* Lay out up to FALCON_MAX_USERS avatars centred horizontally; the
     * selected one is larger and brighter.                                 */
    i32 avatars = SET.user_count;
    if (avatars <= 0) avatars = 1;

    i32 small_r = 22, big_r = 36;
    i32 spacing = 80;
    i32 row_w   = (avatars - 1) * spacing;
    i32 x0      = cx - row_w / 2;

    i32 placed = 0;
    for (i32 i = 0; i < FALCON_MAX_USERS; i++) {
        if (!SET.users[i].in_use) continue;
        i32 ax = x0 + placed * spacing;
        bool active = (i == selected);
        i32 r = active ? big_r : small_r;
        u32 col = accent_for(SET.users[i].accent);
        if (active) {
            gfx_circle_a(ax, cy, r + 8, col, 60);
            gfx_circle  (ax, cy, r,     col);
        } else {
            gfx_circle_a(ax, cy, r,     col, 200);
        }
        char one[2] = {SET.users[i].name[0] ? SET.users[i].name[0] : '?', 0};
        if (one[0] >= 'a' && one[0] <= 'z') one[0] = (char)(one[0] - 32);
        gfx_text_centered(ax, cy - (active ? 6 : 4), one, 0xFFFFFF);
        if (SET.users[i].is_default) {
            /* tiny "default" pip */
            gfx_circle(ax + r - 2, cy + r - 2, 5, COL_OK);
        }
        placed++;
    }
}

void lockscreen_render(u32 frame)
{
    static bool first_rendered = false;
    if (!first_rendered) {
        outb(0xE9, 'W'); /* QEMU: lock screen reached */
        first_rendered = true;
    }
    ensure_cursor_valid();

    /* Same user-selected native wallpaper as the desktop. Frosted lock UI
     * uses native framebuffer geometry, not pixel-stretched screenshots. */
    gfx_wallpaper();
    i32 W=(i32)FB.width,H=(i32)FB.height;
    i32 cx=W/2,cy=H/2;
    bool dark=SET.wallpaper_style==1;
    u32 title_ink=dark?0xF7FAFFu:0x163151u;
    u32 hint_ink=dark?0xD2E2F7u:0x385779u;
    gfx_round_rect_a(0,0,W,H,0,0x071631u,dark?80:26);
    /* Spacious 900x475 composited sign-in sheet; 2K/HD native pixels. */
    gfx_round_rect_a(cx-454,cy-158,908,483,36,0x0A2248u,32);
    gfx_round_rect_a(cx-450,cy-164,900,475,34,0xFFFFFFu,241);
    gfx_round_outline(cx-450,cy-164,900,475,34,0xCBDCF1u);
    gfx_round_rect_a(cx-83,cy-372,166,38,17,0x102B52u,225);
    gfx_text_centered(cx,cy-360,"FALCON  /  OS",0xFFFFFFu);
    gfx_text_centered(cx,cy-136,"PRIVATE WORKSPACE",0x2C4F7Fu);
    gfx_text_centered(cx,cy+252,"Protected locally by FalconOS",0x617C9Cu);

    /* ---- live clock + "welcome back" line ------------------------------- */
    {
        rtc_time_t now; rtc_local(&now);
        char clk[16]; char tmp[8];
        k_strcpy(clk, "");
        k_itoa(now.hour, tmp, 10);
        if (now.hour < 10) k_strcat(clk, "0");
        k_strcat(clk, tmp);
        k_strcat(clk, ":");
        k_itoa(now.min, tmp, 10);
        if (now.min < 10) k_strcat(clk, "0");
        k_strcat(clk, tmp);
        /* Big crisp clock + small helper text; gives the lockscreen a
         * proper macOS-style headline rather than a tiny terminal time. */
        gfx_text_hero_centered(cx, cy - 323, clk, title_ink);
        /* date below the clock, locale-formatted */
        char date_buf[16];
        loc_format_date(date_buf, &now);
        gfx_text_centered(cx, cy - 249, date_buf, hint_ink);
        gfx_text_centered   (cx, cy - 224,
                            TX("Welcome back",
                               "Tekrar hoşgeldin",
                               "Willkommen zuruck",
                               "Bon retour",
                               "Bienvenido de nuevo"), PAL_TEXT_DIM);
    }

    /* ---- user picker ---------------------------------------------------- */
    draw_user_strip(cx, cy - 31, g_user_cursor);

    /* ---- selected user name --------------------------------------------- */
    const char *uname = "(no users)";
    bool no_pwd = true;
    if (SET.user_count > 0 &&
        g_user_cursor >= 0 && g_user_cursor < FALCON_MAX_USERS &&
        SET.users[g_user_cursor].in_use) {
        uname  = SET.users[g_user_cursor].name;
        no_pwd = SET.users[g_user_cursor].no_password;
    }
    gfx_text_medium(cx-(k_strlen(uname)*6), cy + 35, uname, 0x1F3554u);
    if (SET.user_count > 1) {
        gfx_text_centered(cx, cy + 63,
            T("<-  ->   switch user",
              "<-  ->   kullanıcı değiştir"), PAL_TEXT_FAINT);
    }

    /* ---- password pill -------------------------------------------------- */
    i32 fw = 452, fh = 56;
    i32 fx = cx - fw / 2, fy = cy + 92;
    if (frame < g_shake_until) {
        i32 phase = (i32)(frame * 11) & 31;
        i32 dx = ((phase < 8) ? phase : (16 - phase)) - 4;
        fx += dx;
    }
    /* Aero blur the wallpaper behind the password pill so the field
     * lifts off the desktop. Flat overlay if the user disabled Aero. */
    if (SET.aero_enabled) {
        gfx_blur_rect(fx, fy, fw, fh, 6);
        gfx_round_rect_a(fx, fy, fw, fh, 14, PAL_PANEL, 170);
    } else {
        gfx_round_rect_a(fx, fy, fw, fh, 14, PAL_PANEL, 240);
    }
    gfx_round_outline(fx, fy, fw, fh, 14, PAL_HAIRLINE);

    char masked[24];
    for (i32 i = 0; i < g_input_len; i++) masked[i] = '*';
    masked[g_input_len] = 0;
    if (g_input_len == 0) {
        gfx_text_medium(fx + 20, fy + 17,
                 no_pwd?T("Continue", "Devam et"):
                        T("Enter password","Parola gir"), 0x607B9Du);
    } else {
        gfx_text_medium(fx + 20, fy + 17, masked, 0x243955u);
    }
    if ((frame / 30) & 1) {
        i32 caret_x = fx + 20 + (g_input_len ? g_input_len*12 : 0);
        gfx_rect(caret_x, fy + 16, 2, 25, PAL_ACCENT);
    }

    /* A real clickable confirm affordance; clicks are PS/2 events and
     * password-protected accounts still use users_verify() on Enter. */
    gfx_round_rect(fx+fw-52,fy+7,44,42,13,0x327CFAu);
    gfx_text_centered(fx+fw-30,fy+18,">",0xFFFFFFu);
    if(mouse_peek_click()){
        i32 mx,my;bool held;
        mouse_get(&mx,&my,&held);(void)held;
        if(mx>=fx+fw-60 && mx<fx+fw &&
           my>=fy && my<fy+fh){
            (void)mouse_consume_click();
            lockscreen_input(KEY_ENTER);
        }
    }
    /* ---- error / hint --------------------------------------------------- */
    extern volatile u32 g_tick;
    if (g_throttle_end > g_tick) {
        u32 secs = (g_throttle_end - g_tick + 99) / 100;
        char msg[64];
        char tmp[8];
        k_strcpy(msg, TX("Locked - wait ",
                         "Kilitli - bekle ",
                         "Gesperrt - warte ",
                         "Verrouille - attendre ",
                         "Bloqueado - espera "));
        k_itoa(secs, tmp, 10);
        k_strcat(msg, tmp);
        k_strcat(msg, "s");
        gfx_text_centered(cx, fy + fh + 12, msg, COL_ERR);
    } else if (g_show_error) {
        gfx_text_centered(cx, fy + fh + 12,
                          TX("Wrong password - try again",
                             "Parola yanlis - tekrar dene",
                             "Falsches Passwort - erneut versuchen",
                             "Mauvais mot de passe - reessayer",
                             "Contrasena incorrecta - intentar de nuevo"),
                          COL_ERR);
    } else if (no_pwd) {
        gfx_text_centered(cx, fy + fh + 12,
                          TX("No password set - press Enter",
                             "Parola yok - Enter ile devam",
                             "Kein Passwort - Eingabe drucken",
                             "Pas de mot de passe - appuyez sur Entree",
                             "Sin contrasena - pulsa Intro"),
                          PAL_TEXT_DIM);
    } else {
        gfx_text_centered(cx, fy + fh + 12,
                          TX("Press Enter to unlock",
                             "Enter ile kilidi aç",
                             "Eingabe zum Entsperren",
                             "Entree pour deverrouiller",
                             "Intro para desbloquear"),
                          PAL_TEXT_DIM);
    }

    /* ---- footer hint ---------------------------------------------------- */
    gfx_text_centered(cx, H - 26,
        "FalconOS 1  -  bare-metal x86_64", PAL_TEXT_FAINT);
}

/* --------------------------------------------------------------------------- */
void lockscreen_input(i32 key)
{
    if (g_unlocked) return;
    extern volatile u32 g_tick;
    ensure_cursor_valid();

    if (key == KEY_LEFT)  { step_user_cursor(-1); g_input_len = 0; g_input[0] = 0; g_show_error = false; return; }
    if (key == KEY_RIGHT) { step_user_cursor(+1); g_input_len = 0; g_input[0] = 0; g_show_error = false; return; }

    if (key == KEY_BACKSPACE) {
        buf_pop_utf8(g_input, &g_input_len);
        g_show_error = false;
        return;
    }
    /* While the throttle is active, swallow every keystroke other than
     * the user-cursor moves (those reset the typed buffer above).      */
    if (g_throttle_end > g_tick) return;

    if (key == KEY_ENTER) {
        g_input[g_input_len] = 0;
        bool ok = users_verify(g_user_cursor, g_input);
        if (ok) {
            SET.active_user = g_user_cursor;
            g_unlocked      = true;
            g_fail_count    = 0;
        } else {
            g_show_error  = true;
            g_shake_until = g_tick + 30;
            g_fail_count++;
            if (g_fail_count >= MAX_FAILS_BEFORE_THROTTLE) {
                g_throttle_end = g_tick + THROTTLE_TICKS;
                g_fail_count   = 0;
            }
        }
        /* Always wipe the plaintext attempt buffer so it doesn't
         * linger in BSS for someone with a JTAG / memdump.          */
        k_explicit_bzero(g_input, sizeof g_input);
        g_input_len = 0;
        return;
    }
    if (buf_append_key(g_input, &g_input_len, 24, key)) {
        g_show_error = false;
    }
}
