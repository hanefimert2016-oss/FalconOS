/* FalconOS Aura desktop: lightweight Chromebook-inspired shell.
 * Actual applications, not 19 constantly-visible demo widgets.
 * Top: ambient live status. Center: actionable cards.
 * Bottom: floating Shelf with launcher, favorites and actual app state.
 * All drawing is deterministic CPU framebuffer code; no GPU required.
 */
#include "falcon.h"
#define SHELF_SLOTS 7
/* Files, Falco, Browser, CodeDium, Discover, Notes and Settings. */
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
static void backdrop(void){
    /* Organic light bands give structure without expensive full-screen blur. */
    i32 w=(i32)FB.width,h=(i32)FB.height;
    /* Simple layered broad tints: full-frame supersampled circles at 50fps
     * stalled keyboard processing under QEMU TCG (no GPU acceleration). */
    gfx_round_rect_a(w-420,170,300,92,28,0x86ACF8u,20);
    gfx_round_rect_a(58,h-272,300,72,26,0x4ECFCBu,19);
    gfx_round_rect_a(28,55,w-56,104,26,surface(),is_dark()?178:196);
    gfx_round_outline(28,55,w-56,104,26,is_dark()?0x3E5074u:0xE4ECF7u);
    gfx_round_rect(52,77,54,54,19,0x2867E6u);
    gfx_text_lg_centered(79,85,"F",0xFFFFFFu);
    gfx_text_lg(129,77,T("Welcome to FalconOS","FalconOS'a hos geldin"),ink());
    gfx_text(131,118,T("Your space to create, explore and build",
         "Kesfet, uret ve kendi calisma alanini olustur"),muted());
    rtc_time_t now;rtc_local(&now);
    char time[16],minute[8];k_itoa(now.hour,time,10);
    if(now.hour<10){k_strcpy(minute,time);k_strcpy(time,"0");k_strcat(time,minute);}
    k_strcat(time,":");k_itoa(now.min,minute,10);
    if(now.min<10)k_strcat(time,"0");
    k_strcat(time,minute);
    gfx_text_lg(w-159,79,time,ink());
    gfx_text(w-161,122,net_connected()?"Network ready":"Network offline",
             net_connected()?0x0EA579u:muted());
}
static void quick_cards(void){
    if(!SET.widgets_shown||apps_active()>=0||launchpad_is_open())return;
    i32 W=(i32)FB.width,H=(i32)FB.height;
    i32 content=W-96;
    if(content>1060)content=1060;
    i32 x=(W-content)/2;
    i32 y=H/2-178;
    if(y<184)y=184;
    gfx_text_lg(x,y-52,T("Pick up where you left off","Calismaya devam et"),ink());
    gfx_text(x,y-14,"YOUR WORKSPACE   /   REAL NATIVE APPS",muted());
    const char *titles[6]={"Files","Falco","CodeDium","Discover","Notes","Settings"};
    const char *sub[6]={"Your files","Search + HTTPS","Build .app.pkg","Verified apps","Write & save","Make it yours"};
    const i32 ids[6]={1,13,18,2,7,3};
    const u32 colors[6]={0xF3AD37u,0x3184F6u,0x5369E9u,0x1DB88Fu,0xE9C04Bu,0x7868E9u};
    i32 cw=(content-5*12)/6;
    i32 mx,my;bool held;mouse_get(&mx,&my,&held);(void)held;
    bool click=mouse_peek_click();
    for(i32 i=0;i<6;i++){
        i32 bx=x+i*(cw+12);
        bool hov=within(mx,my,bx,y+19,cw,165);
        gfx_round_rect_a(bx+3,y+25,cw,165,22,0x0C2248u,29);
        gfx_round_rect(bx,y+19,cw,165,22,surface());
        gfx_round_outline(bx,y+19,cw,165,22,
             hov?0x719FEFu:(is_dark()?0x506080u:0xD6E2F3u));
        gfx_round_rect(bx+18,y+38,56,56,18,colors[i]);
        apps_draw_icon(ids[i],bx+46,y+66);
        gfx_text(bx+19,y+116,titles[i],ink());
        gfx_text(bx+19,y+143,sub[i],muted());
        if(hov&&click){
            (void)mouse_consume_click();use_app(ids[i]);return;
        }
    }
    i32 by=y+218;
    gfx_round_rect(x,by,content,74,22,surface());
    gfx_round_outline(x,by,content,74,22,is_dark()?0x506080u:0xD9E5F5u);
    gfx_circle(x+34,by+36,12,net_connected()?0x26B98Au:0xEAAE50u);
    gfx_text(x+62,by+18,"System & network",ink());
    gfx_text(x+62,by+42,net_connected()?
       "Network ready. Falco uses native TLS (text-only pages).":
       "Offline. Configure RTL8139 + DHCP for Falco and Discover.",muted());
    gfx_round_rect(x+content-145,by+19,123,36,14,0xE6EEFFu);
    gfx_text_centered(x+content-83,by+30,"Settings",0x2758AEu);
    if(click&&within(mx,my,x+content-151,by+10,140,55)){
        (void)mouse_consume_click();apps_open(3);
    }
}
static void shelf(void){
    i32 W=(i32)FB.width,H=(i32)FB.height;
    i32 tile=50+SET.dock_size*3;
    if(tile>70)tile=70;
    if(tile<50)tile=50;
    i32 gap=9;
    i32 mainw=76+SHELF_SLOTS*(tile+gap)+20;
    i32 w=mainw+170;
    if(w>W-22)w=W-22;
    i32 x=(W-w)/2,y=H-94;
    if(y<340)y=340;
    gfx_round_rect_a(x+4,y+6,w,74,26,0x07172Fu,65);
    gfx_round_rect_a(x,y,w,74,26,surface(),239);
    gfx_round_outline(x,y,w,74,26,is_dark()?0x627595u:0xD5E2F4u);
    i32 mx,my;bool held;mouse_get(&mx,&my,&held);(void)held;
    bool can_click=!launchpad_is_open();
    bool click=can_click&&mouse_peek_click();
    bool launcher=within(mx,my,x+12,y+11,53,53);
    gfx_round_rect(x+13,y+13,48,48,17,launcher?0x4088FFu:0x2869E8u);
    gfx_text_lg_centered(x+37,y+20,"F",0xFFFFFFu);
    if(launcher&&click){
        (void)mouse_consume_click();
        launchpad_open();click=false;
    }
    for(i32 i=0;i<SHELF_SLOTS;i++){
        i32 bx=x+77+i*(tile+gap);
        if(bx+tile>x+w-168)break;
        bool hovered=within(mx,my,bx,y+11,tile,53);
        i32 id=SHELF_APPS[i];
        bool running=(apps_is_open(id)||apps_minimized()==id);
        if(hovered||running||i==shelf_cursor)
            gfx_round_rect_a(bx,y+11,tile,52,16,
                hovered?0xBFD8FCu:0xDDE9FAu,190);
        apps_draw_icon(id,bx+tile/2,y+37);
        if(running)gfx_circle(bx+tile/2,y+66,3,0x246FE7u);
        if(hovered&&click){
            (void)mouse_consume_click();use_app(id);click=false;
        }
    }
    i32 tray=x+w-170;
    gfx_rect(tray,y+18,1,37,is_dark()?0x697B9Cu:0xDCE6F4u);
    gfx_circle(tray+26,y+33,6,net_connected()?0x24B68Au:0xB7BECEu);
    rtc_time_t now;rtc_local(&now);
    char hh[8],mm[8],clock[20];k_itoa(now.hour,hh,10);
    k_itoa(now.min,mm,10);
    k_strcpy(clock,now.hour<10?"0":"");k_strcat(clock,hh);k_strcat(clock,":");
    if(now.min<10)k_strcat(clock,"0");k_strcat(clock,mm);
    gfx_text(tray+44,y+31,clock,ink());
    if(click&&within(mx,my,tray,y+6,161,63)){
        (void)mouse_consume_click();apps_open(3);click=false;
    }
}
void mode_personal_render(u32 frame){
    (void)frame;
    backdrop();
    quick_cards();
    /* Persistent shortcuts are user-controlled and never auto-filled. */
    if(apps_active()<0)desktop_pins_render(frame);
    if(apps_active()>=0){
        i32 mx,my;bool held;mouse_get(&mx,&my,&held);
        bool edge=mouse_peek_click();
        /* Shelf has its own hit-testing and must not drag the app window. */
        if(my<(i32)FB.height-102){
            bool wm_used=apps_wm_handle_mouse(mx,my,held,edge);
            if(edge&&wm_used)(void)mouse_consume_click();
        }
    }else if(mouse_peek_click()&&!launchpad_is_open()){
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
    if(apps_active()>=0&&mouse_peek_click())
        (void)mouse_consume_click();
}
