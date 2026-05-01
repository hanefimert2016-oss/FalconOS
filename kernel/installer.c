/* =============================================================================
 *  FalconOS — first-boot installer wizard  (v5+)
 * -----------------------------------------------------------------------------
 *  The wizard runs once on cold boot, before the desktop comes up.  It walks
 *  the user through:
 *
 *      1. LANG       — Türkçe  /  English             (←/→  + Enter)
 *      2. THEME      — Açık (Lumen)  /  Koyu (Nox)    (←/→  + Enter)
 *      3. ACCENT     — Blue / Purple / Green / Pink / Graphite
 *      4. KEYBOARD   — TR-Q / TR-F / US-QWERTY        (saved in SET.kbd_layout)
 *      5. USER NAME  — type 0..23 chars, Enter to confirm
 *      6. PASSWORD   — type 0..23 chars, Enter to confirm (hashed via PBKDF2)
 *      7. MORE?      — add another user yes/no.  Up to FALCON_MAX_USERS.
 *                      First user created is automatically "default" (auto-
 *                      logged-in target on every cold boot).
 *
 *  When step 7 finishes we mark SET.installed = true and write the entire
 *  settings_t (including the user table) to LBA0 via diskdb_save() so the
 *  next boot skips the wizard entirely.
 * ============================================================================= */
#include "falcon.h"

typedef enum {
    INST_LANG = 0,
    INST_THEME,
    INST_ACCENT,
    INST_KBD,
    INST_USER_NAME,
    INST_USER_PASS,
    INST_USER_MORE,     /* "create another user?" — y/n                   */
    INST_DONE
} install_step_t;

static install_step_t g_step = INST_LANG;
static i32 g_choice = 0;

static char g_uname[FALCON_NAME_BYTES];
static i32  g_uname_len = 0;
static char g_pwd[24];
static i32  g_pwd_len = 0;

bool installer_is_done(void) { return g_step == INST_DONE; }

/* --------------------------------------------------------------------------- */
static void draw_card(i32 cx, i32 cy, i32 w, i32 h)
{
    i32 x = cx - w / 2, y = cy - h / 2;
    gfx_round_rect_a(x - 2, y - 2, w + 4, h + 4, 22, PAL_ACCENT, 30);
    gfx_round_glass(x, y, w, h, 20);
    gfx_round_outline(x, y, w, h, 20, PAL_HAIRLINE);
}

static void draw_progress(i32 cx, i32 y, i32 active, i32 total)
{
    for (i32 i = 0; i < total; i++) {
        i32 dx = cx - (total - 1) * 15 + i * 30;
        u32 c = (i <= active) ? PAL_ACCENT : PAL_HAIRLINE;
        gfx_circle(dx, y, i == active ? 6 : 4, c);
    }
}

static void draw_choice_row(i32 y, const char *items[], i32 n, i32 sel)
{
    i32 W   = (i32)FB.width;
    i32 pad = 14;
    i32 pw  = 180;
    i32 row_w = n * pw + (n - 1) * pad;
    i32 x0 = (W - row_w) / 2;
    for (i32 i = 0; i < n; i++) {
        i32 px = x0 + i * (pw + pad);
        bool active = (i == sel);
        if (active) {
            gfx_round_rect_a(px - 3, y - 3, pw + 6, 56 + 6, 14, PAL_ACCENT, 60);
            gfx_round_rect(px, y, pw, 56, 12, PAL_ACCENT);
            gfx_text_centered(px + pw / 2, y + 20, items[i], 0xFFFFFF);
        } else {
            gfx_round_rect_a(px, y, pw, 56, 12, PAL_PANEL_DEEP, 240);
            gfx_round_outline(px, y, pw, 56, 12, PAL_HAIRLINE);
            gfx_text_centered(px + pw / 2, y + 20, items[i], PAL_TEXT_DIM);
        }
    }
}

static void mask_password(char *out, i32 n)
{
    for (i32 i = 0; i < n; i++) out[i] = '*';
    out[n] = 0;
}

