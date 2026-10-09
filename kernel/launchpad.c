/* FalconOS Shelf Launcher — Chromebook-inspired app workspace.
 *
 * Home: 9 functional favorites. System: settings and real diagnostics.
 * All: functional built-ins and installed package IDs; demo apps hidden.
 * Typed search filters names. Arrow/Enter/mouse open real app IDs.
 *
 * No per-frame heap allocation, GPU or browser dependency.
 */
#include "falcon.h"
#define DRAW_COLS 4
#define DRAW_ROWS 3
#define DRAW_PAGE (DRAW_COLS*DRAW_ROWS)
#define MAX_VISIBLE 80
static bool is_open;
static i32 section_id,selected,page;
static char search_term[40];
static i32 search_length;
static u32 opened_at;

static bool hit(i32 mx,i32 my,i32 x,i32 y,i32 w,i32 h){
    return mx>=x&&my>=y&&mx<x+w&&my<y+h;
}
static bool has_ci(const char *s,const char *term){
    if(!term||!*term)return true;
    for(u32 i=0;s&&s[i];i++){
        u32 j=0;
        while(term[j]&&s[i+j]){
            char a=s[i+j],b=term[j];
            if(a>='A'&&a<='Z')a+=32;
            if(b>='A'&&b<='Z')b+=32;
            if(a!=b)break;
            j++;
        }
        if(!term[j])return true;
    }
    return false;
}
static i32 visible[MAX_VISIBLE],visible_count;
static void refresh(void){
    visible_count=0;
    i32 n=apps_launcher_count(section_id);
    for(i32 i=0;i<n&&visible_count<MAX_VISIBLE;i++){
        i32 id=apps_launcher_id(section_id,i);
        if(id>=0 && has_ci(apps_display_name(id),search_term))
            visible[visible_count++]=id;
    }
    if(selected<0)selected=0;
    if(selected>=visible_count)selected=visible_count?visible_count-1:0;
    page=selected/DRAW_PAGE;
}
bool launchpad_is_open(void){return is_open;}
void launchpad_open(void){
    is_open=true;opened_at=pit_ms();
    refresh();
}
void launchpad_close(void){is_open=false;}
i32 launchpad_cursor(void){return selected;}
void launchpad_input(i32 key){
    if(key==KEY_ESC||key==KEY_F2){launchpad_close();return;}
    if(key==KEY_TAB){
        section_id=(section_id+1)%3;
        selected=0;refresh();return;
    }
    if(key==KEY_LEFT)selected--;
    else if(key==KEY_RIGHT)selected++;
    else if(key==KEY_UP)selected-=DRAW_COLS;
    else if(key==KEY_DOWN)selected+=DRAW_COLS;
    else if(key==KEY_PGUP)selected-=DRAW_PAGE;
    else if(key==KEY_PGDN)selected+=DRAW_PAGE;
    else if(key==KEY_BACKSPACE){
        if(search_length>0)search_term[--search_length]=0;
        selected=0;refresh();return;
    }else if(key==KEY_ENTER){
        if(visible_count){apps_open(visible[selected]);launchpad_close();}
        return;
    }else if(key=='p'&&visible_count){
        desktop_pin_toggle(visible[selected]);return;
    }else{
        char bytes[4];i32 count=key_to_utf8(key,bytes);
        if(count>0&&search_length+count<(i32)sizeof search_term){
            for(i32 j=0;j<count;j++)search_term[search_length++]=bytes[j];
            search_term[search_length]=0;
            selected=0;refresh();
        }
        return;
    }
    if(selected<0)selected=0;
    if(selected>=visible_count)selected=visible_count?visible_count-1:0;
    page=selected/DRAW_PAGE;
}
void launchpad_render(u32 frame){
    (void)frame;
    if(!is_open)return;
    i32 W=(i32)FB.width,H=(i32)FB.height;
    gfx_rect_a(0,30,W,H-30,0x101C37,130);
    i32 panel_w= W-72;
    if(panel_w>930)panel_w=930;
    i32 panel_h=H-180;
    if(panel_h>710)panel_h=710;
    if(panel_h<420)panel_h=420;
    i32 x=(W-panel_w)/2,y=(H-panel_h)/2-10;
    if(y<40)y=40;
    gfx_round_rect_a(x+7,y+14,panel_w,panel_h,28,0x020818,84);
    gfx_round_rect(x,y,panel_w,panel_h,28,0xF8FAFF);
    gfx_round_outline(x,y,panel_w,panel_h,28,0xD4E1F2);
    gfx_round_rect(x+24,y+20,48,48,17,0x2662DF);
    gfx_text_lg_centered(x+48,y+25,"F",0xFFFFFF);
    gfx_text_lg(x+88,y+24,"Apps",0x192945);
    gfx_text(x+89,y+57,"Your FalconOS workspace",0x65758D);
    i32 search_y=y+90;
    gfx_round_rect(x+26,search_y,panel_w-52,48,19,0xECF3FF);
    gfx_round_outline(x+26,search_y,panel_w-52,48,19,0xC9DAF5);
    gfx_circle_outline(x+50,search_y+22,8,0x4977BC);
    gfx_line(x+56,search_y+28,x+62,search_y+34,0x4977BC);
    gfx_text(x+77,search_y+17,search_length?search_term:
        "Search installed apps...",search_length?0x132E54:0x8495AA);
    if(((pit_ms()-opened_at)/450u)&1u)
        gfx_rect(x+80+gfx_text_width(search_term),search_y+13,2,21,0x396CD8);
    const char *tabs[]={"Essentials","System","All apps"};
    i32 tabs_y=search_y+66;
    for(i32 i=0;i<3;i++){
        i32 tx=x+27+i*148;
        gfx_round_rect(tx,tabs_y,138,36,15,i==section_id?0x286BE5:0xE9EEF8);
        gfx_text_centered(tx+69,tabs_y+11,tabs[i],i==section_id?0xFFFFFF:0x61748F);
    }
    i32 start=page*DRAW_PAGE,end=start+DRAW_PAGE;
    if(end>visible_count)end=visible_count;
    i32 grid_top=tabs_y+62;
    i32 tile_w=(panel_w-64-3*14)/4;
    i32 tile_h=(panel_h-(grid_top-y)-80)/3;
    if(tile_h>136)tile_h=136;
    if(tile_h<78)tile_h=78;
    i32 mx,my;bool held;mouse_get(&mx,&my,&held);(void)held;
    bool click=mouse_peek_click();
    if(click&&hit(mx,my,x+20,tabs_y,3*148,38)){
        i32 tab=(mx-x-27)/148;
        if(tab>=0&&tab<3){
            section_id=tab;selected=0;refresh();
            (void)mouse_consume_click();
            click=false;
        }
    }
    for(i32 k=start;k<end;k++){
        i32 slot=k-start,col=slot%4,row=slot/4;
        i32 tx=x+32+col*(tile_w+14),ty=grid_top+row*(tile_h+10);
        bool hover=hit(mx,my,tx,ty,tile_w,tile_h);
        bool focus=k==selected;
        gfx_round_rect(tx,ty,tile_w,tile_h,18,
            focus||hover?0xE4EDFF:0xF0F4FB);
        if(focus||hover)gfx_round_outline(tx,ty,tile_w,tile_h,18,0x94B9FB);
        i32 id=visible[k];
        u32 tint=apps_tint(id);
        i32 cx=tx+tile_w/2;
        gfx_round_rect(cx-26,ty+9,52,52,17,tint);
        apps_draw_icon(id,cx,ty+35);
        const char *name=apps_display_name(id);
        gfx_text_centered(cx,ty+73,name,0x1B3151);
        if(desktop_pin_is_pinned(id))
            gfx_circle(cx+29,ty+15,4,0x24B785);
        if(hover&&click){
            (void)mouse_consume_click();
            apps_open(id);launchpad_close();
            return;
        }
    }
    if(!visible_count)gfx_text_centered(x+panel_w/2,grid_top+80,
        "No app found. Try another search.",0x5E7394);
    gfx_rect(x+27,y+panel_h-51,panel_w-54,1,0xDFE7F4);
    char count[12];k_itoa((u32)visible_count,count,10);
    gfx_text(x+31,y+panel_h-33,count,0x285EBD);
    gfx_text(x+56,y+panel_h-33,
        "apps    Tab categories   Arrows navigate   Enter open   Esc close",
        0x697C99);
}
