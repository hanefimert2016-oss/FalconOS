/* FalconOS V2 AeroFlow.
 * Shared CPU-framebuffer design primitives: original FalconOS glass visual
 * language, derived entirely from the active system palette. No fake data,
 * GPU dependency, dynamic allocation or window ABI changes.
 */
#ifndef FALCON_AEROFLOW_H
#define FALCON_AEROFLOW_H
#include "falcon.h"

static inline bool aero_dark(void){return SET.theme==THEME_DARK;}
static inline u32 aero_text(void){return PAL_TEXT;}
static inline u32 aero_dim(void){return PAL_TEXT_DIM;}
static inline u32 aero_accent(void){return PAL_ACCENT;}
static inline u32 aero_accent_ink(void){
    const u32 c=PAL_ACCENT;
    const u32 lum=(((c>>16)&255)*299+((c>>8)&255)*587+(c&255)*114)/1000;
    return lum>=147?0x102039u:0xFFFFFFu;
}
static inline u32 aero_layer(void){return PAL_PANEL;}
static inline u32 aero_soft(void){return PAL_PANEL_DEEP;}
static inline u32 aero_tile(void){return PAL_PANEL_HI;}
static inline u32 aero_line(void){return PAL_HAIRLINE;}
static inline void aero_panel(i32 x,i32 y,i32 w,i32 h,i32 r){
    gfx_round_rect_a(x+4,y+8,w,h,r,0x071324u,aero_dark()?104:55);
    gfx_round_rect_a(x,y,w,h,r,aero_layer(),SET.aero_enabled?244:255);
    gfx_round_outline(x,y,w,h,r,aero_line());
    gfx_rect_a(x+r,y+1,w-2*r,1,0xFFFFFFu,aero_dark()?24:75);
}
static inline void aero_card(i32 x,i32 y,i32 w,i32 h,i32 r,bool selected){
    gfx_round_rect(x,y,w,h,r,selected?PAL_ACCENT_DIM:aero_soft());
    gfx_round_outline(x,y,w,h,r,selected?PAL_ACCENT:aero_line());
}
static inline void aero_chip(i32 x,i32 y,i32 w,i32 h,const char *label,bool selected){
    gfx_round_rect(x,y,w,h,11,selected?aero_accent():aero_tile());
    if(!selected)gfx_round_outline(x,y,w,h,11,aero_line());
    gfx_text_centered(x+w/2,y+(h-16)/2,label,selected?aero_accent_ink():aero_dim());
}
#endif
