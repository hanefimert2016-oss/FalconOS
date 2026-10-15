/* Falco MiniWeb/3: bounded HTML/CSS/JS subset, no arbitrary JS execution.
 * Only literals and explicitly allowlisted DOM mutations are evaluated.
 * Browser layout is native FalconOS framebuffer, not a host screenshot. */
#ifndef FALCO_MINIWEB_H
#define FALCO_MINIWEB_H
#include "falcon.h"
#define FMW_NODES 64
#define FMW_STYLES 32
typedef struct {
    char id[32],klass[32],tag[12],text[224],onclick[192],href[192];
    u32 color,bg;
    u16 font_px;
    u8 align,hidden,button,bold;
} fmw_node_t;
typedef struct {
    fmw_node_t nodes[FMW_NODES];
    u32 count,css_rules,js_mutations,buttons;
    u32 page_bg;
    char title[96];
} fmw_page_t;
bool falco_miniweb_parse(fmw_page_t *page,const char *html);
bool falco_miniweb_parse_options(fmw_page_t *page,const char *html,
                                 bool allow_literal_dom);
bool falco_miniweb_action(fmw_page_t *page,u32 button_index);
#endif