/* --------------------------------------------------------------------------- */
void installer_render(u32 frame)
{
    gfx_gradient_v(PAL_BG_TOP, PAL_BG_BOT);

    i32 W = (i32)FB.width;
    i32 H = (i32)FB.height;
    i32 cx = W / 2;
    i32 cy = H / 2;

    /* logo + title — the title uses the 16×32 headline font so the
     * setup wizard reads as a real OS install screen, not a debug ROM. */
    gfx_circle(cx, cy - 220, 36, PAL_ACCENT);
    gfx_text_lg_centered(cx, cy - 236, "F", 0xFFFFFF);
    gfx_text_lg_centered(cx, cy - 180, "FalconOS Setup", PAL_TEXT);

    /* card */
    draw_card(cx, cy, 760, 280);

    const char *headline = "";
    const char *helptext = "";

    switch (g_step) {
        case INST_LANG: {
            headline = "Choose your language  /  Dilini sec";
            helptext = "<-/->  switch    Enter  continue";
            const char *items[2] = { "Turkce", "English" };
            gfx_text_centered(cx, cy - 100, headline, PAL_TEXT);
            draw_choice_row(cy - 40, items, 2, g_choice);
            gfx_text_centered(cx, cy + 76, helptext, PAL_TEXT_DIM);
            break;
        }
        case INST_THEME: {
            headline = T("Pick a theme",
                          "Bir tema sec");
            helptext = T("Light feels macOS-Big-Sur.   Dark is the Nox palette.",
                          "Acik macOS havasi.   Koyu Nox paleti.");
            const char *items[2] = {
                T("Light", "Acik"),
                T("Dark",  "Koyu"),
            };
            gfx_text_centered(cx, cy - 100, headline, PAL_TEXT);
            draw_choice_row(cy - 40, items, 2, g_choice);
            gfx_text_centered(cx, cy + 76, helptext, PAL_TEXT_DIM);
            break;
        }
        case INST_ACCENT: {
            headline = T("Pick an accent color",
                          "Vurgu rengi sec");
            helptext = T("Used for buttons, badges and highlights.",
                          "Butonlar, rozetler ve vurgular icin.");
            const char *items[5] = { "Blue", "Purple", "Green", "Pink", "Graphite" };
            gfx_text_centered(cx, cy - 100, headline, PAL_TEXT);
            i32 pw = 130, pad = 10, n = 5;
            i32 row_w = n * pw + (n - 1) * pad;
            i32 x0 = cx - row_w / 2;
            i32 y0 = cy - 40;
            for (i32 i = 0; i < n; i++) {
                i32 px = x0 + i * (pw + pad);
                u32 sw[5] = { 0x3070FF, 0xA45EE5, 0x16B5A8, 0xE85D9C, 0x6E7884 };
                if (i == g_choice) {
                    gfx_round_rect_a(px - 3, y0 - 3, pw + 6, 60 + 6, 14, sw[i], 60);
                    gfx_round_rect(px, y0, pw, 60, 12, sw[i]);
                    gfx_text_centered(px + pw / 2, y0 + 22, items[i], 0xFFFFFF);
                } else {
                    gfx_round_rect_a(px, y0, pw, 60, 12, PAL_PANEL_DEEP, 240);
                    gfx_round_outline(px, y0, pw, 60, 12, PAL_HAIRLINE);
                    gfx_circle(px + 16, y0 + 30, 8, sw[i]);
                    gfx_text(px + 30, y0 + 22, items[i], PAL_TEXT_DIM);
                }
            }
            gfx_text_centered(cx, cy + 76, helptext, PAL_TEXT_DIM);
            break;
        }
        case INST_KBD: {
            headline = T("Choose your keyboard layout",
                          "Klavye duzenini sec");
            helptext = T("Used for typing in apps. Affects scancode -> char.",
                          "Uygulamada yazma icin. Scancode -> karakter etkilenir.");
            const char *items[3] = { "TR-Q", "TR-F", "US-QWERTY" };
            gfx_text_centered(cx, cy - 100, headline, PAL_TEXT);
            draw_choice_row(cy - 40, items, 3, g_choice);
            gfx_text_centered(cx, cy + 76, helptext, PAL_TEXT_DIM);
            break;
        }
        case INST_USER_NAME: {
            headline = T("Create your account",
                          "Hesabini olustur");
            char hbuf[64]; k_strcpy(hbuf, T("User ", "Kullanici "));
            char num[8]; k_itoa((u32)(SET.user_count + 1), num, 10); k_strcat(hbuf, num);
            k_strcat(hbuf, T(" of up to 8.   Type a name, Enter to continue.",
                              "/8.   Bir ad yaz, Enter ile devam."));
            helptext = hbuf;
            gfx_text_centered(cx, cy - 100, headline, PAL_TEXT);

            i32 fw = 420, fh = 56;
            i32 fx = cx - fw / 2, fy = cy - 28;
            gfx_round_rect_a(fx, fy, fw, fh, 14, PAL_PANEL, 240);
            gfx_round_outline(fx, fy, fw, fh, 14, PAL_ACCENT);
            gfx_text(fx + 16, fy + 20, g_uname, PAL_TEXT);
            i32 caret_x = fx + 16 + gfx_text_width(g_uname);
            if ((frame / 30) & 1) gfx_rect(caret_x, fy + 16, 2, 22, PAL_ACCENT);

            gfx_text_centered(cx, cy + 76, helptext, PAL_TEXT_DIM);
            break;
        }
        case INST_USER_PASS: {
            headline = T("Set a password",
                          "Bir parola belirle");
            helptext = T("Hashed with PBKDF2-HMAC-SHA256, 50000 rounds + salt.",
                          "PBKDF2-HMAC-SHA256, 50000 tur + salt ile hash.");
            gfx_text_centered(cx, cy - 100, headline, PAL_TEXT);

            char who[40]; k_strcpy(who, T("User: ", "Kullanici: "));
            k_strcat(who, g_uname);
            gfx_text_centered(cx, cy - 78, who, PAL_TEXT_DIM);

            i32 fw = 420, fh = 56;
            i32 fx = cx - fw / 2, fy = cy - 18;
            gfx_round_rect_a(fx, fy, fw, fh, 14, PAL_PANEL, 240);
            gfx_round_outline(fx, fy, fw, fh, 14, PAL_ACCENT);
            char masked[24]; mask_password(masked, g_pwd_len);
            gfx_text(fx + 16, fy + 20, masked, PAL_TEXT);
            i32 caret_x = fx + 16 + gfx_text_width(masked);
            if ((frame / 30) & 1) gfx_rect(caret_x, fy + 16, 2, 22, PAL_ACCENT);

            gfx_text_centered(cx, cy + 76, helptext, PAL_TEXT_DIM);
            break;
        }
        case INST_USER_MORE: {
            char hbuf[80];
            k_strcpy(hbuf, T("Add another user?  ", "Baska kullanici ekle?  "));
            char num[8]; k_itoa((u32)SET.user_count, num, 10); k_strcat(hbuf, num);
            k_strcat(hbuf, "/8");
            headline = hbuf;
            helptext = T("First created user becomes the default (auto on boot).",
                          "Ilk olusturulan varsayilan olur (boot'ta otomatik).");
            const char *items[2] = {
                T("Yes, add another", "Evet, ekle"),
                T("No, finish setup", "Hayir, bitir")
            };
            gfx_text_centered(cx, cy - 100, headline, PAL_TEXT);
            draw_choice_row(cy - 40, items, 2, g_choice);
            gfx_text_centered(cx, cy + 76, helptext, PAL_TEXT_DIM);
            break;
        }
        case INST_DONE: break;
    }

    /* visual progress: 7 dots                                                */
    i32 step_idx = (i32)g_step;
    if (step_idx > 6) step_idx = 6;
    draw_progress(cx, cy + 124, step_idx, 7);

    gfx_text_centered(cx, H - 30, "FalconOS v5 Aurora  -  bare-metal x86_64", PAL_TEXT_FAINT);
}

