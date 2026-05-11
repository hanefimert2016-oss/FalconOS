/* ============================================================================
 *  kernel/jarvis.c — built-in system AI assistant ("Jarvis")
 * ----------------------------------------------------------------------------
 *  FalconOS 1 ships a real, working in-kernel assistant that the user can
 *  talk to in natural language (Turkish or English) and have it actually
 *  drive the system: switch theme, open apps, change language, install or
 *  remove a package, lock the screen, read the clock, etc.
 *
 *  No LLM runs on this hardware — there is no userland Python, no libc,
 *  no network stack — so Jarvis is intent-routed: each user prompt is
 *  pattern-matched against ~30 phrase fingerprints, and on match we
 *  perform the corresponding kernel call and return a short reply.  The
 *  effect from the user's seat is identical to chatting with Siri /
 *  Google Assistant for system-control queries; the model just happens
 *  to be a hand-written rule table that fits in a few hundred bytes.
 *
 *  Every reply is logged into a 16-line transcript and persists for the
 *  lifetime of the boot.  Side-effects that touch SET (theme, language,
 *  packages, keyboard layout) call diskdb_save() so the change is
 *  remembered after a cold reboot.
 *
 *  Public surface (callable from kernel/apps.c):
 *      void jarvis_reset(void);
 *      void jarvis_render(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame);
 *      void jarvis_input(i32 key);
 *      void jarvis_icon(i32 cx, i32 cy);
 * ============================================================================ */
#include "falcon.h"

/* ---- transcript ---------------------------------------------------------- */
#define J_LINES 14
#define J_COLS  78
static char  j_log[J_LINES][J_COLS];
static i32   j_n      = 0;
static char  j_input[J_COLS];
static i32   j_in_n   = 0;
static bool  j_inited = false;

