/* =============================================================================
 *  FalconOS — public kernel header  (FalconOS 1)
 * =============================================================================
 *  All public types, theme palette functions and module-level entry points
 *  live here so the rest of the kernel can stay surgically small.
 * ============================================================================= */
#ifndef FALCON_H
#define FALCON_H

/* ---- fixed-width primitives (no libc) ------------------------------------- */
typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned int        u32;
typedef unsigned long long  u64;
typedef signed char         i8;
typedef short               i16;
typedef int                 i32;
typedef long long           i64;
typedef unsigned long       uintptr_t;
/* Short alias used in low-level code that has to round-trip a memory
 * address through an integer (e.g. dev memory inspector, REPL peek).
 * uintptr_t already captures word-width on 32- and 64-bit builds. */
typedef uintptr_t           uptr;
typedef _Bool               bool;
#define true                1
#define false               0
#define NULL                ((void *)0)

#ifndef FB_W
#define FB_W 1920
#endif
#ifndef FB_H
#define FB_H 1080
#endif

/* ---- framebuffer ---------------------------------------------------------- */
typedef struct {
    u32 *pixels;
    u32  width;
    u32  height;
    u32  pitch;
    u8   bpp;
} fb_t;

extern fb_t FB;
u32  gfx_back_w(void);
u32  gfx_back_h(void);

/* ---- theme system (FalconOS 1) ------------------------------------------- */
/*  Palette is selected at runtime; mutate SET.theme and the next frame
 *  re-renders in the new palette. THEME_LIGHT(0) and THEME_DARK(1) keep
 *  their v5 numeric values so existing on-disk SET records still load. */
typedef enum {
    THEME_LIGHT    = 0,    /* Lumen      - off-white macOS-Big-Sur            */
    THEME_DARK     = 1,    /* Nox        - dark counterpart                   */
    THEME_LIQUID   = 2,    /* Liquid     - frosted-glass / aqua / max-blur     */
    THEME_NORDIC   = 3,    /* Nordic     - cool blue-grey, low-contrast        */
    THEME_ROSEGOLD = 4,    /* Rose Gold  - warm pink-gold                     */
    THEME_COUNT
} theme_t;
typedef enum {
    ACC_BLUE = 0,
    ACC_PURPLE,
    ACC_GREEN,
    ACC_PINK,
    ACC_GRAPHITE,
    ACC_COUNT
} accent_t;
typedef enum {
    LANG_TR = 0,    /* Turkce       */
    LANG_EN = 1,    /* English      */
    LANG_DE = 2,    /* Deutsch      */
    LANG_FR = 3,    /* Francais     */
    LANG_ES = 4,    /* Espanol      */
    LANG_COUNT,
} lang_t;
typedef enum {
    KBD_TR_Q = 0,    /* Türkçe Q (default for TR users)                  */
    KBD_TR_F,        /* Türkçe F                                          */
    KBD_US,          /* US QWERTY                                         */
    KBD_COUNT
} kbd_layout_t;

/* Light theme ("Lumen") — 0xRRGGBB ----------------------------------------- */
#define COL_BG_TOP      0xEAF0F8
#define COL_BG_BOT      0xC8D5E6
#define COL_BG_HINT     0xA8BBD3
#define COL_PANEL       0xFFFFFF
#define COL_PANEL_HI    0xD8E1EC
#define COL_PANEL_DEEP  0xF1F4F9
#define COL_TEXT        0x14181F
#define COL_TEXT_DIM    0x6E7884
#define COL_TEXT_FAINT  0xA3ACB7
#define COL_ACCENT      0x3070FF
#define COL_ACCENT_DIM  0xB8CDFF
#define COL_OK          0x2BB673
#define COL_WARN        0xF59F1A
#define COL_ERR         0xE53935
#define COL_PURPLE      0xA45EE5
#define COL_TEAL        0x16B5A8
#define COL_GLASS       0xFFFFFF
#define COL_HAIRLINE    0xC4CDD9
#define COL_SHADOW      0x000000