/* --------------------------------------------------------------------------- */
static void commit_user(void)
{
    g_uname[g_uname_len] = 0;
    g_pwd[g_pwd_len]     = 0;
    if (g_uname_len == 0) k_strcpy(g_uname, "User");

    /* per-user accent: cycle through accents for visual variety              */
    accent_t acc = (accent_t)((SET.user_count) % ACC_COUNT);
    users_add(g_uname, g_pwd, acc);

    /* Mirror first user into legacy SET.owner so any v5-era code that
     * still references SET.owner keeps showing the right name. We
     * deliberately do NOT mirror the password — auth always goes
     * through users_verify() against the PBKDF2 hash, and copying the
     * plaintext into SET.password leaves it on disk too.              */
    if (SET.user_count == 1) {
        k_strcpy(SET.owner, g_uname);
    }
    /* Wipe the legacy field if anything earlier touched it.            */
    k_explicit_bzero(SET.password, sizeof SET.password);

    /* Wipe the local capture buffers so the plaintext password doesn't
     * linger in BSS for the rest of the boot.                          */
    k_explicit_bzero(g_uname, sizeof g_uname); g_uname_len = 0;
    k_explicit_bzero(g_pwd,   sizeof g_pwd);   g_pwd_len   = 0;
}

void installer_input(i32 key)
{
    if (g_step == INST_DONE) return;

    /* horizontal-choice steps -------------------------------------------- */
    if (g_step == INST_LANG  || g_step == INST_THEME  ||
        g_step == INST_ACCENT|| g_step == INST_KBD    ||
        g_step == INST_USER_MORE) {
        i32 max;
        switch (g_step) {
            case INST_ACCENT:   max = 5; break;
            case INST_KBD:      max = 3; break;
            case INST_LANG:
            case INST_THEME:
            case INST_USER_MORE:max = 2; break;
            default:            max = 2; break;
        }
        if (key == KEY_LEFT)  { if (--g_choice < 0)    g_choice = max - 1; return; }
        if (key == KEY_RIGHT) { if (++g_choice >= max) g_choice = 0;       return; }
        if (key == KEY_ENTER) {
            switch (g_step) {
                case INST_LANG:
                    SET.lang = (g_choice == 0) ? LANG_TR : LANG_EN;
                    g_step = INST_THEME;   g_choice = SET.theme;        return;
                case INST_THEME:
                    SET.theme = (g_choice == 0) ? THEME_LIGHT : THEME_DARK;
                    g_step = INST_ACCENT;  g_choice = SET.accent;       return;
                case INST_ACCENT:
                    SET.accent = (accent_t)g_choice;
                    g_step = INST_KBD;     g_choice = SET.kbd_layout;   return;
                case INST_KBD:
                    SET.kbd_layout = (kbd_layout_t)g_choice;
                    g_step = INST_USER_NAME; g_choice = 0;               return;
                case INST_USER_MORE:
                    if (g_choice == 0 && SET.user_count < FALCON_MAX_USERS) {
                        g_step = INST_USER_NAME; g_choice = 0;
                    } else {
                        SET.installed   = true;
                        SET.active_user = SET.default_user;
                        diskdb_save();             /* persist everything    */
                        g_step = INST_DONE;
                    }
                    return;
                default: return;
            }
        }
        return;
    }

    /* text-input steps ----------------------------------------------------- */
    if (g_step == INST_USER_NAME) {
        if (key == KEY_BACKSPACE) { if (g_uname_len) g_uname[--g_uname_len] = 0; return; }
        if (key == KEY_ENTER) {
            g_uname[g_uname_len] = 0;
            g_step = INST_USER_PASS;
            return;
        }
        if (key >= 0x20 && key < 0x7F && g_uname_len < FALCON_NAME_BYTES - 1) {
            g_uname[g_uname_len++] = (char)key;
            g_uname[g_uname_len]   = 0;
        }
        return;
    }
    if (g_step == INST_USER_PASS) {
        if (key == KEY_BACKSPACE) { if (g_pwd_len) g_pwd[--g_pwd_len] = 0; return; }
        if (key == KEY_ENTER) {
            g_pwd[g_pwd_len] = 0;
            commit_user();
            if (SET.user_count >= FALCON_MAX_USERS) {
                SET.installed   = true;
                SET.active_user = SET.default_user;
                diskdb_save();
                g_step = INST_DONE;
            } else {
                g_step = INST_USER_MORE;
                g_choice = (SET.user_count >= 2) ? 1 : 0;   /* nudge "no" after 2 */
            }
            return;
        }
        if (key >= 0x20 && key < 0x7F && g_pwd_len < 23) {
            g_pwd[g_pwd_len++] = (char)key;
            g_pwd[g_pwd_len]   = 0;
        }
        return;
    }
}
