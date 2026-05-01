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
    SET.theme         = THEME_LIGHT;
    SET.accent        = ACC_BLUE;
    SET.lang          = LANG_TR;        /* repo owner is Turkish         */
    SET.kbd_layout    = KBD_TR_Q;       /* default for TR users          */
    SET.animations    = true;
    SET.dock_size     = 2;
    SET.viewport_w    = 0;              /* native                        */
    SET.viewport_h    = 0;
    SET.widgets_shown = true;
    SET.center_circle = false;          /* removed in v5 by user request */
    k_strcpy(SET.password, "");
    k_strcpy(SET.owner,    "Falcon");
    SET.user_count    = 0;
    SET.default_user  = 0;
    SET.active_user   = 0;
    for (i32 i = 0; i < FALCON_MAX_USERS; i++) SET.users[i].in_use = false;

    /* Probe disk and try to restore the entire settings_t from LBA0. If a
     * superblock with matching magic+version+checksum is found, SET is
     * overwritten and the installer wizard is skipped automatically.    */
    diskdb_load();
}

/* ---- runtime palette ----------------------------------------------------- */
u32 PAL(u8 role)
{
    if (SET.theme == THEME_DARK) {
        switch (role) {
            case 0:  return DCOL_BG_TOP;
            case 1:  return DCOL_BG_BOT;
            case 2:  return DCOL_BG_HINT;
            case 3:  return DCOL_PANEL;
            case 4:  return DCOL_PANEL_HI;
            case 5:  return DCOL_PANEL_DEEP;
            case 6:  return DCOL_TEXT;
            case 7:  return DCOL_TEXT_DIM;
            case 8:  return DCOL_TEXT_FAINT;
            case 9:  return ACCENTS[SET.accent];
            case 10: return 0x2A3D6A;            /* dim accent for dark   */
            case 11: return DCOL_HAIRLINE;
            case 12: return DCOL_GLASS;
        }
    } else {
        switch (role) {
            case 0:  return COL_BG_TOP;
            case 1:  return COL_BG_BOT;
            case 2:  return COL_BG_HINT;
            case 3:  return COL_PANEL;
            case 4:  return COL_PANEL_HI;
            case 5:  return COL_PANEL_DEEP;
            case 6:  return COL_TEXT;
            case 7:  return COL_TEXT_DIM;
            case 8:  return COL_TEXT_FAINT;
            case 9:  return ACCENTS[SET.accent];
            case 10: return COL_ACCENT_DIM;
            case 11: return COL_HAIRLINE;
            case 12: return COL_GLASS;
        }
    }
    return 0xFF00FF;    /* unreachable: hot pink to flag bugs            */
}

/* ---- tr/en translation helper -------------------------------------------- */
const char *T(const char *en, const char *tr)
{
    return SET.lang == LANG_TR ? tr : en;
}