/* Dark theme ("Nox") -------------------------------------------------------- */
#define DCOL_BG_TOP     0x1A1E26
#define DCOL_BG_BOT     0x0E1117
#define DCOL_BG_HINT    0x303744
#define DCOL_PANEL      0x232730
#define DCOL_PANEL_HI   0x3A4150
#define DCOL_PANEL_DEEP 0x1B1F27
#define DCOL_TEXT       0xEBEEF4
#define DCOL_TEXT_DIM   0x99A1AE
#define DCOL_TEXT_FAINT 0x636C79
#define DCOL_HAIRLINE   0x2E3340
#define DCOL_GLASS      0x1F232C

/* Runtime palette accessors (use these in render code, not the constants). */
u32 PAL(u8 role);
#define PAL_BG_TOP      PAL(0)
#define PAL_BG_BOT      PAL(1)
#define PAL_BG_HINT     PAL(2)
#define PAL_PANEL       PAL(3)
#define PAL_PANEL_HI    PAL(4)
#define PAL_PANEL_DEEP  PAL(5)
#define PAL_TEXT        PAL(6)
#define PAL_TEXT_DIM    PAL(7)
#define PAL_TEXT_FAINT  PAL(8)
#define PAL_ACCENT      PAL(9)
#define PAL_ACCENT_DIM  PAL(10)
#define PAL_HAIRLINE    PAL(11)
#define PAL_GLASS       PAL(12)

/* ---- gfx primitives ------------------------------------------------------- */
void gfx_init(void *p, u32 w, u32 h, u32 pitch, u8 bpp);
void gfx_present(void);
void gfx_clear(u32 c);
void gfx_gradient_v(u32 top, u32 bot);
void gfx_wallpaper(void);
void gfx_pixel(i32 x, i32 y, u32 c);
void gfx_pixel_a(i32 x, i32 y, u32 c, u8 a);
void gfx_rect(i32 x, i32 y, i32 w, i32 h, u32 c);
void gfx_rect_a(i32 x, i32 y, i32 w, i32 h, u32 c, u8 a);
void gfx_round_rect(i32 x, i32 y, i32 w, i32 h, i32 r, u32 c);
void gfx_round_rect_a(i32 x, i32 y, i32 w, i32 h, i32 r, u32 c, u8 a);
void gfx_round_outline(i32 x, i32 y, i32 w, i32 h, i32 r, u32 c);
void gfx_round_glass(i32 x, i32 y, i32 w, i32 h, i32 r);
/* Aero (frosted-glass) primitives — separable box blur the live back
 * buffer in a sub-rect, then optionally tint the blurred pixels with a
 * translucent rounded panel. The blur is *real*: it samples the pixels
 * already rendered behind the panel, so wallpaper / widgets / open
 * windows show through softly. Disable globally via SET.aero_enabled
 * (see settings.c) to fall back to the cheaper flat gfx_round_glass.  */
void gfx_blur_rect(i32 x, i32 y, i32 w, i32 h, i32 radius);
void gfx_aero_round_rect(i32 x, i32 y, i32 w, i32 h, i32 r,
                          u32 tint, u8 tint_alpha);
/* FalconOS 1 — visual lift helpers used by panels / windows / dialogs. */
void gfx_round_drop_shadow(i32 x, i32 y, i32 w, i32 h, i32 r);
void gfx_round_inset_highlight(i32 x, i32 y, i32 w, i32 h, i32 r);
void gfx_circle(i32 cx, i32 cy, i32 r, u32 c);
void gfx_circle_a(i32 cx, i32 cy, i32 r, u32 c, u8 alpha);
void gfx_circle_outline(i32 cx, i32 cy, i32 r, u32 c);
void gfx_line(i32 x0, i32 y0, i32 x1, i32 y1, u32 c);
void gfx_text(i32 x, i32 y, const char *s, u32 c);
void gfx_text_centered(i32 cx, i32 y, const char *s, u32 c);
i32  gfx_text_width(const char *s);
void gfx_dim(u8 amount);

