/* Falco browser chrome color tokens: site document color stays independent. */
#ifndef FALCO_THEME_H
#define FALCO_THEME_H
#include "falcon.h"
typedef struct {
    u32 chrome, chrome_border, tab, tab_inactive, tab_hover;
    u32 omnibox, omnibox_focus, surface, surface_alt, hairline;
    u32 text, muted, faint, accent, accent_ink, secure;
} falco_theme_t;
falco_theme_t falco_theme_current(void);
u32 falco_theme_mix(u32 a,u32 b,u32 weight_b);
void falco_theme_header(i32 x,i32 y,i32 w,bool secure);
#endif
