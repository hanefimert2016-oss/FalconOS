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
    INST_DISK,          /* FalconOS 1 — pick where to install the OS      */
    INST_USER_NAME,
    INST_USER_PASS,
    INST_USER_PASS2,    /* confirm password — must match step 6           */
    INST_USER_MORE,     /* "create another user?" — y/n                   */
    INST_DONE
} install_step_t;

static install_step_t g_step = INST_LANG;
static i32 g_choice = 0;

static char g_uname[FALCON_NAME_BYTES];
static i32  g_uname_len = 0;
static char g_pwd[24];
static i32  g_pwd_len = 0;
static char g_pwd2[24];
static i32  g_pwd2_len = 0;
static bool g_pwd_mismatch = false;

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
            headline = "Dilinizi seçin  —  Choose language  —  Sprache wählen  —  Choisir";
            helptext   = "<-/-> değiştir / switch    Enter ile devam / continue";
            /* 11 languages — one row of tiles, each with a coloured flag
             * pip drawn over the active highlight so the eye matches
             * country quickly even when the label is transliterated.   */
            const char *items[LANG_COUNT] = {
                "Türkçe", "English", "Deutsch", "Français", "Español",
                "Italiano", "Português", "Русский", "العربية",
                "中文", "日本語"
            };
            const u32 lang_dot[LANG_COUNT] = {
                0xE30A17,  /* TR  red                       */
                0x012169,  /* EN  British navy              */
                0x000000,  /* DE  black                     */
                0x0055A4,  /* FR  blue                      */
                0xAA151B,  /* ES  red                       */
                0x009246,  /* IT  green                     */
                0xFF0000,  /* PT  red                       */
                0xFFFFFF,  /* RU  white (top stripe)        */
                0x006C35,  /* AR  green (Saudi-style ref.)  */
                0xDE2910,  /* ZH  red                       */
                0xBC002D,  /* JA  hi-no-maru                */
            };
            gfx_text_centered(cx, cy - 110, headline, PAL_TEXT);
            /* Hand-tiled because draw_choice_row only fits 5; we lay
             * out a single row of 11 narrow tiles (120 px each).      */
            i32 W = (i32)FB.width, pad = 6, pw = 120, n = LANG_COUNT;
            i32 row_w = n * pw + (n - 1) * pad;
            i32 x0 = (W - row_w) / 2;
            i32 y0 = cy - 40;
            for (i32 i = 0; i < n; i++) {
                i32 px = x0 + i * (pw + pad);
                bool active = (i == g_choice);
                if (active) {
                    gfx_round_rect_a(px - 3, y0 - 3, pw + 6, 60 + 6, 14, PAL_ACCENT, 60);
                    gfx_round_rect(px, y0, pw, 60, 12, PAL_ACCENT);
                    gfx_text_centered(px + pw / 2, y0 + 22, items[i], 0xFFFFFF);
                } else {
                    gfx_round_rect_a(px, y0, pw, 60, 12, PAL_PANEL_DEEP, 240);
                    gfx_round_outline(px, y0, pw, 60, 12, PAL_HAIRLINE);
                    gfx_text_centered(px + pw / 2, y0 + 22, items[i], PAL_TEXT_DIM);
                }
                /* coloured 8x8 flag pip in the top-left corner       */
                gfx_round_rect(px + 8, y0 + 8, 8, 8, 4, lang_dot[i]);
            }
            gfx_text_centered(cx, cy + 76, helptext, PAL_TEXT_DIM);
            break;
        }
        case INST_THEME: {
            headline = T("Pick a theme",
                          "Bir tema seç");
            helptext = T("You can change this any time from Settings.",
                          "Ayarlardan istediğin zaman değiştirebilirsin.");
            /* 5 themes shipped in the box. Tile width is shrunk so all
             * five fit on a single row at FHD.                         */
            const char *items[THEME_COUNT] = {
                "Lumen", "Nox", "Liquid", "Nordic", "Rose Gold"
            };
            gfx_text_centered(cx, cy - 100, headline, PAL_TEXT);
            i32 W = (i32)FB.width, pad = 10, pw = 130, n = THEME_COUNT;
            i32 row_w = n * pw + (n - 1) * pad;
            i32 x0 = (W - row_w) / 2;
            i32 y0 = cy - 40;
            const u32 swatch[THEME_COUNT] = {
                0xEAF0F8, 0x101418, 0xDDE9F4, 0xECEFF4, 0xFAF1EE
            };
            for (i32 i = 0; i < n; i++) {
                i32 px = x0 + i * (pw + pad);
                bool active = (i == g_choice);
                if (active) {
                    gfx_round_rect_a(px - 3, y0 - 3, pw + 6, 60 + 6, 14, PAL_ACCENT, 60);
                    gfx_round_rect(px, y0, pw, 60, 12, PAL_ACCENT);
                    gfx_text_centered(px + pw / 2, y0 + 22, items[i], 0xFFFFFF);
                } else {
                    gfx_round_rect_a(px, y0, pw, 60, 12, PAL_PANEL_DEEP, 240);
                    gfx_round_outline(px, y0, pw, 60, 12, PAL_HAIRLINE);
                    gfx_round_rect(px + 14, y0 + 22, 16, 16, 6, swatch[i]);
                    gfx_text(px + 38, y0 + 22, items[i], PAL_TEXT_DIM);
                }
            }
            gfx_text_centered(cx, cy + 76, helptext, PAL_TEXT_DIM);
            break;
        }
        case INST_ACCENT: {
            headline = T("Pick an accent color",
                          "Vurgu rengi seç");
            helptext = T("Used for buttons, badges and highlights.",
                          "Butonlar, rozetler ve vurgular için.");
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
                          "Klavye düzenini seç");
            helptext = T("Used for typing in apps. Affects scancode -> char.",
                          "Uygulamada yazma için. Scancode -> karakter etkilenir.");
            const char *items[3] = { "TR-Q", "TR-F", "US-QWERTY" };
            gfx_text_centered(cx, cy - 100, headline, PAL_TEXT);
            draw_choice_row(cy - 40, items, 3, g_choice);
            gfx_text_centered(cx, cy + 76, helptext, PAL_TEXT_DIM);
            break;
        }
        case INST_DISK: {
            headline = T("Choose where to install FalconOS 1",
                          "FalconOS 1'i nereye kuralım?");
            helptext = T("User database + settings will be written to this disk.",
                          "Kullanıcı + ayarlar bu diske yazılacak.");
            gfx_text_centered(cx, cy - 100, headline, PAL_TEXT);

            /* Each ATA drive + “güvenli çalıştırma” (no FalconFS persistence). */
            i32 n_ata    = ata_probe_count();
            i32 n_total  = n_ata + 1;            /* +1 = güvenli slot   */
            if (g_choice >= n_total) g_choice = n_total - 1;

            i32 row_h = 60, gap = 10;
            i32 list_h = n_total * row_h + (n_total - 1) * gap;
            i32 y0 = cy - list_h / 2;

            for (i32 i = 0; i < n_total; i++) {
                i32 yy   = y0 + i * (row_h + gap);
                i32 rw   = 620;
                i32 rx   = cx - rw / 2;
                bool act = (i == g_choice);
                if (act) {
                    gfx_round_rect_a(rx - 3, yy - 3, rw + 6, row_h + 6, 14,
                                     PAL_ACCENT, 60);
                    gfx_round_rect (rx, yy, rw, row_h, 12, PAL_ACCENT);
                } else {
                    gfx_round_rect_a(rx, yy, rw, row_h, 12, PAL_PANEL_DEEP, 240);
                    gfx_round_outline(rx, yy, rw, row_h, 12, PAL_HAIRLINE);
                }

                /* HDD glyph (left edge) */
                u32 ic_col = act ? 0xFFFFFF : PAL_TEXT_DIM;
                i32 ix = rx + 16, iy = yy + row_h / 2 - 10;
                gfx_round_rect(ix, iy, 28, 20, 4, ic_col);
                gfx_round_rect(ix + 4, iy + 4, 20, 12, 2, act ? PAL_ACCENT : PAL_PANEL);
                gfx_circle(ix + 22, iy + 14, 2, act ? PAL_ACCENT : PAL_PANEL);

                /* Last entry = güvenli çalıştırma (diskdb_save disabled).   */
                u32 title_c = act ? 0xFFFFFF : PAL_TEXT;
                u32 sub_c   = act ? 0xC8DBFF : PAL_TEXT_DIM;
                if (i < n_ata) {
                    char title[80]; k_strcpy(title, T("Disk ", "Disk "));
                    char num[8]; k_itoa((u32)i, num, 10); k_strcat(title, num);
                    k_strcat(title, "  -  ");
                    k_strcat(title, ata_model(i));

                    char sub[80];
                    /* sectors * 512 / 1024 / 1024 = MiB */
                    u64 sect = ata_sectors(i);
                    u32 mib  = (u32)((sect * 512ull) >> 20);
                    if (mib >= 1024) {
                        u32 gib_x10 = (mib * 10) / 1024;
                        char b[8]; k_itoa(gib_x10 / 10, b, 10);
                        k_strcpy(sub, b);
                        k_strcat(sub, ".");
                        k_itoa(gib_x10 % 10, b, 10); k_strcat(sub, b);
                        k_strcat(sub, " GiB");
                    } else {
                        char b[8]; k_itoa(mib, b, 10);
                        k_strcpy(sub, b); k_strcat(sub, " MiB");
                    }
                    k_strcat(sub, T("   primary IDE", "   birincil IDE"));
                    gfx_text(rx + 56, yy + 14, title, title_c);
                    gfx_text(rx + 56, yy + 36, sub,   sub_c);
                } else {
                    gfx_text(rx + 56, yy + 14,
                             T("Secure session  —  no FalconFS write to ATA",
                               "Güvenli oturum  —  ATA'ya FalconFS yazılmaz"),
                             title_c);
                    gfx_text(rx + 56, yy + 36,
                             T("Try changes without leaving data on removable media.",
                               "Kalıcı veri yazmadan dene — USB çıkarinca iz kalmaz (oturumluk)."),
                             sub_c);
                }
            }
            gfx_text_centered(cx, cy + list_h / 2 + 24, helptext, PAL_TEXT_DIM);
            break;
        }
        case INST_USER_NAME: {
            headline = T("Create your account",
                          "Hesabını oluştur");
            char hbuf[64]; k_strcpy(hbuf, T("User ", "Kullanıcı "));
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
            helptext = T("Step 1 of 2.  You'll confirm this on the next screen.",
                          "Adim 1/2.  Sonraki ekranda tekrar yazacaksin.");
            gfx_text_centered(cx, cy - 100, headline, PAL_TEXT);

            char who[40]; k_strcpy(who, T("User: ", "Kullanıcı: "));
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

            /* Strength meter — three pips that light up as the password
             * gets longer/more diverse. Empty pwd = 0 pips (allowed but
             * dimmed warning); 12+ chars w/ mixed classes = 3 pips.    */
            g_pwd[g_pwd_len] = 0;
            i32 strength = password_strength(g_pwd);
            i32 sx = fx + fw - 90, sy = fy + 22;
            for (i32 i = 0; i < 3; i++) {
                u32 c = (i < strength) ? COL_OK : PAL_HAIRLINE;
                if (strength == 1 && i == 0) c = COL_WARN;
                if (strength == 2 && i <  2) c = COL_OK;
                gfx_round_rect(sx + i * 22, sy, 16, 12, 5, c);
            }
            const char *strength_label;
            switch (strength) {
                case 0: strength_label = TX("(empty)", "(boş)",
                                             "(leer)", "(vide)", "(vacio)"); break;
                case 1: strength_label = TX("Weak", "Zayif",
                                             "Schwach","Faible","Debil"); break;
                case 2: strength_label = TX("OK",   "İyi",
                                             "OK",  "OK",     "OK"); break;
                default:strength_label = TX("Strong","Guclu",
                                             "Stark","Forte", "Fuerte"); break;
            }
            gfx_text(sx, sy + 16, strength_label,
                     strength >= 2 ? COL_OK :
                     strength == 1 ? COL_WARN : PAL_TEXT_FAINT);

            gfx_text_centered(cx, cy + 76, helptext, PAL_TEXT_DIM);
            break;
        }
        case INST_USER_PASS2: {
            headline = T("Confirm your password",
                          "Parolanı onayla");
            helptext = g_pwd_mismatch
                ? T("Passwords did not match. Try again.",
                    "Parolalar eslesmedi. Tekrar dene.")
                : T("Step 2 of 2.  Type the same password again.",
                    "Adim 2/2.  Ayni parolayi tekrar yaz.");
            gfx_text_centered(cx, cy - 100, headline, PAL_TEXT);

            char who[40]; k_strcpy(who, T("User: ", "Kullanıcı: "));
            k_strcat(who, g_uname);
            gfx_text_centered(cx, cy - 78, who, PAL_TEXT_DIM);

            i32 fw = 420, fh = 56;
            i32 fx = cx - fw / 2, fy = cy - 18;
            gfx_round_rect_a(fx, fy, fw, fh, 14, PAL_PANEL, 240);
            u32 outline = g_pwd_mismatch ? COL_ERR : PAL_ACCENT;
            gfx_round_outline(fx, fy, fw, fh, 14, outline);
            char masked[24]; mask_password(masked, g_pwd2_len);
            gfx_text(fx + 16, fy + 20, masked, PAL_TEXT);
            i32 caret_x = fx + 16 + gfx_text_width(masked);
            if ((frame / 30) & 1) gfx_rect(caret_x, fy + 16, 2, 22, outline);

            /* live match indicator — green check when prefixes agree */
            if (g_pwd2_len > 0) {
                bool prefix_ok = (g_pwd2_len <= g_pwd_len);
                for (i32 i = 0; i < g_pwd2_len && prefix_ok; i++)
                    if (g_pwd[i] != g_pwd2[i]) prefix_ok = false;
                u32 col = prefix_ok ? COL_OK : COL_ERR;
                const char *lab = prefix_ok
                    ? T("matches", "esit")
                    : T("does not match", "eslesmiyor");
                gfx_text(fx + fw - 130, fy + 20, lab, col);
            }

            gfx_text_centered(cx, cy + 76, helptext,
                              g_pwd_mismatch ? COL_ERR : PAL_TEXT_DIM);
            break;
        }
        case INST_USER_MORE: {
            char hbuf[80];
            k_strcpy(hbuf, T("Add another user?  ", "Baska kullanıcı ekle?  "));
            char num[8]; k_itoa((u32)SET.user_count, num, 10); k_strcat(hbuf, num);
            k_strcat(hbuf, "/8");
            headline = hbuf;
            helptext = T("First created user becomes the default (auto on boot).",
                          "İlk oluşturulan varsayılan olur (boot'ta otomatik).");
            const char *items[2] = {
                T("Yes, add another", "Evet, ekle"),
                T("No, finish setup", "Hayır, bitir")
            };
            gfx_text_centered(cx, cy - 100, headline, PAL_TEXT);
            draw_choice_row(cy - 40, items, 2, g_choice);
            gfx_text_centered(cx, cy + 76, helptext, PAL_TEXT_DIM);
            break;
        }
        case INST_DONE: break;
    }

    /* visual progress: 9 dots (extra one for the disk-target step)         */
    i32 step_idx = (i32)g_step;
    if (step_idx > 8) step_idx = 8;
    draw_progress(cx, cy + 124, step_idx, 9);

    gfx_text_centered(cx, H - 30, "FalconOS 1  -  bare-metal x86_64", PAL_TEXT_FAINT);
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
        g_step == INST_DISK  || g_step == INST_USER_MORE) {
        i32 max;
        switch (g_step) {
            case INST_LANG:     max = LANG_COUNT; break;
            case INST_THEME:    max = THEME_COUNT; break;
            case INST_ACCENT:   max = 5; break;
            case INST_KBD:      max = 3; break;
            case INST_DISK:     max = ata_probe_count() + 1; break; /* güvenli */
            case INST_USER_MORE:max = 2; break;
            default:            max = 2; break;
        }
        /* Disk picker is a vertical list: Up/Down also navigate.    */
        if (g_step == INST_DISK) {
            if (key == KEY_UP)    { if (--g_choice < 0)    g_choice = max - 1; return; }
            if (key == KEY_DOWN)  { if (++g_choice >= max) g_choice = 0;       return; }
        }
        if (key == KEY_LEFT)  { if (--g_choice < 0)    g_choice = max - 1; return; }
        if (key == KEY_RIGHT) { if (++g_choice >= max) g_choice = 0;       return; }
        if (key == KEY_ENTER) {
            switch (g_step) {
                case INST_LANG:
                    SET.lang = (lang_t)g_choice;
                    /* default keyboard heuristic — only TR users want
                     * a TR layout; everyone else gets US-QWERTY.       */
                    if (SET.lang == LANG_TR) SET.kbd_layout = KBD_TR_Q;
                    else                     SET.kbd_layout = KBD_US;
                    g_step = INST_THEME;   g_choice = SET.theme;        return;
                case INST_THEME:
                    SET.theme = (theme_t)g_choice;
                    g_step = INST_ACCENT;  g_choice = SET.accent;       return;
                case INST_ACCENT:
                    SET.accent = (accent_t)g_choice;
                    g_step = INST_KBD;     g_choice = SET.kbd_layout;   return;
                case INST_KBD:
                    SET.kbd_layout = (kbd_layout_t)g_choice;
                    g_step = INST_DISK;
                    /* default-pick: first detected disk if any, else
                     * the güvenli sentinel (last index).         */
                    g_choice = (ata_probe_count() > 0) ? 0
                                                       : ata_probe_count();
                    return;
                case INST_DISK: {
                    i32 n_ata = ata_probe_count();
                    SET.install_disk = (g_choice < n_ata) ? g_choice : -1;
                    g_step = INST_USER_NAME; g_choice = 0;
                    return;
                }
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
        if (key == KEY_BACKSPACE) { buf_pop_utf8(g_uname, &g_uname_len); return; }
        if (key == KEY_ENTER) {
            g_uname[g_uname_len] = 0;
            /* Validate username: must have at least one letter/number/symbol */
            bool has_letter = false, has_num = false, has_symbol = false;
            for (i32 i = 0; g_uname[i]; i++) {
                u8 c = (u8)g_uname[i];
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= 0xC0))
                    has_letter = true;
                else if (c >= '0' && c <= '9')
                    has_num = true;
                else if (c == '_' || c == '-' || c == '.' || c == '@')
                    has_symbol = true;
            }
            if (!has_letter && !has_num && !has_symbol) {
                /* Invalid username - show warning by clearing and staying */
                g_uname_len = 0;
                return;
            }
            g_step = INST_USER_PASS;
            return;
        }
        (void)buf_append_key(g_uname, &g_uname_len, FALCON_NAME_BYTES, key);
        return;
    }
    if (g_step == INST_USER_PASS) {
        if (key == KEY_BACKSPACE) { buf_pop_utf8(g_pwd, &g_pwd_len); return; }
        if (key == KEY_ENTER) {
            g_pwd[g_pwd_len] = 0;
            /* Move to confirmation step instead of committing.        */
            g_pwd2_len = 0;
            g_pwd2[0]  = 0;
            g_pwd_mismatch = false;
            g_step = INST_USER_PASS2;
            return;
        }
        (void)buf_append_key(g_pwd, &g_pwd_len, 24, key);
        return;
    }
    if (g_step == INST_USER_PASS2) {
        if (key == KEY_BACKSPACE) { buf_pop_utf8(g_pwd2, &g_pwd2_len); return; }
        if (key == KEY_ENTER) {
            g_pwd2[g_pwd2_len] = 0;
            bool match = (g_pwd_len == g_pwd2_len);
            for (i32 i = 0; i < g_pwd_len && match; i++)
                if (g_pwd[i] != g_pwd2[i]) match = false;
            if (!match) {
                /* mismatch — wipe both buffers and bounce back to the
                 * original password step with a red error banner.    */
                g_pwd_mismatch = true;
                k_explicit_bzero(g_pwd,  sizeof g_pwd);  g_pwd_len  = 0;
                k_explicit_bzero(g_pwd2, sizeof g_pwd2); g_pwd2_len = 0;
                g_step = INST_USER_PASS;
                return;
            }
            /* match — wipe the confirm buffer and commit.            */
            k_explicit_bzero(g_pwd2, sizeof g_pwd2); g_pwd2_len = 0;
            g_pwd_mismatch = false;
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
        (void)buf_append_key(g_pwd2, &g_pwd2_len, 24, key);
        return;
    }
}