/* Apply the current SET.viewport_w / SET.viewport_h letterbox after drawing. */
void gfx_apply_viewport(void);

/* Font tables — generated by tools/genfont.py.
 *  Index 0..94 is ASCII 0x20..0x7E; index 95..106 are the 12 Turkish
 *  letters Ç ç Ğ ğ İ ı Ö ö Ş ş Ü ü in that order.  gfx_text() decodes
 *  UTF-8 from the source string and dispatches into the right entry.   */
#define FONT_GLYPH_COUNT  107
/* 8×16 grayscale (antialiased) — one byte per pixel, row-major.        */
extern const u8 FONT8X16 [FONT_GLYPH_COUNT][128];
/* 16×32 1-bit headline font — two bytes per row, MSB = leftmost pixel. */
extern const u8 FONT16X32[FONT_GLYPH_COUNT][64];

/* Optional headline renderer — same color/coordinate semantics, twice
 * the metrics. Used by titles/installer headings, not by general UI.   */
void gfx_text_lg(i32 x, i32 y, const char *s, u32 c);
void gfx_text_lg_centered(i32 cx, i32 y, const char *s, u32 c);
i32  gfx_text_width_lg(const char *s);

/* ---- keyboard ------------------------------------------------------------- */
i32  kbd_poll(void);
void kbd_drain(void);
void kbd_set_focus_text(bool b);
#define KEY_F1         0x101
#define KEY_F2         0x102
#define KEY_F3         0x103
#define KEY_F4         0x104
#define KEY_F5         0x105
#define KEY_F6         0x106
#define KEY_F7         0x107
#define KEY_F8         0x108
#define KEY_F9         0x109
#define KEY_F10        0x10A
#define KEY_F11        0x10B
#define KEY_F12        0x10C
#define KEY_INSERT     0x110
#define KEY_PRTSC      0x111
#define KEY_ESC        0x01B
#define KEY_TAB        0x009
#define KEY_ENTER      0x00A
#define KEY_BACKSPACE  0x008
#define KEY_UP         0x110
#define KEY_DOWN       0x111
#define KEY_LEFT       0x112
#define KEY_RIGHT      0x113
#define KEY_HOME       0x114
#define KEY_END        0x115
#define KEY_PGUP       0x116
#define KEY_PGDN       0x117
#define KEY_DEL        0x07F
/* UTF-8-capable Turkish letter keys emitted by kbd.c for TR layouts. */
#define KEY_TR_C_CEDILLA_LO 0x200   /* ç */
#define KEY_TR_C_CEDILLA_UP 0x201   /* Ç */
#define KEY_TR_G_BREVE_LO   0x202   /* ğ */
#define KEY_TR_G_BREVE_UP   0x203   /* Ğ */
#define KEY_TR_DOTLESS_I_LO 0x204   /* ı */
#define KEY_TR_DOTTED_I_UP  0x205   /* İ */
#define KEY_TR_O_UMLAUT_LO  0x206   /* ö */
#define KEY_TR_O_UMLAUT_UP  0x207   /* Ö */
#define KEY_TR_S_CEDILLA_LO 0x208   /* ş */
#define KEY_TR_S_CEDILLA_UP 0x209   /* Ş */
#define KEY_TR_U_UMLAUT_LO  0x20A   /* ü */
#define KEY_TR_U_UMLAUT_UP  0x20B   /* Ü */

/* Modifier-state bitmask returned by kbd_mod_state().                    */
#define KMOD_SHIFT  (1u << 0)
#define KMOD_CTRL   (1u << 1)
#define KMOD_ALT    (1u << 2)
#define KMOD_CAPS   (1u << 3)
u32  kbd_mod_state(void);
/* Driver telemetry counters — surfaced in Settings ▸ Drivers.        */
void kbd_stats(u32 *seen, u32 *keys, u32 *drops);

