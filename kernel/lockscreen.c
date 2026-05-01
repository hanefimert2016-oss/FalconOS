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

bool lockscreen_is_unlocked(void) { return g_unlocked; }

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
    ensure_cursor_valid();

    gfx_gradient_v(PAL_BG_TOP, PAL_BG_HINT);
    gfx_rect_a(0, 0, FB.width, FB.height, COL_SHADOW, 70);

    i32 W = (i32)FB.width, H = (i32)FB.height;
    i32 cx = W / 2,        cy = H / 2;

    /* ---- live clock + "welcome back" line ------------------------------- */
    {
        u32 H_, M_, S_; pit_uptime(&H_, &M_, &S_);
        char clk[16]; char tmp[8];
        k_strcpy(clk, "");
        k_itoa(H_, tmp, 10); if (H_ < 10) k_strcat(clk, "0"); k_strcat(clk, tmp);
        k_strcat(clk, ":");
        k_itoa(M_, tmp, 10); if (M_ < 10) k_strcat(clk, "0"); k_strcat(clk, tmp);
        /* Big crisp clock + small helper text; gives the lockscreen a
         * proper macOS-style headline rather than a tiny terminal time. */
        gfx_text_lg_centered(cx, cy - 245, clk, PAL_TEXT);
        gfx_text_centered   (cx, cy - 200, T("Welcome back",
                                             "Tekrar hosgeldin"), PAL_TEXT_DIM);
    }

    /* ---- user picker ---------------------------------------------------- */
    draw_user_strip(cx, cy - 110, g_user_cursor);

    /* ---- selected user name --------------------------------------------- */
    const char *uname = "(no users)";
    bool no_pwd = true;
    if (SET.user_count > 0 &&
        g_user_cursor >= 0 && g_user_cursor < FALCON_MAX_USERS &&
        SET.users[g_user_cursor].in_use) {
        uname  = SET.users[g_user_cursor].name;
        no_pwd = SET.users[g_user_cursor].no_password;
    }
    gfx_text_centered(cx, cy - 40, uname, PAL_TEXT);
    if (SET.user_count > 1) {
        gfx_text_centered(cx, cy - 22,
            T("<-  ->   switch user",
              "<-  ->   kullanici degistir"), PAL_TEXT_FAINT);
    }

    /* ---- password pill -------------------------------------------------- */
    i32 fw = 360, fh = 50;
    i32 fx = cx - fw / 2, fy = cy + 4;
    if (frame < g_shake_until) {
        i32 phase = (i32)(frame * 11) & 31;
        i32 dx = ((phase < 8) ? phase : (16 - phase)) - 4;
        fx += dx;
    }
    gfx_round_rect_a(fx, fy, fw, fh, 14, PAL_PANEL, 240);
    gfx_round_outline(fx, fy, fw, fh, 14, PAL_HAIRLINE);

    char masked[24];
    for (i32 i = 0; i < g_input_len; i++) masked[i] = '*';
    masked[g_input_len] = 0;
    if (g_input_len == 0) {
        gfx_text(fx + 18, fy + 18,
                 T("Enter password",
                   "Parola gir"), PAL_TEXT_FAINT);
    } else {
        gfx_text(fx + 18, fy + 18, masked, PAL_TEXT);
    }
    if ((frame / 30) & 1) {
        i32 caret_x = fx + 18 + (g_input_len ? gfx_text_width(masked) : 0);
        gfx_rect(caret_x, fy + 14, 2, 22, PAL_ACCENT);
    }

    /* ---- error / hint --------------------------------------------------- */
    if (g_show_error) {
        gfx_text_centered(cx, fy + fh + 12,
                          T("Wrong password - try again",
                            "Parola yanlis - tekrar dene"), COL_ERR);
    } else if (no_pwd) {
        gfx_text_centered(cx, fy + fh + 12,
                          T("No password set - press Enter",
                            "Parola yok - Enter ile devam"), PAL_TEXT_DIM);
    } else {
        gfx_text_centered(cx, fy + fh + 12,
                          T("Press Enter to unlock",
                            "Enter ile kilidi ac"), PAL_TEXT_DIM);
    }

    /* ---- footer hint ---------------------------------------------------- */
    gfx_text_centered(cx, H - 26,
        "FalconOS v5 Aurora  -  bare-metal x86_64", PAL_TEXT_FAINT);
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
        if (g_input_len) g_input[--g_input_len] = 0;
        g_show_error = false;
        return;
    }
    if (key == KEY_ENTER) {
        g_input[g_input_len] = 0;
        if (users_verify(g_user_cursor, g_input)) {
            SET.active_user = g_user_cursor;
            g_unlocked      = true;
        } else {
            g_show_error  = true;
            g_shake_until = g_tick + 30;
            g_input_len   = 0;
            g_input[0]    = 0;
        }
        return;
    }
    if (key >= 0x20 && key < 0x7F && g_input_len < 23) {
        g_input[g_input_len++] = (char)key;
        g_input[g_input_len]   = 0;
        g_show_error = false;
    }
}
