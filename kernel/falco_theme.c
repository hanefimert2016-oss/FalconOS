/* FalconOS Falco V2 theme engine (first-party C software UI).
 * All colors derive from the current FalconOS theme + chosen accent.
 * Never apply these colors to external HTML/CSS content: web document
 * colors are authored by their origin, not by the browser's toolbars.
 */
#include "falco_theme.h"

u32 falco_theme_mix(u32 a,u32 b,u32 weight_b){
    if(weight_b>255)weight_b=255;
    u32 aw=255u-weight_b;
    u32 ar=((a>>16)&255u)*aw,ag=((a>>8)&255u)*aw,ab=(a&255u)*aw;
    u32 br=((b>>16)&255u)*weight_b,bg=((b>>8)&255u)*weight_b,bb=(b&255u)*weight_b;
    return (((ar+br+127u)/255u)<<16) |
           (((ag+bg+127u)/255u)<<8) |
           ((ab+bb+127u)/255u);
}
static u32 perceived_luma(u32 c){
    return (((c>>16)&255u)*299u+((c>>8)&255u)*587u+(c&255u)*114u)/1000u;
}
falco_theme_t falco_theme_current(void){
    falco_theme_t s;
    const bool dark=SET.theme==THEME_DARK;
    s.chrome=PAL_PANEL_DEEP;
    s.chrome_border=PAL_HAIRLINE;
    s.tab=PAL_PANEL;
    s.tab_inactive=falco_theme_mix(PAL_PANEL_DEEP,PAL_PANEL_HI,dark?66u:94u);
    s.tab_hover=falco_theme_mix(PAL_PANEL_HI,PAL_PANEL,124);
    s.omnibox=PAL_PANEL;
    s.omnibox_focus=PAL_ACCENT;
    s.surface=PAL_PANEL;
    s.surface_alt=falco_theme_mix(PAL_PANEL,PAL_PANEL_DEEP,80);
    s.hairline=PAL_HAIRLINE;
    s.text=PAL_TEXT;
    s.muted=PAL_TEXT_DIM;
    s.faint=PAL_TEXT_FAINT;
    s.accent=PAL_ACCENT;
    s.accent_ink=perceived_luma(s.accent)<152u?0xFFFFFFu:0x101D2Au;
    s.secure=dark?0x80E4B8u:0x157E60u;
    return s;
}
void falco_theme_header(i32 x,i32 y,i32 w,bool secure){
    falco_theme_t s=falco_theme_current();
    if(w<700)return;
    i32 mx,my;bool held;
    mouse_get(&mx,&my,&held);
    (void)held;
    bool back_hover=mx>=x+195&&mx<x+274&&my>=y+8&&my<y+51;
    bool address_hover=mx>=x+280&&mx<x+w-370&&my>=y+9&&my<y+46;
    /* Geometries intentionally match browser click targets in apps.c.
     * The native site renderer and fallback HTML text reader now share
     * a common, consistent browser accent / tab / omnibox vocabulary. */
    gfx_round_rect(x,y,w,53,12,s.chrome);
    gfx_rect(x,y+51,w,2,s.chrome_border);
    gfx_round_rect(x+14,y+8,170,44,11,s.tab);
    gfx_round_outline(x+14,y+8,170,44,11,s.chrome_border);
    gfx_rect(x+30,y+50,139,2,s.accent);
    gfx_circle(x+33,y+28,7,s.accent);
    gfx_text(x+48,y+20,"Falco",s.text);
    gfx_text(x+202,y+19,"<    >",back_hover?s.accent:s.muted);
    i32 aw=w-650;
    if(aw<140)aw=140;
    gfx_round_rect(x+280,y+9,aw,37,11,s.omnibox);
    gfx_round_outline(x+280,y+9,aw,37,11,address_hover?s.omnibox_focus:s.hairline);
    gfx_circle(x+299,y+27,5,s.secure);
    gfx_text(x+316,y+19,"https://falconos.tech/",s.text);
    gfx_round_rect(x+w-351,y+10,335,35,11,s.tab_inactive);
    gfx_round_outline(x+w-351,y+10,335,35,11,s.chrome_border);
    gfx_circle(x+w-329,y+27,5,s.secure);
    gfx_text(x+w-316,y+19,
         secure?"Host CA-verified HTTPS":"HTTPS not verified",s.muted);
}