/* ---- mouse ---------------------------------------------------------------- */
void mouse_init(void);
void mouse_get(i32 *x, i32 *y, bool *left);
bool mouse_consume_click(void);
bool mouse_peek_click(void);
void mouse_inject_click(void);
void mouse_drain(void);
bool mouse_consume_right(void);
bool mouse_consume_double(void);
/* Driver telemetry — packet seen / dropped (desync or overflow) / clicks. */
void mouse_stats(u32 *seen, u32 *dropped, u32 *clicks);

/* ---- timer / clock -------------------------------------------------------- */
void pit_init(void);
u32  pit_ms(void);
void pit_sleep(u32 ms);
void pit_uptime(u32 *h, u32 *m, u32 *s);
extern volatile u32 g_ticks;          /* 100 Hz, set by IRQ0 */

/* ---- real-time clock (CMOS / MC146818) ----------------------------------- */
typedef struct {
    u32 year;     /* full 4-digit, e.g. 2026 */
    u8  month;    /* 1..12  */
    u8  day;      /* 1..31  */
    u8  hour;     /* 0..23  */
    u8  min;      /* 0..59  */
    u8  sec;      /* 0..59  */
} rtc_time_t;

void rtc_now(rtc_time_t *t);     /* raw CMOS read (UTC on most BIOSes)   */
void rtc_local(rtc_time_t *t);   /* CMOS + SET.tz_minutes, date rolled  */

/* ---- gdt / idt / pic ------------------------------------------------------ */
void gdt_install(void);
void idt_install(void);
void pic_remap(void);
void pic_unmask(u8 irq);
void pic_eoi(u32 vec);

/* ---- CPU helpers ---------------------------------------------------------- */
u8   inb(u16 port);
void outb(u16 port, u8 v);
u16  inw(u16 port);
void outw(u16 port, u16 v);
void insw(u16 port, void *buf, u32 count);
void outsw(u16 port, const void *buf, u32 count);
u64  rdtsc(void);

/* ---- utility -------------------------------------------------------------- */
void  k_itoa(u32 v, char *buf, i32 base);
void  k_pad(char *buf, i32 width, char fill);
i32   k_strlen(const char *s);
void  k_memcpy(void *d, const void *s, u32 n);
void  k_memset(void *d, u8 v, u32 n);
/* Volatile zero-fill that the compiler may not optimise away. Use this
 * for transient password / key buffers so plaintext doesn't linger in
 * stack or BSS slots after authentication.                             */
void  k_explicit_bzero(void *p, u32 n);
char *k_strcat(char *d, const char *s);
char *k_strcpy(char *d, const char *s);
i32   k_strcmp(const char *a, const char *b);
i32   k_strncmp(const char *a, const char *b, i32 n);
u32   k_parse_hex(const char *s);
/* Convert an input key to UTF-8 bytes (ASCII + Turkish keycodes above).
 * Returns byte count written to out (1..3), or 0 if key is not printable. */
i32   key_to_utf8(i32 key, char out[4]);

/* ---- multiboot2 memory map ------------------------------------------------ */
typedef struct { u32 base; u32 length; u32 type; } mmap_entry_t;
#define MMAP_MAX 16
extern mmap_entry_t MMAP[MMAP_MAX];
extern i32          MMAP_N;
extern u32          RAM_TOTAL_KB;
void mmap_parse(uptr info_ptr);
const char *mmap_type_name(u32 t);

/* ---- developer log -------------------------------------------------------- */
void log_push_dev(const char *s);

/* ---- developer REPL ------------------------------------------------------- */
void repl_input(i32 key);
void repl_render(i32 x, i32 y, i32 w, i32 h);

/* ---- panic ---------------------------------------------------------------- */
extern volatile bool g_panic;
extern char          g_panic_msg[80];