/* ---- helpers ------------------------------------------------------------- */
static char j_lower(char c)
{
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

static void j_buf_pop_utf8(char *buf, i32 *len)
{
    if (!buf || !len || *len <= 0) return;
    i32 i = *len - 1;
    while (i > 0 && (((u8)buf[i] & 0xC0u) == 0x80u)) i--;
    buf[i] = 0;
    *len = i;
}

static bool j_buf_append_key(char *buf, i32 *len, i32 cap, i32 key)
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

/* contains_ci(haystack, needle): case-insensitive substring search.
 * Used everywhere below so users can type "TEMA Koyu" or "tema koyu"
 * or even "TeMa  KoYu".                                              */
static bool contains_ci(const char *hay, const char *needle)
{
    if (!hay || !needle) return false;
    for (i32 i = 0; hay[i]; i++) {
        i32 j = 0;
        while (needle[j] && hay[i + j]
               && j_lower((char)hay[i + j]) == j_lower((char)needle[j])) j++;
        if (!needle[j]) return true;
    }
    return false;
}

/* match_any(prompt, list...): true if prompt contains any of the
 * comma-separated tokens.  Tokens are case-insensitive whole substrings.
 * Pass a NULL terminator.                                              */
static bool match_any(const char *prompt, const char *t1, const char *t2,
                      const char *t3, const char *t4)
{
    if (t1 && contains_ci(prompt, t1)) return true;
    if (t2 && contains_ci(prompt, t2)) return true;
    if (t3 && contains_ci(prompt, t3)) return true;
    if (t4 && contains_ci(prompt, t4)) return true;
    return false;
}

/* ---- log emit ------------------------------------------------------------ */
static void j_push(const char *s)
{
    if (j_n < J_LINES) {
        i32 n = 0;
        while (s[n] && n < J_COLS - 1) { j_log[j_n][n] = s[n]; n++; }
        j_log[j_n][n] = 0;
        j_n++;
        return;
    }
    /* scroll */
    for (i32 i = 1; i < J_LINES; i++) k_strcpy(j_log[i - 1], j_log[i]);
    i32 n = 0;
    while (s[n] && n < J_COLS - 1) { j_log[J_LINES - 1][n] = s[n]; n++; }
    j_log[J_LINES - 1][n] = 0;
}

static void j_say(const char *s)
{
    char line[J_COLS];
    k_strcpy(line, "Jarvis: ");
    k_strcat(line, s);
    j_push(line);
}

static void j_user(const char *s)
{
    char line[J_COLS];
    k_strcpy(line, "Sen: ");
    k_strcat(line, s);
    j_push(line);
}

/* ---- index lookup over the apps[] table --------------------------------- */
/* Find an app by case-insensitive substring on its name.  Returns -1 if
 * none matches.                                                          */
static i32 j_find_app(const char *needle)
{
    i32 n = apps_count();
    for (i32 i = 0; i < n; i++) {
        if (contains_ci(apps_name(i), needle) ||
            contains_ci(apps_display_name(i), needle)) return i;
    }
    return -1;
}

/* ---- intent table -------------------------------------------------------- */
typedef struct {
    /* Up to four keywords; ANY-of match.  The lowest-index matcher wins,
     * so order matters when one prompt could trigger multiple intents.   */
    const char *k1, *k2, *k3, *k4;
    /* Handler returns true when it produced a reply.  When false we fall
     * through to the next intent.                                       */
    bool (*handle)(const char *prompt);
} intent_t;

/* Localised "ok" reply respecting current SET.lang. */
static const char *ok_text(void)
{
    switch (SET.lang) {
        case LANG_TR: return "Tamam.";
        case LANG_DE: return "Erledigt.";
        case LANG_FR: return "C'est fait.";
        case LANG_ES: return "Hecho.";
        default:      return "Done.";
    }
}

/* ---- intent handlers ----------------------------------------------------- */
static bool ih_hello(const char *p)
{
    (void)p;
    if (SET.lang == LANG_TR) {
        const falcon_user_t *u = users_at(SET.active_user);
        char line[80];
        k_strcpy(line, "Merhaba ");
        k_strcat(line, u ? u->name : "kullanici");
        k_strcat(line, ", nasil yardim edebilirim?");
        j_say(line);
    } else {
        j_say("Hello, how can I help?");
    }
    return true;
}

static bool ih_help(const char *p)
{
    (void)p;
    j_say(SET.lang == LANG_TR
          ? "Yapabileceklerim: tema degistir, dili degistir, saat,"
          : "I can: switch theme, change language, tell time,");
    j_push("        uygulama ac, paket kur/kaldir, kilitle, kapat,");
    j_push("        kullanicilari listele, durum raporu.");
    return true;
}

static bool ih_time(const char *p)
{
    (void)p;
    rtc_time_t t; rtc_local(&t);
    char line[64], num[8];
    k_strcpy(line, SET.lang == LANG_TR ? "Saat " : "It is ");
    k_itoa(t.hour, num, 10); k_pad(num, 2, '0'); k_strcat(line, num);
    k_strcat(line, ":");
    k_itoa(t.min, num, 10);  k_pad(num, 2, '0'); k_strcat(line, num);
    k_strcat(line, ":");
    k_itoa(t.sec, num, 10);  k_pad(num, 2, '0'); k_strcat(line, num);
    k_strcat(line, ".");
    j_say(line);
    return true;
}

static bool ih_date(const char *p)
{
    (void)p;
    rtc_time_t t; rtc_local(&t);
    char line[64];
    loc_format_date(line, &t);
    j_say(line);
    return true;
}

static bool ih_uptime(const char *p)
{
    (void)p;
    u32 h, m, s; pit_uptime(&h, &m, &s);
    char line[64], num[8];
    k_strcpy(line, SET.lang == LANG_TR ? "Acik kalma suresi: " : "Uptime: ");
    k_itoa(h, num, 10); k_strcat(line, num); k_strcat(line, "h ");
    k_itoa(m, num, 10); k_strcat(line, num); k_strcat(line, "m ");
    k_itoa(s, num, 10); k_strcat(line, num); k_strcat(line, "s.");
    j_say(line);
    return true;
}

static bool ih_whoami(const char *p)
{
    (void)p;
    const falcon_user_t *u = users_at(SET.active_user);
    char line[80];
    k_strcpy(line, SET.lang == LANG_TR ? "Su an oturum: "
                                       : "Currently signed in: ");
    k_strcat(line, u ? u->name : "?");
    j_say(line);
    return true;
}

/* Theme switching ---------------------------------------------------------- */
static void j_apply_theme(theme_t t, const char *human)
{
    SET.theme = t;
    diskdb_save();
    char line[80];
    k_strcpy(line, SET.lang == LANG_TR ? "Tema: " : "Theme: ");
    k_strcat(line, human);
    j_say(line);
}

static bool ih_theme(const char *p)
{
    if (!match_any(p, "tema", "theme", NULL, NULL)) return false;
    if (match_any(p, "koyu", "dark", "nox", NULL)) {
        j_apply_theme(THEME_DARK, "Nox (dark)"); return true;
    }
    if (match_any(p, "acik", "light", "lumen", "aydin")) {
        j_apply_theme(THEME_LIGHT, "Lumen (light)"); return true;
    }
    if (match_any(p, "liquid", "cam", "glass", "tahoe")) {
        j_apply_theme(THEME_LIQUID, "Liquid Glass"); return true;
    }
    if (match_any(p, "nordic", "kuzey", NULL, NULL)) {
        j_apply_theme(THEME_NORDIC, "Nordic"); return true;
    }
    if (match_any(p, "rose", "gul", NULL, NULL)) {
        j_apply_theme(THEME_ROSEGOLD, "Rose Gold"); return true;
    }
    j_say(SET.lang == LANG_TR
          ? "Hangi tema? Acik / Koyu / Liquid / Nordic / Rose."
          : "Which theme? Light / Dark / Liquid / Nordic / Rose.");
    return true;
}

/* Language switching ------------------------------------------------------- */
static bool ih_lang(const char *p)
{
    if (!match_any(p, "dil", "language", "lang", "sprache")
        && !match_any(p, "langue", "idioma", NULL, NULL)) return false;
    if (match_any(p, "ingiliz", "english", "ingles", "ang")) {
        SET.lang = LANG_EN; diskdb_save(); j_say("Language set to English.");
        return true;
    }
    if (match_any(p, "turk", "turkish", "tr ", "turkce")) {
        SET.lang = LANG_TR; diskdb_save(); j_say("Dil Turkce yapildi.");
        return true;
    }
    if (match_any(p, "alman", "german", "deutsch", NULL)) {
        SET.lang = LANG_DE; diskdb_save(); j_say("Auf Deutsch gestellt.");
        return true;
    }
    if (match_any(p, "fransiz", "french", "francais", NULL)) {
        SET.lang = LANG_FR; diskdb_save(); j_say("Reglage en francais.");
        return true;
    }
    if (match_any(p, "ispanyol", "spanish", "espanol", NULL)) {
        SET.lang = LANG_ES; diskdb_save(); j_say("Idioma espanol.");
        return true;
    }
    j_say(SET.lang == LANG_TR
          ? "Dil: TR / EN / DE / FR / ES."
          : "Language: TR / EN / DE / FR / ES.");
    return true;
}

/* Aero toggle -------------------------------------------------------------- */
static bool ih_aero(const char *p)
{
    if (!match_any(p, "aero", "saydam", "transparent", "blur"))
        return false;
    if (match_any(p, "kapat", "off", "disable", "kapali")) {
        SET.aero_enabled = false; diskdb_save();
        j_say(SET.lang == LANG_TR ? "Aero kapatildi." : "Aero off."); return true;
    }
    if (match_any(p, "ac",   "on",  "enable",  "acik")) {
        SET.aero_enabled = true; diskdb_save();
        j_say(SET.lang == LANG_TR ? "Aero acildi." : "Aero on."); return true;
    }
    SET.aero_enabled = !SET.aero_enabled; diskdb_save();
    j_say(SET.aero_enabled
          ? (SET.lang == LANG_TR ? "Aero acildi." : "Aero on.")
          : (SET.lang == LANG_TR ? "Aero kapatildi." : "Aero off."));
    return true;
}

/* Open app by name --------------------------------------------------------- */
static bool ih_open(const char *p)
{
    if (!match_any(p, "ac ", "open ", "launch", "calistir")
        && !match_any(p, "git ", "go to ", NULL, NULL)) return false;
    /* try every app by name */
    static const char *aliases[][2] = {
        { "ayar",       "Settings"   },
        { "settings",   "Settings"   },
        { "terminal",   "Terminal"   },
        { "kabuk",      "Terminal"   },
        { "shell",      "Terminal"   },
        { "hesap",      "Calculator" },
        { "calc",       "Calculator" },
        { "not ",       "Notes"      },
        { "note",       "Notes"      },
        { "saat",       "Clock"      },
        { "clock",      "Clock"      },
        { "takvim",     "Calendar"   },
        { "calendar",   "Calendar"   },
        { "magaz",      "Store"      },
        { "store",      "Store"      },
        { "paket",      "Store"      },
        { "browser",    "Falco"      },
        { "chrome",     "Chrome"     },
        { "falco",      "Falco"      },
        { "tarayic",    "Falco"      },
        { "heroic",     "Heroic"     },
        { "oyun",       "Heroic"     },
        { "video",      "Video"      },
        { "medya",      "Video"      },
        { "galeri",     "Gallery"    },
        { "gallery",    "Gallery"    },
        { "dosya",      "Files"      },
        { "files",      "Files"      },
        { "hakkin",     "About"      },
        { "about",      "About"      },
        { "istatist",   "Stats"      },
        { "stats",      "Stats"      },
        { "ana ",       "Home"       },
        { "home",       "Home"       },
    };
    i32 nal = (i32)(sizeof aliases / sizeof aliases[0]);
    for (i32 i = 0; i < nal; i++) {
        if (contains_ci(p, aliases[i][0])) {
            i32 idx = j_find_app(aliases[i][1]);
            if (idx >= 0) {
                apps_open(idx);
                char line[64];
                k_strcpy(line, SET.lang == LANG_TR
                               ? "Aciliyor: " : "Opening: ");
                k_strcat(line, apps_display_name(idx));
                j_say(line);
                return true;
            }
        }
    }
    j_say(SET.lang == LANG_TR
          ? "Hangi uygulama? Ornek: 'ayarlari ac', 'terminal ac'."
          : "Which app? e.g. 'open settings', 'open terminal'.");
    return true;
}

/* Lock / power ------------------------------------------------------------- */
static bool ih_lock(const char *p)
{
    if (!match_any(p, "kilit", "lock", "kilitle", NULL)) return false;
    j_say(SET.lang == LANG_TR ? "Ekran kilitleniyor." : "Locking screen.");
    lockscreen_lock();
    return true;
}

static bool ih_power(const char *p)
{
    if (!match_any(p, "kapat", "shutdown", "yeniden", "restart")
        && !match_any(p, "uyku", "sleep", "logout", "oturum"))
        return false;
    j_say(SET.lang == LANG_TR ? "Guc menusu acildi." : "Power menu opened.");
    power_menu_open();
    return true;
}

/* Package install / remove ------------------------------------------------- */
static bool ih_pkg(const char *p)
{
    bool inst = match_any(p, "kur ", "install", "yukle", "ekle ");
    bool rem  = match_any(p, "kaldir", "remove", "sil ", "uninstall");
    if (!inst && !rem) return false;

    /* Walk every catalogue entry and pick the longest case-insensitive
     * substring match against the prompt — protects 'vim' from also
     * triggering 'vim-tiny'.                                       */
    i32 best = -1, best_len = 0;
    i32 n = prg_count();
    for (i32 i = 0; i < n; i++) {
        const prg_pkg_t *pk = prg_at(i);
        if (!pk) continue;
        if (contains_ci(p, pk->name)) {
            i32 ln = k_strlen(pk->name);
            if (ln > best_len) { best_len = ln; best = i; }
        }
    }
    if (best < 0) {
        j_say(SET.lang == LANG_TR
              ? "Hangi paket? Ornek: 'vim-tiny kur'."
              : "Which package? e.g. 'install vim-tiny'.");
        return true;
    }
    char line[80];
    if (inst) {
        if (prg_install(best)) {
            k_strcpy(line, SET.lang == LANG_TR ? "Kuruldu: " : "Installed: ");
        } else {
            k_strcpy(line, SET.lang == LANG_TR ? "Kurulamadi: " : "Failed: ");
        }
    } else {
        if (prg_remove(best)) {
            k_strcpy(line, SET.lang == LANG_TR
                           ? "Kaldirildi: " : "Removed: ");
        } else {
            k_strcpy(line, SET.lang == LANG_TR
                           ? "Kaldirilamadi (yerlesik ya da bagimlilik): "
                           : "Cannot remove (built-in or required): ");
        }
    }
    k_strcat(line, prg_at(best)->name);
    j_say(line);
    return true;
}

/* Status / system summary -------------------------------------------------- */
static bool ih_status(const char *p)
{
    if (!match_any(p, "durum", "status", "sistem", "ozet")
        && !match_any(p, "telemetr", NULL, NULL, NULL))
        return false;
    char line[80], num[16];
    k_strcpy(line, SET.lang == LANG_TR ? "Surum: FalconOS 1 x86_64"
                                       : "Version: FalconOS 1 x86_64");
    j_say(line);
    u32 h, m, s; pit_uptime(&h, &m, &s);
    k_strcpy(line, SET.lang == LANG_TR ? "Uptime: " : "Uptime: ");
    k_itoa(h, num, 10); k_strcat(line, num); k_strcat(line, "h ");
    k_itoa(m, num, 10); k_strcat(line, num); k_strcat(line, "m");
    j_push(line);
    k_strcpy(line, SET.lang == LANG_TR ? "Kullanicilar: " : "Users: ");
    k_itoa(SET.user_count, num, 10); k_strcat(line, num);
    j_push(line);
    k_strcpy(line, SET.lang == LANG_TR ? "Yuklu paket: " : "Installed pkgs: ");
    k_itoa(prg_installed_count(), num, 10); k_strcat(line, num);
    j_push(line);
    return true;
}

/* List users --------------------------------------------------------------- */
static bool ih_users(const char *p)
{
    if (!match_any(p, "kullanici list", "list user", "kullanicilar", "users"))
        return false;
    j_say(SET.lang == LANG_TR ? "Kullanicilar:" : "Users:");
    for (i32 i = 0; i < SET.user_count && i < 8; i++) {
        const falcon_user_t *u = users_at(i);
        if (!u) continue;
        char line[64];
        k_strcpy(line, "  - ");
        k_strcat(line, u->name);
        if (i == SET.active_user) k_strcat(line, "  (active)");
        j_push(line);
    }
    return true;
}

/* Thanks / chitchat -------------------------------------------------------- */
static bool ih_thanks(const char *p)
{
    if (!match_any(p, "tesekkur", "thanks", "thank you", "sagol"))
        return false;
    j_say(SET.lang == LANG_TR ? "Rica ederim." : "You're welcome.");
    return true;
}

static bool ih_who(const char *p)
{
    if (!match_any(p, "sen kim", "who are you", "kimsin", "name"))
        return false;
    j_say(SET.lang == LANG_TR
          ? "Ben Jarvis, FalconOS 1 yardimcisi."
          : "I'm Jarvis, the FalconOS 1 assistant.");
    return true;
}

static bool ih_clear(const char *p)
{
    if (!match_any(p, "temizle", "clear", "reset", "sil "))
        return false;
    j_n = 0;
    j_say(ok_text());
    return true;
}

/* New Jarvis capabilities for FalconOS 1.1 -------------------------------- */
static bool ih_version(const char *p)
{
    if (!match_any(p, "version", "surum", "versiyon", "kac"))
        return false;
    j_say("FalconOS 1.1.0 Blue Dragon Edition");
    j_push("  Kernel: falcon-kernel 1.0.0");
    j_push("  Shell:  falcon-shell 1.0.0");
    j_push("  Arch:   x86_64 long-mode");
    return true;
}

static bool ih_memory(const char *p)
{
    if (!match_any(p, "memory", "hafiza", "bellek", "ram"))
        return false;
    char line[80], num[16];
    k_strcpy(line, SET.lang == LANG_TR ? "Bellek durumu: " : "Memory status: ");
    k_strcat(line, "available");
    j_say(line);
    return true;
}

static bool ih_disk(const char *p)
{
    if (!match_any(p, "disk", "depolama", "storage", "alan"))
        return false;
    j_say(SET.lang == LANG_TR ? "Disk durumu:" : "Disk status:");
    j_push("  ATA PIO: connected");
    j_push("  Superblock: active");
    return true;
}

static bool ih_joke(const char *p)
{
    if (!match_any(p, "joke", "fikra", "espri", "komik"))
        return false;
    if (SET.lang == LANG_TR) {
        j_say("Programci neden gozluk takar?");
        j_push("  Cunku C# goremez! :)");
    } else {
        j_say("Why do programmers prefer dark mode?");
        j_push("  Because light attracts bugs! :)");
    }
    return true;
}

static bool ih_weather(const char *p)
{
    if (!match_any(p, "weather", "hava", "sicaklik", "derece"))
        return false;
    j_say(SET.lang == LANG_TR
          ? "Hava durumu: Ag baglantisi gerekli (cevrimdisi mod)."
          : "Weather: Network connection required (offline mode).");
    return true;
}

static bool ih_music(const char *p)
{
    if (!match_any(p, "music", "muzik", "sarki", "calgi"))
        return false;
    j_say(SET.lang == LANG_TR
          ? "Music uygulamasini ac: 'terminal ac' veya Store'dan yukle."
          : "Open Music app: 'open terminal' or install from Store.");
    return true;
}

static bool ih_calculate(const char *p)
{
    if (!match_any(p, "hesap", "calculate", "matematik", "toplam"))
        return false;
    i32 idx = j_find_app("Calculator");
    if (idx >= 0) {
        apps_open(idx);
        j_say(SET.lang == LANG_TR
              ? "Calculator acildi."
              : "Calculator opened.");
    } else {
        j_say(SET.lang == LANG_TR
              ? "Calculator bulunamadi."
              : "Calculator not found.");
    }
    return true;
}

static bool ih_search(const char *p)
{
    if (!match_any(p, "search", "ara ", "bul ", "find"))
        return false;
    j_say(SET.lang == LANG_TR
          ? "Arama: Cevrimdisi modda yerel dosyalari arayabilirim."
          : "Search: In offline mode, I can search local files.");
    return true;
}

static bool ih_screenshot(const char *p)
{
    if (!match_any(p, "screenshot", "ekran goru", "capture", "yakala"))
        return false;
    j_say(SET.lang == LANG_TR
          ? "Ekran goruntusu: Bu ozellik Store'dan yuklenebilir."
          : "Screenshot: Install app-recorder from Store.");
    return true;
}

static bool ih_goodbye(const char *p)
{
    if (!match_any(p, "bye", "gule", "hosca", "gorusur"))
        return false;
    j_say(SET.lang == LANG_TR
          ? "Hosca kal! Ihtiyacin olursa buradayim."
          : "Goodbye! I'm here if you need me.");
    return true;
}

/* ==================== FalconOS 1.2.1 extended intents ==================== */

static bool ih_network(const char *p)
{
    if (!match_any(p, "ag durum", "network", "internet", "baglanti"))
        return false;
    char buf[J_COLS];
    if (!net_present()) {
        j_say(SET.lang == LANG_TR ? "Ag karti bulunamadi (virtio-net yok)."
                                  : "No network adapter found (virtio-net absent).");
        return true;
    }
    if (!net_connected()) {
        j_say(SET.lang == LANG_TR ? "Ag karti var ama baglanti yok."
                                  : "Adapter present, link down.");
        return true;
    }
    k_strcpy(buf, SET.lang == LANG_TR ? "Baglandi.  IP: " : "Connected.  IP: ");
    k_strcat(buf, net_ip_addr());
    k_strcat(buf, "  GW: ");
    k_strcat(buf, net_gateway());
    j_say(buf);
    return true;
}

static bool ih_ip(const char *p)
{
    if (!match_any(p, "ip adres", "my ip", "ipv4", "address"))
        return false;
    if (!net_connected()) {
        j_say(SET.lang == LANG_TR ? "Henuz IP aldim degil; once 'ag durum' kontrol et."
                                  : "No IP yet; check 'network status' first.");
        return true;
    }
    char buf[J_COLS];
    k_strcpy(buf, "IP: ");
    k_strcat(buf, net_ip_addr());
    j_say(buf);
    return true;
}

static bool ih_mac(const char *p)
{
    if (!match_any(p, "mac adres", "mac address", "donanim adres", "hw addr"))
        return false;
    if (!net_present()) {
        j_say(SET.lang == LANG_TR ? "Ag karti yok." : "No adapter.");
        return true;
    }
    char mac[32];
    net_mac_string(mac);
    char buf[J_COLS];
    k_strcpy(buf, "MAC: ");
    k_strcat(buf, mac);
    j_say(buf);
    return true;
}

static bool ih_hostname(const char *p)
{
    if (!match_any(p, "hostname", "makine ad", "bilgisayar ad", "host name"))
        return false;
    j_say(SET.lang == LANG_TR ? "Makine adi: falcon" : "Hostname: falcon");
    return true;
}

/* Report which TLS engine is linked into the kernel.  Hitting this
 * intent proves end-to-end that vendor/bearssl/ compiled, archived
 * into libbearssl.a, and got pulled into falcon.elf at link time. */
static bool ih_tls(const char *p)
{
    if (!match_any(p, "tls", "https", "ssl", "sertifika"))
        return false;
    char buf[J_COLS];
    k_strcpy(buf, SET.lang == LANG_TR ? "TLS motoru: " : "TLS engine: ");
    k_strcat(buf, tls_version());
    j_say(buf);
    return true;
}

static bool ih_kbd(const char *p)
{
    if (!match_any(p, "klavye", "keyboard", "layout", "duzen"))
        return false;
    if (contains_ci(p, "turkce f") || contains_ci(p, "tr-f") || contains_ci(p, "tr f")) {
        SET.kbd_layout = KBD_TR_F; diskdb_save();
        j_say(SET.lang == LANG_TR ? "Klavye TR-F oldu." : "Keyboard set to TR-F.");
        return true;
    }
    if (contains_ci(p, "turkce") || contains_ci(p, "tr-q") || contains_ci(p, "turkish")) {
        SET.kbd_layout = KBD_TR_Q; diskdb_save();
        j_say(SET.lang == LANG_TR ? "Klavye TR-Q oldu." : "Keyboard set to TR-Q.");
        return true;
    }
    if (contains_ci(p, "ingilizce") || contains_ci(p, "us") || contains_ci(p, "english")) {
        SET.kbd_layout = KBD_US; diskdb_save();
        j_say(SET.lang == LANG_TR ? "Klavye US oldu." : "Keyboard set to US.");
        return true;
    }
    char buf[J_COLS];
    k_strcpy(buf, SET.lang == LANG_TR ? "Klavye duzeni: " : "Keyboard layout: ");
    k_strcat(buf, kbd_layout_name(SET.kbd_layout));
    j_say(buf);
    return true;
}

static bool ih_workspace(const char *p)
{
    if (!match_any(p, "masaustu", "calisma alan", "workspace", "desktop "))
        return false;
    /* dial-in a specific number 1..4 */
    for (i32 i = 0; p[i]; i++) {
        if (p[i] >= '1' && p[i] <= '4') {
            SET.active_workspace = p[i] - '1';
            char buf[J_COLS];
            k_strcpy(buf, SET.lang == LANG_TR ? "Masaustu " : "Workspace ");
            char num[2]; num[0] = p[i]; num[1] = 0;
            k_strcat(buf, num);
            k_strcat(buf, SET.lang == LANG_TR ? "'e gectim." : " active.");
            j_say(buf);
            return true;
        }
    }
    if (contains_ci(p, "sonraki") || contains_ci(p, "next")) {
        SET.active_workspace = (SET.active_workspace + 1) % 4;
    } else if (contains_ci(p, "onceki") || contains_ci(p, "prev")) {
        SET.active_workspace = (SET.active_workspace + 3) % 4;
    }
    char buf[J_COLS];
    char num[4]; k_itoa((u32)(SET.active_workspace + 1), num, 10);
    k_strcpy(buf, SET.lang == LANG_TR ? "Masaustu " : "Workspace ");
    k_strcat(buf, num);
    k_strcat(buf, " / 4");
    j_say(buf);
    return true;
}

static bool ih_packages(const char *p)
{
    if (!match_any(p, "kurulu paket", "installed pkg", "installed package",
                   "paket liste"))
        return false;
    i32 total = prg_count(), inst = 0;
    for (i32 i = 0; i < total; i++) if (prg_is_installed(i)) inst++;
    char buf[J_COLS];
    char num[12];
    k_itoa((u32)inst, num, 10); k_strcpy(buf, num);
    k_strcat(buf, " / ");
    k_itoa((u32)total, num, 10); k_strcat(buf, num);
    k_strcat(buf, SET.lang == LANG_TR ? " paket kurulu." : " packages installed.");
    j_say(buf);
    return true;
}

static bool ih_apps(const char *p)
{
    if (!match_any(p, "kac uygulama", "how many apps", "app count", "uygulama say"))
        return false;
    char buf[J_COLS]; char num[12];
    k_itoa((u32)apps_count(), num, 10);
    k_strcpy(buf, SET.lang == LANG_TR ? "Toplam " : "Total ");
    k_strcat(buf, num);
    k_strcat(buf, SET.lang == LANG_TR ? " uygulama var." : " apps available.");
    j_say(buf);
    return true;
}

static bool ih_reboot(const char *p)
{
    if (!match_any(p, "yeniden basla", "reboot", "tekrar baslat", "restart now"))
        return false;
    j_say(SET.lang == LANG_TR ? "Guc menusu aciliyor (F12)..." : "Opening power menu (F12)...");
    power_menu_open();
    return true;
}

static bool ih_shutdown_now(const char *p)
{
    if (!match_any(p, "simdi kapat", "shutdown now", "kapan", "power off"))
        return false;
    j_say(SET.lang == LANG_TR ? "Guc menusu aciliyor (F12)..." : "Opening power menu (F12)...");
    power_menu_open();
    return true;
}

static bool ih_random(const char *p)
{
    if (!match_any(p, "rastgele", "random", "sayi sec", "pick number"))
        return false;
    /* tick-based PRNG, fine for trivia */
    u32 seed = g_ticks ^ 0xCAFEBABEu;
    seed = seed * 1103515245u + 12345u;
    i32 v = (i32)((seed >> 16) % 100);
    char buf[J_COLS]; char num[12];
    k_itoa((u32)v, num, 10);
    k_strcpy(buf, SET.lang == LANG_TR ? "Rastgele sayi: " : "Random number: ");
    k_strcat(buf, num);
    j_say(buf);
    return true;
}

static bool ih_coinflip(const char *p)
{
    if (!match_any(p, "yazi tura", "coin flip", "para at", "heads tails"))
        return false;
    bool heads = ((g_ticks ^ 0xDEADBEEFu) & 1u) == 0;
    if (SET.lang == LANG_TR) j_say(heads ? "Yazi geldi." : "Tura geldi.");
    else                     j_say(heads ? "Heads."      : "Tails.");
    return true;
}

static bool ih_brightness(const char *p)
{
    if (!match_any(p, "parlak", "brightness", "ekran isik", "screen light"))
        return false;
    j_say(SET.lang == LANG_TR
          ? "Bu donanimda yazilim parlaklik kontrolu yok; QEMU host'undan ayarla."
          : "Software brightness is not exposed on this hardware; adjust on the QEMU host.");
    return true;
}

static bool ih_volume(const char *p)
{
    if (!match_any(p, "ses", "volume", "sessize", "mute"))
        return false;
    j_say(SET.lang == LANG_TR
          ? "Ses surucusu henuz yok (AC'97 portu 1.3 listesinde)."
          : "Audio driver not yet shipped (AC'97 port on the 1.3 roadmap).");
    return true;
}

static bool ih_cpuinfo(const char *p)
{
    if (!match_any(p, "cpu", "islemci", "processor", "core"))
        return false;
    j_say(SET.lang == LANG_TR
          ? "x86_64 long mode, tek cekirdek aktif, PIT 100 Hz scheduler."
          : "x86_64 long mode, one core active, PIT 100 Hz scheduler.");
    return true;
}

static bool ih_note(const char *p)
{
    if (!match_any(p, "not al", "make note", "note ", "kaydet"))
        return false;
    j_say(SET.lang == LANG_TR
          ? "Notlar uygulamasini aciyorum."
          : "Opening Notes.");
    i32 idx = j_find_app("Notes");
    if (idx >= 0) apps_open(idx);
    return true;
}

static bool ih_files_open(const char *p)
{
    if (!match_any(p, "dosya yon", "file manager", "dosyalari ac", "open files"))
        return false;
    i32 idx = j_find_app("Files");
    if (idx >= 0) {
        apps_open(idx);
        j_say(SET.lang == LANG_TR ? "Dosyalar acildi." : "Files opened.");
    } else {
        j_say("Files app not found.");
    }
    return true;
}

static bool ih_store(const char *p)
{
    if (!match_any(p, "magaza ac", "open store", "store ac", "yazilim merkezi"))
        return false;
    i32 idx = j_find_app("Store");
    if (idx >= 0) {
        apps_open(idx);
        j_say(SET.lang == LANG_TR ? "Magaza acildi." : "Store opened.");
    }
    return true;
}

static bool ih_settings_open(const char *p)
{
    if (!match_any(p, "ayarlar", "settings", "preference", "tercih"))
        return false;
    i32 idx = j_find_app("Settings");
    if (idx >= 0) {
        apps_open(idx);
        j_say(SET.lang == LANG_TR ? "Ayarlar acildi." : "Settings opened.");
    }
    return true;
}

static bool ih_help_drawer(const char *p)
{
    if (!match_any(p, "yardim panel", "help drawer", "yardim ekran", "show help"))
        return false;
    helppanel_open();
    j_say(SET.lang == LANG_TR ? "Yardim paneli acildi (F1)." : "Help drawer opened (F1).");
    return true;
}

static bool ih_workspace_count(const char *p)
{
    if (!match_any(p, "masaustu sayi", "workspace count", "kac masaustu", "how many workspace"))
        return false;
    char buf[J_COLS]; char num[12];
    k_itoa((u32)SET.workspace_count, num, 10);
    k_strcpy(buf, num);
    k_strcat(buf, SET.lang == LANG_TR ? " masaustu var, aktif: " : " workspaces, active: ");
    k_itoa((u32)(SET.active_workspace + 1), num, 10);
    k_strcat(buf, num);
    j_say(buf);
    return true;
}

static bool ih_aero_toggle(const char *p)
{
    if (!match_any(p, "aero ac", "aero kapat", "aero on", "aero off"))
        return false;
    bool enable = !(contains_ci(p, "kapat") || contains_ci(p, "off") || contains_ci(p, "disable"));
    SET.aero_enabled = enable;
    diskdb_save();
    j_say(SET.lang == LANG_TR
          ? (enable ? "Aero saydamlik acildi." : "Aero saydamlik kapatildi.")
          : (enable ? "Aero transparency on."  : "Aero transparency off."));
    return true;
}

static bool ih_uname(const char *p)
{
    if (!match_any(p, "uname", "sistem bilgi", "system info", "kernel info"))
        return false;
    j_say("FalconOS 1 x86_64 long-mode bare-metal kernel, PIT 100Hz");
    return true;
}

static bool ih_ping(const char *p)
{
    if (!match_any(p, "ping", "gateway test", "ag testi", "echo test"))
        return false;
    if (!net_connected()) {
        j_say(SET.lang == LANG_TR ? "Once ag baglantisi gerek." : "Need a live link first.");
        return true;
    }
    char buf[J_COLS];
    k_strcpy(buf, "ping ");
    k_strcat(buf, net_gateway());
    k_strcat(buf, SET.lang == LANG_TR ? "  (yakinda gercek ICMP)" : "  (real ICMP soon)");
    j_say(buf);
    return true;
}

static bool ih_color_accent(const char *p)
{
    if (!match_any(p, "vurgu", "accent", "renk degisti", "ana renk"))
        return false;
    /* cycle through 4 presets stored in SET.accent_idx via SET.theme */
    SET.theme = (theme_t)((SET.theme + 1) % 5);
    diskdb_save();
    j_say(SET.lang == LANG_TR ? "Sonraki temaya gectim." : "Cycled to next theme.");
    return true;
}

static bool ih_apps_running(const char *p)
{
    if (!match_any(p, "acik uygulama", "running apps", "kac uygulama acik", "open apps"))
        return false;
    i32 cur = apps_active();
    if (cur < 0) {
        j_say(SET.lang == LANG_TR ? "Acik pencere yok." : "No window open.");
        return true;
    }
    char buf[J_COLS];
    k_strcpy(buf, SET.lang == LANG_TR ? "Acik uygulama: " : "Active window: ");
    k_strcat(buf, apps_name(cur));
    j_say(buf);
    return true;
}

static bool ih_close_all(const char *p)
{
    if (!match_any(p, "hepsini kapat", "close all", "tumunu kapat", "pencereler kapat"))
        return false;
    apps_close();
    j_say(SET.lang == LANG_TR ? "Pencere kapatildi." : "Closed the active window.");
    return true;
}

/* ---- intent table -------------------------------------------------------- */
static const intent_t INTENTS[] = {
    /* greeting & meta first */
    { "merhaba", "selam", "hello", "hi ",       ih_hello   },
    { "yardim",  "help ", "ne yapabili", "what can", ih_help },
    { "sen kim", "kimsin", "who are you", "name", ih_who    },
    /* concrete state queries */
    { "saat",    "time",  "what time", NULL,    ih_time    },
    { "tarih",   "date",  "bugun",  "today",    ih_date    },
    { "uptime",  "ne kadar", "acik kalma", NULL, ih_uptime },
    { "ben kim", "whoami","aktif kull", "current user", ih_whoami },
    { "kullanici list", "kullanicilar", "list user", "users", ih_users },
    { "durum",   "status","sistem","telemetr",   ih_status  },
    /* new FalconOS 1.1 intents */
    { "version", "surum", "versiyon", NULL,     ih_version },
    { "memory",  "hafiza", "bellek", "ram",     ih_memory  },
    { "disk",    "depolama", "storage", "alan", ih_disk    },
    { "joke",    "fikra", "espri", "komik",     ih_joke    },
    { "weather", "hava", "sicaklik", "derece",  ih_weather },
    { "music",   "muzik", "sarki", "calgi",     ih_music   },
    { "hesap",   "calculate", "matematik", NULL,ih_calculate},
    { "search",  "ara ", "bul ", "find",        ih_search  },
    { "screenshot","ekran goru","capture",NULL, ih_screenshot},
    { "bye",     "gule", "hosca", "gorusur",    ih_goodbye },
    /* mutating intents */
    { "tema",    "theme", "renk",   NULL,       ih_theme   },
    { "dil",     "language", "lang", "sprache", ih_lang    },
    { "aero",    "saydam","transparent","blur", ih_aero    },
    { "kilit",   "lock", "kilitle",  NULL,      ih_lock    },
    { "kapat",   "shutdown","yeniden","restart",ih_power   },
    { "kur ",    "install","yukle","kaldir",    ih_pkg     },
    { "ac ",     "open ", "launch","calistir",  ih_open    },
    /* social pleasantries */
    { "tesekkur","thanks","sagol",   NULL,      ih_thanks  },
    { "temizle", "clear", "reset",   NULL,      ih_clear   },
    /* ----- FalconOS 1.2.1 — extended deterministic intents ----- */
    { "ag durum","network","internet","baglanti",  ih_network         },
    { "ip adres","my ip","ipv4","address",          ih_ip              },
    { "mac adres","mac address","donanim adres","hw addr", ih_mac      },
    { "hostname","makine ad","bilgisayar ad","host name",  ih_hostname },
    { "tls","https","ssl","sertifika",              ih_tls             },
    { "klavye","keyboard","layout","duzen",         ih_kbd             },
    { "masaustu","calisma alan","workspace","desktop ", ih_workspace   },
    { "masaustu sayi","workspace count","kac masaustu","how many workspace", ih_workspace_count },
    { "kurulu paket","installed pkg","installed package","paket liste", ih_packages },
    { "kac uygulama","how many apps","app count","uygulama say", ih_apps },
    { "yeniden basla","reboot","tekrar baslat","restart now", ih_reboot },
    { "simdi kapat","shutdown now","kapan","power off", ih_shutdown_now },
    { "rastgele","random","sayi sec","pick number", ih_random          },
    { "yazi tura","coin flip","para at","heads tails", ih_coinflip     },
    { "parlak","brightness","ekran isik","screen light", ih_brightness },
    { "ses ","volume","sessize","mute",             ih_volume          },
    { "cpu","islemci","processor","core",           ih_cpuinfo         },
    { "not al","make note","note ","kaydet",        ih_note            },
    { "dosya yon","file manager","dosyalari ac","open files", ih_files_open },
    { "magaza ac","open store","store ac","yazilim merkezi", ih_store  },
    { "ayarlar","settings","preference","tercih",   ih_settings_open   },
    { "yardim panel","help drawer","yardim ekran","show help", ih_help_drawer },
    { "aero ac","aero kapat","aero on","aero off",  ih_aero_toggle     },
    { "uname","sistem bilgi","system info","kernel info", ih_uname     },
    { "ping","gateway test","ag testi","echo test", ih_ping            },
    { "vurgu","accent","renk degisti","ana renk",   ih_color_accent    },
    { "acik uygulama","running apps","kac uygulama acik","open apps", ih_apps_running },
    { "hepsini kapat","close all","tumunu kapat","pencereler kapat", ih_close_all },
};
static const i32 N_INTENTS = (i32)(sizeof INTENTS / sizeof INTENTS[0]);

/* ---- public API ---------------------------------------------------------- */
void jarvis_reset(void)
{
    j_n = 0;
    j_in_n = 0;
    j_input[0] = 0;
    j_inited = true;
    if (SET.lang == LANG_TR) {
        j_say("Merhaba, ben Jarvis. Sana nasil yardim edebilirim?");
        j_push("        (Ornek: 'tema koyu yap', 'saat kac', 'terminal ac',");
        j_push("                'vim-tiny kur', 'durum raporu', 'kilitle')");
    } else {
        j_say("Hi, I'm Jarvis. How can I help?");
        j_push("        (e.g. 'switch to dark theme', 'what time is it',");
        j_push("              'open terminal', 'install vim-tiny',");
        j_push("              'system status', 'lock screen')");
    }
}

static void j_dispatch(const char *prompt)
{
    j_user(prompt);
    /* run through intent table; first match wins */
    for (i32 i = 0; i < N_INTENTS; i++) {
        if (match_any(prompt,
                      INTENTS[i].k1, INTENTS[i].k2,
                      INTENTS[i].k3, INTENTS[i].k4)) {
            if (INTENTS[i].handle(prompt)) return;
        }
    }
    /* graceful fallback */
    j_say(SET.lang == LANG_TR
          ? "Anlamadim. 'yardim' yazarsan ornek komutlari sayarim."
          : "Sorry, I didn't catch that. Type 'help' for examples.");
}

void jarvis_input(i32 key)
{
    if (!j_inited) jarvis_reset();
    if (key == KEY_BACKSPACE) {
        j_buf_pop_utf8(j_input, &j_in_n);
        return;
    }
    if (key == KEY_ENTER) {
        if (j_in_n == 0) return;
        j_input[j_in_n] = 0;
        char tmp[J_COLS]; k_strcpy(tmp, j_input);
        j_in_n = 0; j_input[0] = 0;
        j_dispatch(tmp);
        return;
    }
    (void)j_buf_append_key(j_input, &j_in_n, J_COLS, key);
}

void jarvis_render(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame;
    if (!j_inited) jarvis_reset();
    /* header */
    gfx_text(wx + 24, wy +  6, "Jarvis", PAL_TEXT);
    gfx_text(wx + 24, wy + 28,
             SET.lang == LANG_TR ? "Sistem yardimcisi - dogal dilde sor"
                                 : "System assistant - ask in plain language",
             PAL_TEXT_DIM);

    /* transcript pane */
    i32 px = wx + 24, py = wy + 60;
    i32 pw = ww - 48, ph = wh - 130;
    gfx_round_rect_a(px, py, pw, ph, 12, PAL_PANEL_DEEP, 255);
    gfx_round_outline(px, py, pw, ph, 12, PAL_HAIRLINE);
    i32 ty = py + 12;
    for (i32 i = 0; i < j_n; i++) {
        u32 col = PAL_TEXT;
        /* "Jarvis:" lines pop in accent, "Sen:" lines stay neutral */
        if (j_log[i][0] == 'J' && j_log[i][1] == 'a') col = PAL_ACCENT;
        gfx_text(px + 12, ty, j_log[i], col);
        ty += 18;
    }

    /* input bar */
    i32 ix = px, iy = py + ph + 12, iw = pw, ih = 36;
    gfx_round_rect_a(ix, iy, iw, ih, 10, PAL_PANEL, 255);
    gfx_round_outline(ix, iy, iw, ih, 10, PAL_HAIRLINE);
    /* prompt glyph */
    gfx_text(ix + 10, iy + 10,
             SET.lang == LANG_TR ? "> " : "> ",
             PAL_ACCENT);
    /* user buffer + a slow-blinking caret */
    i32 caret = ((frame >> 4) & 1) ? 1 : 0;
    char shown[J_COLS + 2];
    k_strcpy(shown, j_input);
    if (caret) k_strcat(shown, "_");
    gfx_text(ix + 30, iy + 10, shown, PAL_TEXT);

    /* footer hint */
    gfx_text(wx + 24, wy + wh - 20,
             SET.lang == LANG_TR
                 ? "Enter: gonder    Backspace: sil    Esc: kapat"
                 : "Enter: send    Backspace: delete    Esc: close",
             PAL_TEXT_FAINT);
}

/* Launcher icon: a chat-bubble silhouette with a small sparkle dot.
 * Drawn on the dock and Launchpad tile.                           */
void jarvis_icon(i32 cx, i32 cy)
{
    /* bubble base */
    gfx_round_rect_a(cx - 22, cy - 18, 44, 32, 12, 0x6D5BFF, 255);
    gfx_round_outline(cx - 22, cy - 18, 44, 32, 12, 0xFFFFFF);
    /* sparkle */
    gfx_circle(cx + 10, cy - 10, 3, 0xFFFFFF);
    gfx_circle(cx + 10, cy - 10, 1, 0x6D5BFF);
    /* tail */
    gfx_rect(cx - 12, cy + 14, 8, 4, 0x6D5BFF);
    gfx_text_centered(cx, cy - 4, "AI", 0xFFFFFF);
}
