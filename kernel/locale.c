/* =============================================================================
 *  FalconOS — locale (v5.2)
 * -----------------------------------------------------------------------------
 *  Translation fallback (TX), locale-aware date / number formatting and
 *  language display names.
 *
 *  We keep *primary* coverage at TR + EN — every existing string in the
 *  kernel ships with both. DE / FR / ES are added at "baseline" level:
 *  installer, lock screen, settings labels, login welcome — i.e. the
 *  strings a normal user sees on day one. Anything not yet localized in
 *  those three languages falls back to English, never to a placeholder.
 *
 *  Number / date formatting follows the same priorities:
 *    en        : '.' decimals, ',' thousands, MM/DD/YYYY dates
 *    tr/de/fr  : ',' decimals, '.' thousands, DD.MM.YYYY (de) / DD/MM/YYYY
 *    es        : ',' decimals, '.' thousands, DD/MM/YYYY
 * ============================================================================= */
#include "falcon.h"

const char *TX(const char *en, const char *tr, const char *de,
               const char *fr, const char *es)
{
    switch (SET.lang) {
        case LANG_TR: return tr ? tr : en;
        case LANG_DE: return de ? de : en;
        case LANG_FR: return fr ? fr : en;
        case LANG_ES: return es ? es : en;
        case LANG_EN: default: return en;
    }
}

const char *lang_name(lang_t l)
{
    switch (l) {
        case LANG_TR: return "Turkce";
        case LANG_EN: return "English";
        case LANG_DE: return "Deutsch";
        case LANG_FR: return "Francais";
        case LANG_ES: return "Espanol";
        default: return "?";
    }
}

char loc_decimal_sep(void)
{
    return (SET.lang == LANG_EN) ? '.' : ',';
}

static char loc_thousand_sep(void)
{
    return (SET.lang == LANG_EN) ? ',' : '.';
}

static const char *MONTHS_EN[12] = { "Jan","Feb","Mar","Apr","May","Jun",
                                     "Jul","Aug","Sep","Oct","Nov","Dec" };
static const char *MONTHS_TR[12] = { "Oca","Sub","Mar","Nis","May","Haz",
                                     "Tem","Agu","Eyl","Eki","Kas","Ara" };
static const char *MONTHS_DE[12] = { "Jan","Feb","Mar","Apr","Mai","Jun",
                                     "Jul","Aug","Sep","Okt","Nov","Dez" };
static const char *MONTHS_FR[12] = { "Jan","Fev","Mar","Avr","Mai","Jui",
                                     "Jul","Aou","Sep","Oct","Nov","Dec" };
static const char *MONTHS_ES[12] = { "Ene","Feb","Mar","Abr","May","Jun",
                                     "Jul","Ago","Sep","Oct","Nov","Dic" };

const char *loc_month_short(u8 m)
{
    if (m < 1 || m > 12) return "?";
    switch (SET.lang) {
        case LANG_TR: return MONTHS_TR[m - 1];
        case LANG_DE: return MONTHS_DE[m - 1];
        case LANG_FR: return MONTHS_FR[m - 1];
        case LANG_ES: return MONTHS_ES[m - 1];
        case LANG_EN: default: return MONTHS_EN[m - 1];
    }
}

static void put2(char *p, u32 v)
{
    p[0] = (char)('0' + (v / 10) % 10);
    p[1] = (char)('0' +  v       % 10);
}

void loc_format_date(char *out, const rtc_time_t *t)
{
    /* sep selection per locale convention */
    char sep = '/';
    bool day_first = (SET.lang != LANG_EN);
    if (SET.lang == LANG_DE) sep = '.';

    char tmp[16];
    if (day_first) {
        put2(&tmp[0], t->day);
        tmp[2] = sep;
        put2(&tmp[3], t->month);
        tmp[5] = sep;
    } else {
        put2(&tmp[0], t->month);
        tmp[2] = sep;
        put2(&tmp[3], t->day);
        tmp[5] = sep;
    }
    /* year (assume 2000..9999) */
    u32 y = t->year;
    tmp[6] = (char)('0' + (y / 1000) % 10);
    tmp[7] = (char)('0' + (y /  100) % 10);
    tmp[8] = (char)('0' + (y /   10) % 10);
    tmp[9] = (char)('0' +  y         % 10);
    tmp[10] = 0;

    /* copy out (caller-provided buffer, no length checks; 16+ is safe) */
    for (i32 i = 0; tmp[i]; i++) out[i] = tmp[i];
    out[10] = 0;
}

void loc_format_int_grouped(char *out, u32 value)
{
    char digits[12];
    i32 n = 0;
    if (value == 0) {
        digits[n++] = '0';
    } else {
        while (value > 0 && n < 11) {
            digits[n++] = (char)('0' + (value % 10));
            value /= 10;
        }
    }
    /* now reverse with separators every 3 digits */
    char sep = loc_thousand_sep();
    i32 oi = 0;
    for (i32 i = n - 1; i >= 0; i--) {
        out[oi++] = digits[i];
        if (i > 0 && (i % 3) == 0) out[oi++] = sep;
    }
    out[oi] = 0;
}