/* ---- mode dispatcher ------------------------------------------------------ */
typedef enum { MODE_PERSONAL = 0, MODE_DEVELOPER = 1 } falcon_mode_t;

void mode_personal_render(u32 frame);
void mode_personal_input(i32 key);

void mode_developer_render(u32 frame);
void mode_developer_input(i32 key);

/* ---- application framework (Personal kernel) ------------------------------ */
i32          apps_count(void);
const char  *apps_name(i32 i);
u32          apps_tint(i32 i);
const char  *apps_subtitle(i32 i);
void         apps_draw_icon(i32 i, i32 cx, i32 cy);
void         apps_open(i32 i);
void         apps_close(void);
i32          apps_active(void);
i32          apps_minimized(void);
void         apps_render_active(u32 frame);
void         apps_input_active(i32 key);
/* WM mouse handler — returns true when it consumed the click. */
bool         apps_wm_handle_mouse(i32 mx, i32 my, bool left_held, bool click_edge);

/* ---- Launchpad (full-screen app grid, F2) --------------------------------- */
void launchpad_open(void);
void launchpad_close(void);
bool launchpad_is_open(void);
void launchpad_render(u32 frame);
void launchpad_input(i32 key);
i32  launchpad_cursor(void);

/* ---- Jarvis (built-in assistant, FalconOS 1) ----------------------------- */
void jarvis_reset(void);
void jarvis_render(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame);
void jarvis_input(i32 key);
void jarvis_icon(i32 cx, i32 cy);

/* ---- Help panel (slides in from the right, FalconOS 1) ------------------- */
void helppanel_open(void);
void helppanel_close(void);
bool helppanel_is_open(void);
void helppanel_render(u32 frame);
bool helppanel_handle_key(i32 key);                    /* true = consumed */
bool helppanel_handle_mouse(i32 mx, i32 my, bool click);
bool menu_bar_help_hit(i32 mx, i32 my);                /* exposed for main */

/* ---- desktop widgets + shortcuts (v5) ------------------------------------ */
void widgets_render(u32 frame);
void desktop_pins_render(u32 frame);
bool desktop_pins_input_click(i32 mx, i32 my);     /* returns true if launched */
bool desktop_pin_toggle(i32 app_id);                /* returns new pinned state */
bool desktop_pin_is_pinned(i32 app_id);

/* ---- v5 boot screens ------------------------------------------------------ */
typedef enum {
    SCR_INSTALL = 0,    /* first-boot wizard (lang, theme, password)        */
    SCR_LOCK    = 1,    /* lock screen (password entry)                     */
    SCR_DESKTOP = 2     /* normal Personal/Developer dispatch               */
} screen_t;

void installer_render(u32 frame);
void installer_input(i32 key);
bool installer_is_done(void);

void lockscreen_render(u32 frame);
void lockscreen_input(i32 key);
bool lockscreen_is_unlocked(void);
void lockscreen_lock(void);

/* ---- Power menu (FalconOS 1) --------------------------------------------- */
void power_menu_open(void);
void power_menu_close(void);
bool power_menu_is_open(void);
void power_menu_render(u32 frame);
void power_menu_handle_key(i32 key);
bool power_menu_handle_mouse(i32 mx, i32 my, bool click_edge);

/* ---- multi-user database (v5) -------------------------------------------- */
/*  Up to FALCON_MAX_USERS accounts.  The first one created in the installer
 *  becomes the system "default" — every cold boot opens the lock screen
 *  focused on that user.  Passwords are NEVER stored as plaintext: each user
 *  carries a 16-byte random salt and a 32-byte PBKDF2-HMAC-SHA256 hash that
 *  is stretched 50 000 rounds.  See kernel/auth.c.                        */
#define FALCON_MAX_USERS    8
#define FALCON_SALT_BYTES   16
#define FALCON_HASH_BYTES   32
#define FALCON_NAME_BYTES   24

