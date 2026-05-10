/* =============================================================================
 *  FalconOS — runtime settings + theme palette  (v5)
 * -----------------------------------------------------------------------------
 *  Single source of truth for everything the user can tweak from the
 *  installer or the Settings app: theme (Lumen / Nox), accent color, language,
 *  dock size, animations, viewport letterbox, password, device owner.
 *
 *  Render code never references the COL_* / DCOL_* constants directly — it
 *  goes through PAL() so a one-line `SET.theme = THEME_DARK` flips the whole
 *  shell instantly.
 * ============================================================================= */
#include "falcon.h"

settings_t SET;

/* Accent presets keyed by accent_t.  The blue baseline is the v4 Lumen blue. */
static const u32 ACCENTS[ACC_COUNT] = {
    0x3070FF,    /* ACC_BLUE     — Lumen default                        */
    0xA45EE5,    /* ACC_PURPLE                                          */
    0x16B5A8,    /* ACC_GREEN    — teal-leaning                         */
    0xE85D9C,    /* ACC_PINK                                            */
    0x6E7884,    /* ACC_GRAPHITE — neutral                              */
};

void settings_init(void)
{
    k_memset(&SET, 0, sizeof(SET));
    SET.installed     = false;          /* installer wizard pending      */
    SET.theme         = THEME_LIQUID;   /* default “Liquid Glass” desktop      */
    SET.accent        = ACC_BLUE;
    SET.lang          = LANG_TR;        /* repo owner is Turkish         */
    SET.kbd_layout    = KBD_TR_Q;       /* default for TR users          */
    SET.animations    = true;
    SET.dock_size     = 2;
    SET.viewport_w    = 0;              /* native                        */
    SET.viewport_h    = 0;
    SET.widgets_shown = false;          /* clean first-boot desktop      */
    SET.center_circle = false;          /* removed in v5 by user request */
    k_strcpy(SET.password, "");
    k_strcpy(SET.owner,    "Falcon");
    SET.user_count    = 0;
    SET.default_user  = 0;
    SET.active_user   = 0;
    for (i32 i = 0; i < FALCON_MAX_USERS; i++) SET.users[i].in_use = false;

    /* v5.2 Aero — translucency on by default. Falls back gracefully on
     * older hardware via Settings ▸ "Aero" toggle.                      */
    SET.aero_enabled  = true;
    /* v5.2 timezone — Istanbul / TRT (UTC+3). Installer step lets the
     * user pick a different region; in headless / pre-install boots
     * the lock screen still shows a sensible local time.                */
    SET.tz_minutes    = 180;
    SET.welcome_shown = false;
    SET.install_disk  = -1;            /* güvenli çalıştırma unless user picks ATA */
    /* FalconOS 1 — Help drawer; auto-opens on first desktop session. */
    SET.help_seen     = false;
    /* FalconOS 1.2 — workspaces + first-time tour                       */
    SET.active_workspace = 0;
    SET.workspace_count  = 4;
    SET.toured           = false;

    /* Probe disk and try to restore the entire settings_t from LBA0. If a
     * superblock with matching magic+version+checksum is found, SET is
     * overwritten and the installer wizard is skipped automatically.    */
    diskdb_load();
}

/* ---- runtime palette ----------------------------------------------------- */
/* Palette table indexed by theme_t. Roles 0..12 correspond to PAL_BG_TOP ..
 * PAL_GLASS (see falcon.h). Role 9 (accent) is overridden per-user from
 * ACCENTS[SET.accent] except in Liquid where the accent is fixed to a
 * frosted-aqua and in Rose-Gold where it's a warm rose. */
typedef struct { u32 c[13]; } pal_set_t;

static const pal_set_t THEMES[THEME_COUNT] = {
    /* THEME_LIGHT — Lumen (default) */
    { { 0xEAF0F8, 0xC8D5E6, 0xA8BBD3, 0xFFFFFF, 0xD8E1EC, 0xF1F4F9,
        0x14181F, 0x4F5764, 0xA3ACB7, 0x3070FF, 0xB8CDFF, 0xC4CDD9,
        0xFFFFFF } },
    /* THEME_DARK — Nox */
    { { 0x101418, 0x1B2129, 0x252C36, 0x1F252E, 0x2A3140, 0x171C24,
        0xE8ECF1, 0xA0A8B5, 0x6F7785, 0x3070FF, 0x2A3D6A, 0x2A3140,
        0x33405A } },
    /* THEME_LIQUID — Liquid Glass: vivid underwater gradient,
     * cyan accent, panels lean white-aqua so the new stronger blur
     * inside gfx_round_glass shines through.                       */
    { { 0x9DD8F2, 0x3E8EC0, 0x6FBFE0, 0xEFF7FD, 0xB7DAEE, 0xE3EEF7,
        0x0A1420, 0x3F4D60, 0x8FA4B8, 0x00A0E5, 0x9CE4FA, 0xB6CEDF,
        0xFFFFFF } },
    /* THEME_NORDIC — cool blue-grey, low-contrast */
    { { 0xECEFF4, 0xD8DEE9, 0xC4CBD5, 0xFFFFFF, 0xE5E9F0, 0xF2F4F8,
        0x2E3440, 0x4C566A, 0x6E7886, 0x5E81AC, 0xA9C0DE, 0xD8DEE9,
        0xF5F8FB } },
    /* THEME_ROSEGOLD — warm pink-gold */
    { { 0xFAF1EE, 0xF1DCD7, 0xE6C2B9, 0xFFFFFF, 0xF7DDD4, 0xFBEDE7,
        0x3A2A2E, 0x6E5358, 0xA08084, 0xD78E84, 0xF1C8C0, 0xE9D5CE,
        0xFFFFFF } },
};

u32 PAL(u8 role)
{
    if (role > 12) return 0xFF00FF;
    theme_t t = SET.theme;
    if ((u32)t >= THEME_COUNT) t = THEME_LIGHT;
    /* Role 9 (PAL_ACCENT) is mostly user-tunable, but Liquid and
     * Rose-Gold pin it for visual identity. */
    if (role == 9) {
        if (t == THEME_LIQUID || t == THEME_ROSEGOLD) return THEMES[t].c[9];
        return ACCENTS[SET.accent];
    }
    return THEMES[t].c[role];
}

/* ---- tr/en translation helper -------------------------------------------- */
const char *T(const char *en, const char *tr)
{
    return SET.lang == LANG_TR ? tr : en;
}
