/* FalconOS Aura desktop: lightweight Chromebook-inspired shell.
 * Actual applications, not 19 constantly-visible demo widgets.
 * Top: ambient live status. Center: actionable cards.
 * Bottom: floating Shelf with launcher, favorites and actual app state.
 * All drawing is deterministic CPU framebuffer code; no GPU required.
 */
#include "falcon.h"
#include "aeroflow.h"
#define SHELF_SLOTS 7
/* Files, Falco, Browser, Codedium, Discover, Notes and Settings. */
static const i32 SHELF_APPS[SHELF_SLOTS]={1,13,14,18,2,7,3};
static i32 shelf_cursor;
static bool within(i32 px,i32 py,i32 x,i32 y,i32 w,i32 h){
    return px>=x&&px<x+w&&py>=y&&py<y+h;
}
static bool is_dark(void){return SET.theme==THEME_DARK;}
static u32 ink(void){return is_dark()?0xF0F5FFu:0x172E53u;}
static u32 muted(void){return is_dark()?0xB1BCD0u:0x637794u;}
static u32 surface(void){return is_dark()?0x29344Cu:0xFBFDFFu;}
static void use_app(i32 id){if(id>=0&&id<apps_count())apps_open(id);}
void mode_personal_input(i32 key){
    if(apps_active()>=0){apps_input_active(key);return;}
    if(key==KEY_LEFT&&shelf_cursor>0)shelf_cursor--;
    if(key==KEY_RIGHT&&shelf_cursor<SHELF_SLOTS-1)shelf_cursor++;
    if(key==KEY_ENTER||key==' ')use_app(SHELF_APPS[shelf_cursor]);
}
/* AeroFlow desktop header; real activity and clock only. */
static void backdrop(void){
    const i32 w=(i32)FB.width,h=(i32)FB.height;
    /* AeroFlow wallpaper artwork: Falcon wing-lines (native vector pixels,
     * no wallpaper bitmap or fake Windows background). Hide on narrow
     * screens so the functional cards always get priority. */
    if(w>=1350){
        const i32 ax=w-575, ay=h-335;
        for(i32 i=0;i<8;i++){
            gfx_line(ax+9*i,ay+12*i,w-156+7*i,h-485+11*i,PAL_BG_HINT);
            gfx_line(w-156+7*i,h-485+11*i,w-94+5*i,h-524+7*i,
                     PAL_ACCENT_DIM);
        }
        gfx_text_lg(w-385,h-304,"AeroFlow",PAL_TEXT_DIM);
        gfx_rect(w-385,h-268,210,3,PAL_ACCENT_DIM);
        gfx_text(w-383,h-250,T("FALCONOS VISUAL SYSTEM",
                               "FALCONOS GORSEL SISTEM"),PAL_TEXT_DIM);
    }
    aero_panel(31,53,w-62,113,24);
    gfx_round_rect(53,76,61,61,20,aero_accent());
    gfx_text_lg_centered(83,86,"F",aero_accent_ink());
    gfx_text_lg(132,72,"FalconOS",aero_text());
    gfx_text(133,111,T("Your creative workspace / V2 Beta",
                        "Uretim alanin / V2 Beta"),aero_dim());
    gfx_rect(130,134,190,3,aero_accent());
    if(apps_window_count()>0){
        char buf[12];k_itoa((u32)apps_window_count(),buf,10);
        gfx_text(w/2-132,91,T("Open windows","Acik pencereler"),aero_dim());
        gfx_round_rect(w/2+5,85,40,32,12,aero_soft());
        gfx_text_centered(w/2+25,93,buf,aero_text());
    }else if(w>1360){
        gfx_text_centered(w/2+115,93,
            T("DESIGNED TO EXPLORE  /  BUILT TO CREATE",
              "KESFETMEK ICIN TASARLANDI / URETMEK ICIN"),aero_dim());
    }
    rtc_time_t now;rtc_local(&now);
    char clock[18],tmp[12];k_itoa(now.hour,tmp,10);
    k_strcpy(clock,now.hour<10?"0":"");k_strcat(clock,tmp);k_strcat(clock,":");
    k_itoa(now.min,tmp,10);if(now.min<10)k_strcat(clock,"0");
    k_strcat(clock,tmp);
    gfx_text_lg(w-185,81,clock,aero_text());
    gfx_circle(w-177,129,5,net_connected()?0x22BA93u:0xE7A453u);
    gfx_text(w-164,121,net_connected()?
        T("Network","Ag bagli"):T("Offline","Cevrimdisi"),aero_dim());
}
/* Real calendar and runtime status widgets. No placeholder weather/CPU. */
static void real_status_widgets(void){
    const i32 W=(i32)FB.width,H=(i32)FB.height;
    if(W<1660)return; /* keep smaller monitors uncluttered */
    const i32 x=32, y=214, w=296;
    rtc_time_t now;rtc_local(&now);
    aero_panel(x,y,w,206,22);
    gfx_text(x+22,y+18,T("TODAY","BUGUN"),aero_dim());
    char d[14],date[42],yr[16];
    k_itoa(now.day,d,10);k_itoa(now.year,yr,10);
    k_strcpy(date,d);k_strcat(date," ");k_strcat(date,loc_month_short(now.month));
    k_strcat(date," ");k_strcat(date,yr);
    gfx_text_lg(x+22,y+45,date,aero_text());
    gfx_rect(x+22,y+84,w-44,2,aero_accent());
    gfx_text(x+22,y+104,T("Calendar follows your timezone.",
                          "Takvim yerel saati kullanir."),aero_dim());
    aero_chip(x+22,y+147,w-44,39,
              T("Open calendar >","Takvimi ac >"),false);
    i32 mx,my;bool held;mouse_get(&mx,&my,&held);(void)held;
    if(mouse_peek_click()&&within(mx,my,x+22,y+147,w-44,39)){
        (void)mouse_consume_click();use_app(10);return;
    }
    aero_panel(x,y+221,w,164,22);
    gfx_text(x+22,y+240,T("SYSTEM STATUS","SISTEM DURUMU"),aero_dim());
    gfx_circle(x+28,y+280,6,net_connected()?0x20B890u:0xE7A453u);
    gfx_text(x+44,y+272,net_connected()?
        T("Network connected","Ag bagli"):
        T("Network not connected","Ag bagli degil"),aero_text());
    gfx_rect(x+22,y+312,w-44,1,aero_line());
    char ram[40],n[16];k_itoa((u32)(RAM_TOTAL_BYTES/(1024u*1024u)),n,10);
    k_strcpy(ram,"RAM: ");k_strcat(ram,n);k_strcat(ram," MiB");
    gfx_text(x+22,y+327,ram,aero_text());
    gfx_text(x+22,y+352,T("Real firmware memory map","Gercek bellek haritasi"),aero_dim());
}
static void quick_cards(void){
    if(!SET.widgets_shown||apps_active()>=0||launchpad_is_open())return;
    const i32 W=(i32)FB.width,H=(i32)FB.height;
    real_status_widgets();
    i32 content=W-88;
    if(content>1050)content=1050;
    const i32 x=(W-content)/2;
    i32 y=H/2-153;
    if(y<206)y=206;
    gfx_text_lg(x,y-54,T("Your workspace","Calisma alanin"),aero_text());
    gfx_text(x,y-16,T("FAST ACCESS  /  NATIVE FALCONOS APPS",
                      "HIZLI ERISIM  /  YEREL FALCONOS UYGULAMALARI"),aero_dim());
    const char *titles[6]={"Files","Falco","Codedium","Discover","Notes","Settings"};
    const char *sub[6]={"Local files","Web tools","Build packages",
                        "App catalogue","Text editor","Personalize"};
    const i32 ids[6]={1,13,18,2,7,3};
    const i32 gap=13;
    const i32 tw=(content-gap*5)/6;
    i32 mx,my;bool held;mouse_get(&mx,&my,&held);(void)held;
    bool click=mouse_peek_click();
    for(i32 i=0;i<6;i++){
        const i32 bx=x+i*(tw+gap);
        const bool hover=within(mx,my,bx,y+15,tw,161);
        aero_panel(bx,y+15,tw,161,20);
        if(hover){
            gfx_round_outline(bx,y+15,tw,161,20,aero_accent());
            gfx_rect(bx+23,y+17,tw-46,2,aero_accent());
        }
        gfx_round_rect(bx+19,y+31,58,58,18,apps_tint(ids[i]));
        apps_draw_icon(ids[i],bx+48,y+60);
        gfx_text(bx+19,y+104,titles[i],aero_text());
        gfx_text(bx+19,y+133,sub[i],aero_dim());
        if(hover&&click){
            (void)mouse_consume_click();use_app(ids[i]);return;
        }
    }
    const i32 by=y+212;
    aero_panel(x,by,content,90,22);
    gfx_round_rect(x+20,by+21,45,45,15,
                   net_connected()?0x147D6Bu:0xA86B25u);
    gfx_circle(x+42,by+43,9,0xFFFFFFu);
    gfx_text(x+82,by+20,T("System is ready","Sistem hazir"),aero_text());
    gfx_text(x+82,by+48,net_connected()?
        T("Ethernet is connected. HTTPS features depend on their backend.",
          "Ethernet bagli. HTTPS ozellikleri arka servise baglidir."):
        T("Offline mode. Open Settings for connection and hardware details.",
          "Cevrimdisi. Baglanti bilgileri icin Ayarlar'i ac."),aero_dim());
    const i32 bx=x+content-154;
    aero_chip(bx,by+25,133,39,T("Settings >","Ayarlar >"),true);
    if(click&&within(mx,my,bx,by+25,133,39))
        {(void)mouse_consume_click();use_app(3);}
}
/* Floating launcher/taskbar: keeps the existing OS-level input pipeline. */
static void shelf(void){
    const i32 W=(i32)FB.width,H=(i32)FB.height;
    i32 tile=50+SET.dock_size*3;
    if(tile>70)tile=70;
    if(tile<46)tile=46;
    const i32 gap=9;
    const i32 ideal=76+SHELF_SLOTS*(tile+gap)+184;
    i32 w=ideal;
    if(w>W-24)w=W-24;
    const i32 x=(W-w)/2,y=H-97;
    aero_panel(x,y,w,76,23);
    i32 mx,my;bool held;mouse_get(&mx,&my,&held);(void)held;
    bool click=!launchpad_is_open()&&mouse_peek_click();
    const bool hot=within(mx,my,x+11,y+11,54,54);
    gfx_round_rect(x+13,y+13,48,48,15,hot?PAL_ACCENT_DIM:aero_accent());
    gfx_round_outline(x+13,y+13,48,48,15,aero_accent());
    gfx_text_lg_centered(x+37,y+20,"F",hot?aero_accent():aero_accent_ink());
    if(hot&&click){
        (void)mouse_consume_click();launchpad_open();click=false;
    }
    for(i32 i=0;i<SHELF_SLOTS;i++){
        const i32 bx=x+77+i*(tile+gap);
        if(bx+tile>x+w-176)break;
        const i32 id=SHELF_APPS[i];
        const bool hover=within(mx,my,bx,y+10,tile,54);
        const bool running=apps_is_open(id)||apps_minimized()==id;
        if(hover||running||i==shelf_cursor)
            aero_card(bx,y+10,tile,53,15,hover||apps_active()==id);
        apps_draw_icon(id,bx+tile/2,y+37);
        if(running)
            gfx_round_rect(bx+tile/2-11,y+65,22,4,2,
                apps_active()==id?aero_accent():aero_dim());
        if(hover&&click){
            (void)mouse_consume_click();use_app(id);click=false;
        }
    }
    const i32 tray=x+w-169;
    gfx_rect(tray,y+17,1,42,aero_line());
    gfx_circle(tray+20,y+37,6,
        net_connected()?0x22B88Du:0xD49A43u);
    rtc_time_t now;rtc_local(&now);
    char clock[20],hh[8],mm[8];k_itoa(now.hour,hh,10);k_itoa(now.min,mm,10);
    k_strcpy(clock,now.hour<10?"0":"");k_strcat(clock,hh);k_strcat(clock,":");
    if(now.min<10)k_strcat(clock,"0");k_strcat(clock,mm);
    gfx_text(tray+41,y+20,clock,aero_text());
    char date[24],num[8];k_itoa(now.day,num,10);k_strcpy(date,num);
    k_strcat(date," ");k_strcat(date,loc_month_short(now.month));
    gfx_text(tray+41,y+44,date,aero_dim());
    if(capture_studio_recording()){
        gfx_circle(tray+137,y+24,5,COL_ERR);
        gfx_text(tray+119,y+39,"REC",COL_ERR);
    }
    if(click&&within(mx,my,tray,y+8,163,58)){
        (void)mouse_consume_click();use_app(10);
    }
}
void mode_personal_render(u32 frame){
    (void)frame;
    /* A PS/2 click can arrive during expensive 2K framebuffer painting.
     * Only drain an unhandled click if it existed BEFORE this frame began.
     * Otherwise keep the edge queued so the next frame's WM hit-test sees it. */
    bool click_pending_at_frame_start = mouse_peek_click();
    backdrop();
    quick_cards();
    /* Persistent shortcuts are user-controlled and never auto-filled. */
    if(apps_active()<0)desktop_pins_render(frame);
    {
        static bool previous_held;
        i32 mx,my;bool held;mouse_get(&mx,&my,&held);
        /* Preserve a click even if the PS/2 edge was consumed earlier in
         * the frame. Held-button detection also supports drag gestures. */
        bool edge=mouse_peek_click() || (held && !previous_held);
        previous_held=held;
        if(apps_active()>=0 && !launchpad_is_open() &&
           my<(i32)FB.height-102) {
            bool wm_used=apps_wm_handle_mouse(mx,my,held,edge);
            if(edge&&wm_used)(void)mouse_consume_click();
        }
    }
    if(apps_active()<0 && mouse_peek_click() && !launchpad_is_open()){
        i32 mx,my;bool held;mouse_get(&mx,&my,&held);(void)held;
        if(my>70&&my<(i32)FB.height-110&&!within(mx,my,
              ((i32)FB.width-1060)/2,(i32)FB.height/2-178,
              1060,330)){
            (void)mouse_consume_click();
            desktop_pins_input_click(mx,my);
        }
    }
    apps_render_active(frame);
    shelf();
    if(click_pending_at_frame_start && apps_active()>=0 && mouse_peek_click())
        (void)mouse_consume_click();
}