typedef struct {
    bool   in_use;
    bool   is_default;                    /* first-created user = main      */
    char   name[FALCON_NAME_BYTES];
    u8     salt[FALCON_SALT_BYTES];
    u8     hash[FALCON_HASH_BYTES];
    u8     no_password;                   /* 1 = empty pwd, hash unused     */
    accent_t accent;                      /* per-user accent for avatar    */
    /* FalconOS 1 — security audit / brute-force resistance.  Persistent so a
     * reboot does not reset the throttle window mid-attack.            */
    u32    failed_attempts;               /* since last successful unlock   */
    u32    last_login_uptime_ms;          /* PIT ms at successful unlock    */
    u32    last_fail_uptime_ms;           /* PIT ms at most recent failure  */
} falcon_user_t;

/* ---- runtime settings (single source of truth) ---------------------------- */
typedef struct {
    bool        installed;       /* installer wizard completed              */
    theme_t     theme;
    accent_t    accent;
    lang_t      lang;
    kbd_layout_t kbd_layout;     /* TR-Q / TR-F / US                        */
    bool        animations;
    i32         dock_size;       /* 0..4 → 50px..86px tile                  */
    i32         viewport_w;      /* 0 = native, else letterbox sub-rect     */
    i32         viewport_h;
    char        password[24];    /* deprecated single-user; kept for compat */
    char        owner[24];       /* deprecated; mirrors users[default].name */
    bool        widgets_shown;   /* hide widget grid if false               */
    bool        center_circle;   /* legacy v4 hero (default false in v5)    */

    /* multi-user (v5+) */
    falcon_user_t users[FALCON_MAX_USERS];
    i32         user_count;
    i32         default_user;    /* index into users[]                      */
    i32         active_user;     /* who unlocked the desktop                */

    /* v5.2 "Aero" — frosted-glass / translucency toggle. Default true on
     * x86_64 (≈ 4 ms per panel even in TCG), users on slow boxes can
     * disable from Settings.                                              */
    bool        aero_enabled;
    /* v5.2 timezone offset in *minutes* east of UTC. e.g. Istanbul = +180,
     * London = 0/+60, NYC = -300, Tokyo = +540. Read by pit_uptime() to
     * convert wall-clock to local time without a full tz database.        */
    i32         tz_minutes;
    /* v5.2 first-run welcome banner — shown once on the desktop after
     * the initial install completes, dismissable.                         */
    bool        welcome_shown;
    /* FalconOS 1 — installer disk-target picked at setup. -1 means
     * "RAM only" (no persistent disk attached); 0..3 indexes into the
     * primary IDE controller's master/slave enumeration produced by
     * linux/ata_pio.c.  diskdb_save() writes into this disk.          */
    i32         install_disk;
    /* FalconOS 1 — sliding Help panel.  Auto-opens on the very first
     * desktop session so a newcomer immediately sees the keyboard /
     * mouse / window-management cheat-sheet, then never again unless
     * the user clicks the ? glyph in the menu bar.                    */
    bool        help_seen;
    /* FalconOS 1 — `prg install` persistence.  Byte i (0/1) tracks whether
     * CATALOG[i] is currently installed.  Built-ins live in CATALOG slots
     * 0..N-1 and are forced to 1 on every boot regardless of disk state.
     * 128 entries gives ~1.5× headroom over today's 85-entry catalogue and
     * fits comfortably inside the 2 048-byte FalconFS superblock budget. */
    u8          prg_installed[128];
} settings_t;

extern settings_t SET;

void settings_init(void);

const char *T(const char *en, const char *tr);  /* tr/en switch helper      */
/* 5-way translation: NULL slots fall back to English. Used when v5.2
 * adds DE/FR/ES coverage to a string that already had TR localised.   */
const char *TX(const char *en, const char *tr, const char *de,
               const char *fr, const char *es);
