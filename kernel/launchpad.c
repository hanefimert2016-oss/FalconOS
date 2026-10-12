/* FalconOS Shelf Launcher — Chromebook-inspired app workspace.
 *
 * Home: 9 functional favorites. System: settings and real diagnostics.
 * All: functional built-ins and installed package IDs; demo apps hidden.
 * Typed search filters names. Arrow/Enter/mouse open real app IDs.
 *
 * No per-frame heap allocation, GPU or browser dependency.
 */
#include "falcon.h"
#include "aeroflow.h"
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
    /* A typed app search is global. Restricting it to the current tab made
     * installed apps, Codedium and System utilities appear to be missing. */
    i32 passes=search_length?3:1;
    for(i32 pass=0;pass<passes && visible_count<MAX_VISIBLE;pass++){
        i32 group=search_length?pass:section_id;
        i32 n=apps_launcher_count(group);
        for(i32 i=0;i<n && visible_count<MAX_VISIBLE;i++){
            i32 id=apps_launcher_id(group,i);
            if(id<0)continue;
            bool found=has_ci(apps_display_name(id),search_term) ||
                       has_ci(apps_name(id),search_term) ||
                       (id==2 && (has_ci("Discover",search_term) ||
                                  has_ci("Marketplace",search_term) ||
                                  has_ci("Store",search_term)));
            if(!found)continue;
            bool duplicate=false;
            for(i32 j=0;j<visible_count;j++)
                if(visible[j]==id){duplicate=true;break;}
            if(!duplicate)visible[visible_count++]=id;
        }
    }
    if(selected<0)selected=0;
    if(selected>=visible_count)selected=visible_count?visible_count-1:0;
    page=selected/DRAW_PAGE;
}
bool launchpad_is_open(void){return is_open;}
void launchpad_open(void){
    is_open=true;opened_at=pit_ms();
    /* Every fresh launcher session must start with an empty search.
     * Previously the old 'Falco' term remained when searching Settings,
     * leaving zero matching apps despite a correct real keyboard input. */
    search_term[0]=0;
    search_length=0;
    selected=0;
    section_id=0;
    page=0;
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
    }else if(key==KEY_F3&&visible_count){
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
    const i32 W=(i32)FB.width,H=(i32)FB.height;
    gfx_rect_a(0,30,W,H-30,0x09162Bu,aero_dark()?157:107);
    i32 panel_w=W-46;
    if(panel_w>1000)panel_w=1000;
    i32 panel_h=H-136;
    if(panel_h>728)panel_h=728;
    if(panel_h<440)panel_h=440;
    const i32 x=(W-panel_w)/2,y=(H-panel_h)/2;
    aero_panel(x,y,panel_w,panel_h,25);

    const i32 side_w=207;
    gfx_round_rect(x+10,y+10,side_w,panel_h-20,19,aero_soft());
    gfx_rect(x+side_w+13,y+27,1,panel_h-54,aero_line());
    gfx_round_rect(x+29,y+27,43,43,14,aero_accent());
    gfx_text_lg_centered(x+50,y+33,"F",aero_accent_ink());
    gfx_text(x+84,y+31,"FalconOS",aero_text());
    gfx_text(x+84,y+52,"V2  /  AeroFlow",aero_dim());

    const char *tab_labels[3]={
        T("Favorites","Favoriler"),T("System","Sistem"),
        T("All apps","Tum uygulamalar")
    };
    const char *tab_hints[3]={"01","02","03"};
    const i32 nav_y=y+119;
    i32 mx,my;bool held;mouse_get(&mx,&my,&held);(void)held;
    bool click=mouse_peek_click();
    if(click&&!hit(mx,my,x,y,panel_w,panel_h)){
        (void)mouse_consume_click();launchpad_close();return;
    }
    for(i32 i=0;i<3;i++){
        i32 ny=nav_y+i*54;
        bool hot=hit(mx,my,x+24,ny,178,45);
        if(hot&&click){
            (void)mouse_consume_click();click=false;
            section_id=i;selected=0;refresh();
        }
        aero_card(x+24,ny,178,45,13,section_id==i);
        gfx_text(x+36,ny+14,tab_hints[i],section_id==i?aero_accent():aero_dim());
        gfx_text(x+65,ny+14,tab_labels[i],aero_text());
        if(section_id==i)gfx_rect(x+25,ny+13,3,19,aero_accent());
    }
    gfx_rect(x+31,y+315,164,1,aero_line());
    gfx_text(x+34,y+330,T("SHORTCUTS","KISAYOLLAR"),aero_dim());
    gfx_text(x+34,y+361,T("Tab  -  switch section","Tab  -  kategori"),aero_dim());
    gfx_text(x+34,y+387,T("F3   -  pin selected","F3   -  sabitle"),aero_dim());
    gfx_text(x+34,y+413,T("Enter - open app","Enter - uygulamayi ac"),aero_dim());

    const i32 foot=y+panel_h-80;
    gfx_round_rect(x+24,foot,178,56,14,aero_tile());
    gfx_circle(x+48,foot+28,15,aero_accent());
    gfx_text_centered(x+48,foot+20,"F",aero_accent_ink());
    gfx_text(x+74,foot+14,SET.owner[0]?SET.owner:"Falcon",aero_text());
    gfx_text(x+74,foot+36,T("Local workspace","Yerel oturum"),aero_dim());

    const i32 rx=x+235;
    const i32 rw=panel_w-257;
    gfx_round_rect(rx,y+24,rw,51,15,aero_soft());
    gfx_round_outline(rx,y+24,rw,51,15,aero_accent());
    gfx_circle_outline(rx+25,y+49,8,aero_dim());
    gfx_line(rx+31,y+55,rx+37,y+61,aero_dim());
    gfx_text(rx+49,y+42,search_length?search_term:
        T("Search applications...","Uygulama ara..."),
        search_length?aero_text():aero_dim());
    if((pit_ms()-opened_at)/475u%2u && search_length<25)
        gfx_rect(rx+51+gfx_text_width(search_term),y+39,2,23,aero_accent());
    const char *heading=search_length?
        T("Search results","Arama sonuclari"):
        (section_id==0?T("Your favorites","Favori uygulamalar"):
         section_id==1?T("System tools","Sistem araclari"):
                       T("All applications","Tum uygulamalar"));
    gfx_text_lg(rx+2,y+97,heading,aero_text());
    gfx_text(rx+3,y+128,T("Native apps and installed packages",
                           "Yerel uygulamalar ve kurulu paketler"),aero_dim());
    const i32 cols=4;
    const i32 gap=12;
    const i32 tile_w=(rw-6-(cols-1)*gap)/cols;
    i32 tile_h=(panel_h-250)/3;
    if(tile_h>144)tile_h=144;
    if(tile_h<78)tile_h=78;
    const i32 grid_y=y+160;
    const i32 start=page*DRAW_PAGE;
    i32 end=start+DRAW_PAGE;
    if(end>visible_count)end=visible_count;
    for(i32 k=start;k<end;k++){
        const i32 slot=k-start,col=slot%cols,row=slot/cols;
        const i32 tx=rx+col*(tile_w+gap),ty=grid_y+row*(tile_h+10);
        const bool hover=hit(mx,my,tx,ty,tile_w,tile_h);
        const bool focus=k==selected;
        aero_card(tx,ty,tile_w,tile_h,16,hover||focus);
        const i32 id=visible[k],cx=tx+tile_w/2;
        gfx_round_rect(cx-25,ty+11,50,49,15,apps_tint(id));
        apps_draw_icon(id,cx,ty+35);
        gfx_text_centered(cx,ty+69,apps_display_name(id),aero_text());
        if(tile_h>102){
            /* App-supplied long descriptions must never bleed into the
             * adjacent tile. Use short, factual, locally translated labels
             * for the primary native applications and truncate others. */
            const char *sub=id==1?T("Local files","Dosyalar"):
                            id==13?T("Web search","Web arama"):
                            id==14?T("Text browser","Metin tarayici"):
                            id==20?T("Screen capture","Ekran kaydi"):
                            id==6?T("Calculator","Hesaplama"):
                            id==7?T("Saved notes","Notlar"):
                            id==3?T("Themes & tools","Tema ve sistem"):
                            id==5?T("Commands","Komutlar"):
                            id==2?T("App catalogue","Uygulamalar"):
                            id==18?T("Code editor","Kod editoru"):
                            apps_subtitle(id);
            if(sub){
                char shortsub[40];
                i32 limit=(tile_w-14)/8;
                if(limit>37)limit=37;
                if(limit<0)limit=0;
                i32 n=0;
                while(sub[n]&&n<limit){shortsub[n]=sub[n];n++;}
                if(sub[n] && n>3){
                    shortsub[n-3]='.';shortsub[n-2]='.';shortsub[n-1]='.';
                }
                shortsub[n]=0;
                gfx_text_centered(cx,ty+94,shortsub,aero_dim());
            }
        }
        if(desktop_pin_is_pinned(id))
            gfx_circle(cx+30,ty+15,5,0x16B88Fu);
        if(hover&&click){
            (void)mouse_consume_click();
            apps_open(id);launchpad_close();return;
        }
    }
    if(!visible_count)
        gfx_text_centered(rx+rw/2,grid_y+90,
            T("No matching apps","Eslesen uygulama yok"),aero_dim());
    gfx_rect(rx,foot-12,rw,1,aero_line());
    char numbers[32],num[12];
    i32 pages=(visible_count+DRAW_PAGE-1)/DRAW_PAGE;
    if(pages<1)pages=1;
    k_strcpy(numbers,T("Page ","Sayfa "));
    k_itoa((u32)(page+1),num,10);k_strcat(numbers,num);
    k_strcat(numbers," / ");k_itoa((u32)pages,num,10);k_strcat(numbers,num);
    gfx_text(rx+4,foot+12,numbers,aero_text());
    gfx_text(rx+4,foot+38,
        T("Arrows navigate  /  Enter open  /  F2 close",
          "Oklar: gezin  /  Enter: ac  /  F2: kapat"),aero_dim());
    const i32 bw=77;
    const i32 right=rx+rw-bw,prev=right-bw-10;
    const bool can_prev=page>0,can_next=page+1<pages;
    aero_chip(prev,foot+7,bw,36,"<",can_prev&&hit(mx,my,prev,foot+7,bw,36));
    aero_chip(right,foot+7,bw,36,">",can_next&&hit(mx,my,right,foot+7,bw,36));
    if(click&&hit(mx,my,prev,foot+7,bw,36)&&can_prev){
        (void)mouse_consume_click();selected=(page-1)*DRAW_PAGE;refresh();
    }else if(click&&hit(mx,my,right,foot+7,bw,36)&&can_next){
        (void)mouse_consume_click();selected=(page+1)*DRAW_PAGE;refresh();
    }
#ifdef FALCON_QEMU_UI_GALLERY
    /* Visual QA marker: real full launcher UI was painted by the guest. */
    outb(0xE9,'L');outb(0xE9,'P');
#endif
}