const char *lang_name(lang_t l);                /* "Turkce", "English", ... */
const char *kbd_layout_name(kbd_layout_t l);

/* ---- locale-aware formatting --------------------------------------------- */
char        loc_decimal_sep(void);              /* ',' or '.'               */
const char *loc_month_short(u8 month_1_to_12);  /* "Jan", "Oca", "Jan", ... */
void        loc_format_date(char *out, const rtc_time_t *t);
void        loc_format_int_grouped(char *out, u32 value);  /* 1.234.567 etc */

/* ---- multi-user helpers (v5+) -------------------------------------------- */
i32  users_add(const char *name, const char *plaintext_pwd, accent_t accent);
bool users_remove(i32 idx);
bool users_set_default(i32 idx);
bool users_change_password(i32 idx, const char *new_plaintext);
bool users_verify(i32 idx, const char *plaintext);
/* Heuristic 0..3 strength rating: 0=empty/very-short, 1=weak,
 * 2=ok, 3=strong (length >= 12 + mixed classes).                       */
i32  password_strength(const char *plain);
const falcon_user_t *users_at(i32 idx);
i32  users_count(void);

/* ---- crypto: SHA-256 + PBKDF2 (kernel/auth.c) ---------------------------- */
void sha256_hash(const u8 *data, u32 len, u8 out[32]);
void hmac_sha256(const u8 *key, u32 klen, const u8 *msg, u32 mlen, u8 out[32]);
void pbkdf2_sha256(const u8 *pwd, u32 plen,
                   const u8 *salt, u32 slen,
                   u32 iterations, u8 *out, u32 outlen);
void rng_bytes(u8 *out, u32 n);
void hex_encode(const u8 *in, u32 n, char *out);   /* out >= n*2+1 chars   */

/* ---- disk persistence: FalconFS superblock (kernel/diskdb.c) ------------- */
#define FALCONFS_MAGIC      0x46414C43   /* 'FALC' */
#define FALCONFS_VERSION    3            /* FalconOS 1.1: prg install state  */
#define FALCONFS_SECTOR     0            /* LBA0 of master ATA device      */

void diskdb_load(void);          /* called from settings_init()             */
bool diskdb_save(void);          /* writes SET into LBA0                    */
bool diskdb_present(void);       /* true if last load found a magic block   */

/* ---- ATA PIO (linux/ata_pio.c) ------------------------------------------- */
bool ata_read_lba28 (i32 dev, u32 lba, u8 *buf512, u32 sectors);
bool ata_write_lba28(i32 dev, u32 lba, const u8 *buf512, u32 sectors);

/* ---- keyboard layout (kernel/kbd.c, switched by SET.kbd_layout) ---------- */
i32  kbd_translate(u8 sc, kbd_layout_t layout, bool shift);

/* ---- prg package manager + Store app -------------------------------------- */
typedef struct {
    const char *name;
    const char *version;
    const char *summary;
    const char *category;
    const char *depends;
    u32         size_kb;
    bool        builtin;         /* ships with the OS, can't be removed     */
} prg_pkg_t;

i32                prg_count(void);
const prg_pkg_t   *prg_at(i32 i);
const prg_pkg_t   *prg_find(const char *name);
bool               prg_is_installed(i32 i);
bool               prg_install(i32 i);
bool               prg_remove(i32 i);
i32                prg_installed_count(void);

/* ---- Linux compatibility (drivers + UAPI shims) -------------------------- */
void   linux_compat_init(void);
const char *linux_compat_summary(void);
i32    ata_probe_count(void);          /* 0..2 ATA devices detected      */
const char *ata_model(i32 idx);
u64    ata_sectors(i32 idx);
void   ata_stats(u32 *reads, u32 *writes, u32 *retries, u32 *failed);
void   hid_keymap_dump(char *buf, u32 max);

/* tiny global tick (incremented by main loop, NOT real time — see g_ticks) */
extern volatile u32 g_tick;

#endif /* FALCON_H */
