/* =============================================================================
 *  FalconOS — application framework (Personal kernel, v4 "Lumen")
 * -----------------------------------------------------------------------------
 *  12 self-contained mini-apps surfaced through the dock & Launchpad.  Each
 *  app has a name, dock tint, render() callback for window contents, optional
 *  input() callback, and an icon glyph drawer.
 *
 *  Built-in apps (12):
 *      Home       - quick-link cards
 *      Files      - in-memory file browser
 *      Clock      - analog dial driven by PIT uptime
 *      Stats      - live tsc / uptime / RAM
 *      About      - system info & credits
 *      Terminal   - fake POSIX-style prompt
 *      Calculator - 4-fn arithmetic (keyboard input)
 *      Settings   - toggles & accent color preview
 *      Notes      - free-form note pad (keyboard input)
 *      Calendar   - month grid with PIT-driven "today"
 *      Gallery    - color swatches for the Lumen palette
 *      Browser    - fake URL bar + bookmark cards
 * ============================================================================= */
#include "falcon.h"
#include "shfs.h"

static i32 active_app = -1;
static u32 open_at_ms = 0;     /* used for slide-in animation */
static i32 minimized_app = -1; /* last app sent to dock by yellow light */
static void falco_set_query(const char *q);
static void falco_open_site(const char *address);
static void falco_open_site_host(const char *address);
static void chrome_input_key(i32 key);
static void chrome_focus_url(void);
static void render_browser(i32 x,i32 y,i32 w,i32 h,u32 frame);
static void market_launch(i32 i);
static i32 builtin_app_count(void);

/* ---- Contour native window stack (up to 4 simultaneous application windows).
 * No heap / GPU dependency. Each built-in app remains a singleton, but
 * different applications can be shown, positioned and focused concurrently.
 * Geometry lives per window; the selected window alone receives keyboard
 * and mouse events. This is OS framebuffer code, not the web preview. */
#define WM_MAX_WINDOWS 8
typedef struct { i32 app, dx, dy, dw, dh; bool maximized, minimized; } wm_slot_t;
static wm_slot_t wm_slots[WM_MAX_WINDOWS];
static i32 wm_slot_count;
static i32 wm_dx, wm_dy, wm_dw, wm_dh;
static bool wm_max, wm_dragging, wm_resizing, wm_passive_paint;
static i32 wm_drag_grab_x, wm_drag_grab_y;
static i32 wm_resize_grab_x, wm_resize_grab_y;
static i32 wm_resize_start_w, wm_resize_start_h;

static bool wm_click_enabled(void) {
    return !wm_passive_paint && mouse_peek_click();
}
static void wm_store_top(void) {
    if (wm_slot_count <= 0 || active_app < 0) return;
    wm_slot_t *w=&wm_slots[wm_slot_count-1];
    w->dx=wm_dx; w->dy=wm_dy; w->dw=wm_dw; w->dh=wm_dh;
    w->maximized=wm_max;
}
static void wm_load_top(void) {
    if (wm_slot_count <= 0) {
        active_app=-1; wm_dx=wm_dy=wm_dw=wm_dh=0; wm_max=false;
        return;
    }
    const wm_slot_t *w=&wm_slots[wm_slot_count-1];
    active_app=w->minimized?-1:w->app;
    wm_dx=w->dx; wm_dy=w->dy;
    wm_dw=w->dw; wm_dh=w->dh; wm_max=w->maximized;
}
static void wm_raise(i32 index) {
    if (index<0||index>=wm_slot_count)return;
    wm_store_top();
    if(index==wm_slot_count-1){
        wm_slots[index].minimized=false;wm_load_top();return;
    }
    wm_slot_t raised=wm_slots[index];
    raised.minimized=false;
    for(i32 j=index;j<wm_slot_count-1;j++) wm_slots[j]=wm_slots[j+1];
    wm_slots[wm_slot_count-1]=raised;
    wm_load_top();
    wm_dragging=wm_resizing=false;
    open_at_ms=pit_ms();
}
static void wm_minimize_top(void) {
    if(wm_slot_count<=0 || active_app<0)return;
    wm_store_top();
    wm_slot_t hidden=wm_slots[wm_slot_count-1];
    hidden.minimized=true;
    for(i32 j=wm_slot_count-1;j>0;j--)wm_slots[j]=wm_slots[j-1];
    wm_slots[0]=hidden;
    minimized_app=hidden.app;
    wm_load_top();
    wm_dragging=wm_resizing=false;
}
i32 apps_window_count(void) {return wm_slot_count;}
bool apps_is_open(i32 app) {
    for(i32 j=0;j<wm_slot_count;j++)if(wm_slots[j].app==app)return true;
    return false;
}
void apps_open(i32 app) {
    if(app<0||app>=apps_count())return;
    /* Never allow a translucent help sheet to consume the next click
     * intended for the opened window's titlebar traffic lights. */
    if(helppanel_is_open())helppanel_close();
    if(app==2) market_refresh(); /* Discover requests real releases on open */
    if(app>=builtin_app_count()) {
        i32 idx=app-builtin_app_count();
        if(market_installed(idx))market_launch(idx);
        else { apps_open(2); market_download(idx); }
        return;
    }
    wm_store_top();
    for(i32 j=0;j<wm_slot_count;j++) {
        if(wm_slots[j].app==app) {
            wm_raise(j);
            minimized_app=-1; open_at_ms=pit_ms();
            outb(0xE9,'z');outb(0xE9,(u8)('A'+app));
            outb(0xE9,'n');outb(0xE9,(u8)('0'+wm_slot_count));
            return;
        }
    }
    if(wm_slot_count==WM_MAX_WINDOWS) {
        for(i32 j=1;j<wm_slot_count;j++)wm_slots[j-1]=wm_slots[j];
        wm_slot_count--;
    }
    /* Stagger windows across the desktop instead of stacking every title
     * bar on top of the same place.  Keeps background windows selectable. */
    static const i32 x_off[8]={-190,145,-115,220,-240,70,-75,175};
    static const i32 y_off[8]={-95,-35,45,105,0,80,115,-115};
    i32 cascade=wm_slot_count;
    wm_slot_t item={.app=app,.dx=x_off[cascade],.dy=y_off[cascade],
                    .dw=0,.dh=0,.maximized=false,.minimized=false};
    wm_slots[wm_slot_count++]=item;
    wm_load_top();
    if(app==2)outb(0xE9,'S');
    outb(0xE9,'z');outb(0xE9,(u8)('A'+app));
    outb(0xE9,'n');outb(0xE9,(u8)('0'+wm_slot_count));
    minimized_app=-1; open_at_ms=pit_ms();
    wm_dragging=wm_resizing=false;
}
void apps_close(void) {
    if(wm_slot_count>0&&active_app>=0)wm_slot_count--;
    wm_load_top();
    wm_dragging=wm_resizing=false;
}
i32 apps_active(void) {return active_app;}
i32 apps_minimized(void) {return minimized_app;}
void apps_close_all(void) {
    wm_slot_count=0;wm_dx=wm_dy=wm_dw=wm_dh=0;
    active_app=-1;minimized_app=-1;wm_max=false;
    wm_dragging=wm_resizing=false;
}

/* ===== icon glyphs ======================================================== */
static void icon_home(i32 cx, i32 cy)
{
    /* solid rounded square + cleaner roofline + door */
    gfx_round_rect(cx - 16, cy - 14, 32, 30, 6, PAL_ACCENT);
    /* white roof triangle */
    for (i32 dy = -14; dy <= -4; dy++) {
        i32 half = 14 + dy;            /* widens as dy moves toward -4 */
        gfx_rect(cx - half, cy + dy, half * 2 + 1, 1, 0xFFFFFF);
    }
    /* white door */
    gfx_round_rect(cx - 4, cy + 2, 8, 12, 2, 0xFFFFFF);
    gfx_pixel(cx + 2, cy + 8, PAL_ACCENT);
}
static void icon_files(i32 cx, i32 cy)
{
    /* manila folder with tab + page peeking out */
    gfx_round_rect(cx - 16, cy - 12, 14, 6, 3, 0xFFC56C);
    gfx_round_rect(cx - 16, cy -  8, 32, 22, 4, 0xFFD58A);
    gfx_round_rect(cx - 12, cy -  4, 24, 14, 3, 0xFFFFFF);
    gfx_rect(cx - 9, cy +  0, 18, 1, 0xC8AA80);
    gfx_rect(cx - 9, cy +  4, 14, 1, 0xC8AA80);
}
static void icon_clock(i32 cx, i32 cy)
{
    /* circular dial with 12 tick marks + hour & minute hands */
    gfx_circle(cx, cy, 17, PAL_TEXT);
    gfx_circle(cx, cy, 15, 0xFFFFFF);
    gfx_pixel(cx,      cy - 12, PAL_TEXT);
    gfx_pixel(cx,      cy + 12, PAL_TEXT);
    gfx_pixel(cx - 12, cy,      PAL_TEXT);
    gfx_pixel(cx + 12, cy,      PAL_TEXT);
    gfx_line(cx, cy, cx,     cy -  9, PAL_TEXT);   /* hour   */
    gfx_line(cx, cy, cx + 8, cy +  3, COL_ERR);    /* minute */
    gfx_circle(cx, cy, 2, COL_ERR);
}
static void icon_stats(i32 cx, i32 cy)
{
    /* tinted card behind stepped bars */
    gfx_round_rect(cx - 16, cy - 14, 32, 28, 5, PAL_PANEL_DEEP);
    for (i32 i = 0; i < 5; i++) {
        i32 h = 4 + (i * 3);
        u32 c = (i == 4) ? COL_OK : (i >= 2 ? PAL_ACCENT : COL_WARN);
        gfx_round_rect(cx - 12 + i * 5, cy + 9 - h, 3, h, 1, c);
    }
}
static void icon_about(i32 cx, i32 cy)
{
    /* falcon emblem — accent-tinted disc with stylised wing */
    gfx_circle(cx, cy, 17, PAL_ACCENT);
    gfx_circle(cx, cy, 14, 0xFFFFFF);
    gfx_line(cx - 8, cy + 4, cx + 4, cy - 6, PAL_ACCENT);
    gfx_line(cx - 4, cy + 4, cx + 8, cy - 2, PAL_ACCENT);
    gfx_pixel(cx + 6, cy - 4, COL_ERR);
}
static void icon_term(i32 cx, i32 cy)
{
    /* dark window with chrome strip + prompt cursor */
    gfx_round_rect(cx - 16, cy - 13, 32, 26, 4, 0x10141C);
    gfx_rect(cx - 16, cy - 13, 32, 5, 0x1B2129);
    gfx_circle(cx - 12, cy - 11, 1, COL_ERR);
    gfx_circle(cx -  8, cy - 11, 1, COL_WARN);
    gfx_circle(cx -  4, cy - 11, 1, COL_OK);
    gfx_text(cx - 12, cy - 4, ">_", COL_OK);
}
static void icon_calc(i32 cx, i32 cy)
{
    /* rounded body with screen + 3x3 keypad + accent equals */
    gfx_round_rect(cx - 15, cy - 15, 30, 30, 5, PAL_PANEL);
    gfx_round_rect(cx - 11, cy - 11, 22,  7, 2, 0x202836);
    for (i32 i = 0; i < 3; i++)
        for (i32 j = 0; j < 3; j++) {
            u32 c = (i == 2 && j == 2) ? PAL_ACCENT : PAL_TEXT_DIM;
            gfx_round_rect(cx - 11 + j * 7, cy + 0 + i * 5, 5, 3, 1, c);
        }
}
static void icon_settings(i32 cx, i32 cy)
{
    /* 8-tooth gear: outer flange ring + inner accent core */
    gfx_circle(cx, cy, 15, PAL_PANEL_DEEP);
    /* teeth around 8 cardinal positions */
    static const i32 TX[8] = { 0, 11, 14, 11,  0,-11,-14,-11 };
    static const i32 TY[8] = {-14,-11, 0, 11, 14, 11,  0,-11 };
    for (i32 a = 0; a < 8; a++)
        gfx_round_rect(cx + TX[a] - 2, cy + TY[a] - 2, 5, 5, 1, PAL_PANEL_DEEP);
    gfx_circle(cx, cy, 11, PAL_PANEL);
    gfx_circle(cx, cy,  5, PAL_ACCENT);
    gfx_circle(cx, cy,  2, 0xFFFFFF);
}
static void icon_notes(i32 cx, i32 cy)
{
    /* paper card with yellow highlight strip + ruled lines */
    gfx_round_rect(cx - 14, cy - 14, 28, 28, 4, 0xFFFFFF);
    gfx_rect(cx - 14, cy - 14, 28, 6, 0xFFE082);
    gfx_rect(cx - 11, cy -  4, 22, 1, PAL_TEXT_FAINT);
    gfx_rect(cx - 11, cy +  0, 18, 1, PAL_TEXT_FAINT);
    gfx_rect(cx - 11, cy +  4, 22, 1, PAL_TEXT_FAINT);
    gfx_rect(cx - 11, cy +  8, 14, 1, PAL_TEXT_FAINT);
}
static void icon_calendar(i32 cx, i32 cy)
{
    /* page with red header band + binding tabs + day grid */
    gfx_round_rect(cx - 14, cy - 14, 28, 28, 4, 0xFFFFFF);
    gfx_round_rect(cx - 14, cy - 14, 28,  9, 4, COL_ERR);
    gfx_rect(cx - 9, cy - 16, 2, 4, PAL_TEXT);
    gfx_rect(cx + 7, cy - 16, 2, 4, PAL_TEXT);
    for (i32 r = 0; r < 3; r++)
        for (i32 c = 0; c < 5; c++)
            gfx_round_rect(cx - 11 + c * 5, cy - 2 + r * 5, 3, 3, 1,
                            (r == 1 && c == 2) ? PAL_ACCENT : PAL_TEXT_FAINT);
}
static void icon_gallery(i32 cx, i32 cy)
{
    /* photo frame with mountains + sun */
    gfx_round_rect(cx - 16, cy - 13, 32, 26, 4, 0xFFFFFF);
    gfx_round_rect(cx - 14, cy - 11, 28, 22, 3, 0x9CC2EE);
    gfx_circle(cx + 6, cy - 5, 3, 0xFFD580);
    /* triangular mountains */
    for (i32 i = 0; i < 8; i++)
        gfx_rect(cx - 12 + i, cy + 1 - i, 3, i + 1, 0x4F6E92);
    for (i32 i = 0; i < 6; i++)
        gfx_rect(cx - 4 + i, cy + 3 - i, 3, i + 1, 0x344C6E);
}
static void icon_browser(i32 cx, i32 cy)
{
    /* Chrome-style multi-colour wheel: red / yellow / green outer ring,
     * blue centre dot. Drawn with three pie wedges + a centre fill so
     * the silhouette reads as Chrome at any size.                       */
    gfx_circle(cx, cy, 16, 0xF8F8F8);                 /* white halo     */
    gfx_circle_a(cx, cy, 14, 0xEA4335, 0xFF);         /* red top-left   */
    /* mask out top-right with yellow                                    */
    for (i32 dy = -14; dy <= 0; dy++)
        for (i32 dx = 0; dx <= 14; dx++)
            if (dx*dx + dy*dy <= 14*14)
                gfx_pixel(cx + dx, cy + dy, 0xFBBC04);
    /* mask out bottom half with green                                  */
    for (i32 dy = 1; dy <= 14; dy++)
        for (i32 dx = -14; dx <= 14; dx++)
            if (dx*dx + dy*dy <= 14*14)
                gfx_pixel(cx + dx, cy + dy, 0x34A853);
    gfx_circle(cx, cy, 6, 0x4285F4);                  /* blue centre    */
    gfx_circle_outline(cx, cy, 6, 0xFFFFFF);
}
static void icon_falco(i32 cx, i32 cy)
{
    /* Falco mark: blue dragon head in a rounded badge. */
    gfx_round_rect(cx - 16, cy - 16, 32, 32, 8, 0x163C8C);
    gfx_round_outline(cx - 16, cy - 16, 32, 32, 8, PAL_HAIRLINE);
    gfx_circle(cx - 3, cy + 2, 11, 0x2A66F5);
    gfx_circle(cx + 7, cy - 5, 7, 0x2A66F5);
    gfx_circle(cx + 11, cy - 12, 3, 0xBDE2FF);
    gfx_line(cx - 10, cy + 10, cx - 15, cy + 15, 0x123A9C);
    gfx_line(cx - 7,  cy + 5,  cx - 15, cy + 8, 0x123A9C);
}
static void icon_store(i32 cx, i32 cy)
{
    /* shopping bag with "P" */
    gfx_round_rect(cx - 12, cy - 8, 24, 18, 3, COL_PANEL);
    gfx_line(cx - 8, cy - 8, cx - 8, cy - 14, COL_PANEL);
    gfx_line(cx + 8, cy - 8, cx + 8, cy - 14, COL_PANEL);
    gfx_line(cx - 8, cy - 14, cx + 8, cy - 14, COL_PANEL);
    gfx_text_centered(cx, cy - 6, "P", COL_ACCENT);
}
static void icon_updates(i32 cx, i32 cy)
{
    /* downward refresh arrow */
    gfx_round_rect(cx - 13, cy - 12, 26, 20, 4, PAL_PANEL_DEEP);
    gfx_round_outline(cx - 13, cy - 12, 26, 20, 4, PAL_HAIRLINE);
    gfx_line(cx - 5, cy - 8, cx, cy + 8, PAL_ACCENT);
    gfx_line(cx + 5, cy - 8, cx, cy + 8, PAL_ACCENT);
    gfx_line(cx - 10, cy + 6, cx + 10, cy + 6, PAL_ACCENT);
}
static void icon_video(i32 cx, i32 cy)
{
    gfx_round_rect(cx - 16, cy - 12, 32, 24, 6, 0x10141C);
    gfx_round_outline(cx - 16, cy - 12, 32, 24, 6, PAL_HAIRLINE);
    for (i32 y = -9; y <= 9; y++)
        for (i32 x = -13; x <= 13; x++) {
            u8 a = (u8)(120 + ((x + 13) * 80) / 26);
            gfx_pixel_a(cx + x, cy + y, PAL_ACCENT, a);
        }
    for (i32 y = -5; y <= 5; y++)
        for (i32 x = -2; x <= 6; x++)
            if (x + y / 2 >= -2 && x - y / 2 <= 6)
                gfx_pixel(cx + x, cy + y, 0xFFFFFF);
}
static void icon_heroic(i32 cx, i32 cy)
{
    gfx_round_rect(cx - 16, cy - 16, 32, 32, 8, 0x1E2635);
    gfx_round_outline(cx - 16, cy - 16, 32, 32, 8, PAL_HAIRLINE);
    gfx_text_centered(cx, cy - 8, "H", PAL_ACCENT);
    gfx_rect(cx - 8, cy + 2, 16, 2, 0x34A853);
    gfx_rect(cx - 6, cy + 6, 12, 2, 0xFBBC04);
    gfx_rect(cx - 4, cy + 10, 8, 2, 0xEA4335);
}

/* ===== app render functions ============================================== */

/* shared section header */
static void section(i32 wx, i32 wy, const char *title, const char *subtitle)
{
    gfx_text(wx + 24, wy + 6,  title,    PAL_TEXT);
    gfx_text(wx + 24, wy + 28, subtitle, PAL_TEXT_DIM);
}

/* --- Home: quick-link cards ---------------------------------------------- */
static void render_home(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame;
    section(wx, wy,
            T("Welcome to FalconOS", "FalconOS'a hoş geldiniz"),
            T("Quick links", "Hızlı bağlantılar"));

    const char *c0 = T("Recent docs", "Son dosyalar");
    const char *c1 = T("System info", "Sistem bilgisi");
    const char *c2 = T("Network", "Ağ");
    const char *c3 = T("Theme", "Tema");
    const char *s0 = T("open last 5", "dosya demo");
    const char *s1 = T("uptime / RAM map", "çalışma / RAM");
    const char *s2 = T("no driver yet — virtio-net roadmap", "sürücü yok — virtio-net");
    const char *s3 = T("Liquid Glass default", "Liquid Glass varsayılan");
    const char *cards[] = { c0, c1, c2, c3 };
    const char *subs[]  = { s0, s1, s2, s3 };
    const u32   tints[]    = { PAL_ACCENT, COL_OK, COL_WARN, COL_PURPLE };

    i32 cw = (ww - 72) / 2;
    i32 ch = (wh - 100) / 2;
    for (i32 i = 0; i < 4; i++) {
        i32 r = i / 2, c = i % 2;
        i32 x = wx + 24 + c * (cw + 16);
        i32 y = wy + 60 + r * (ch + 16);
        gfx_round_rect_a(x, y, cw, ch, 12, PAL_PANEL_DEEP, 255);
        gfx_round_outline(x, y, cw, ch, 12, PAL_HAIRLINE);
        gfx_circle(x + 26, y + 24, 12, tints[i]);
        gfx_text(x + 50, y + 12, cards[i], PAL_TEXT);
        gfx_text(x + 50, y + 36, subs[i],  PAL_TEXT_DIM);
    }
}

/* --- Files: live SHFS browser with real folders and a searchable list. ---
 * This UI never fabricates files; only SHFS enumerated entries are displayed.
 * F4 search, Left/Right locations, Up/Down scroll. The same exact file state
 * is shared with Notes, Codedium and downloaded FAPP/1 packages. */
static i32 files_scroll, files_scope;
static bool files_search_mode;
static char files_filter[40];
static i32 files_filter_len;
static const char *FILES_ROOTS[4] = {
    "", "/home/falcon", "/home/falcon/Desktop", "/home/falcon/apps"
};
static const char *FILES_LABELS[4] = { "All files", "Home", "Desktop", "Apps" };
static bool files_matches(const char *path) {
    const char *root=FILES_ROOTS[files_scope];
    i32 root_len=k_strlen(root);
    if(root_len && k_strncmp(path,root,root_len)!=0)return false;
    if(!files_filter_len)return true;
    for(i32 i=0;path[i];i++) {
        i32 j=0;
        while(files_filter[j]&&path[i+j]) {
            char a=path[i+j],b=files_filter[j];
            if(a>='A'&&a<='Z')a+=32;
            if(b>='A'&&b<='Z')b+=32;
            if(a!=b)break;
            j++;
        }
        if(!files_filter[j])return true;
    }
    return false;
}
static void files_input_key(i32 key) {
    if(key==KEY_F4){files_search_mode=!files_search_mode;return;}
    if(files_search_mode){
        if(key==KEY_BACKSPACE){
            if(files_filter_len>0)files_filter[--files_filter_len]=0;
            files_scroll=0;
        }else{
            char bytes[4];i32 n=key_to_utf8(key,bytes);
            if(n>0&&files_filter_len+n<(i32)sizeof files_filter){
                for(i32 i=0;i<n;i++)files_filter[files_filter_len++]=bytes[i];
                files_filter[files_filter_len]=0;files_scroll=0;
            }
        }
        return;
    }
    if(key==KEY_LEFT){files_scope=(files_scope+3)%4;files_scroll=0;}
    if(key==KEY_RIGHT){files_scope=(files_scope+1)%4;files_scroll=0;}
    if(key==KEY_UP&&files_scroll>0)files_scroll--;
    if(key==KEY_DOWN&&files_scroll<SHFS_MAX_ENTRIES-1)files_scroll++;
}
typedef struct {i32 x,y,w,limit,row,seen,files,dirs;} file_draw_ctx_t;
static void files_row(const char *path, bool is_dir, u32 len, void *ud) {
    file_draw_ctx_t *c=(file_draw_ctx_t *)ud;
    if(!files_matches(path))return;
    if(is_dir)c->dirs++;else c->files++;
    if(c->seen++<files_scroll||c->row>=c->limit)return;
    i32 y=c->y+c->row*37;
    gfx_round_rect_a(c->x,y,c->w,34,10,
        (c->row&1)?PAL_PANEL:PAL_PANEL_DEEP,255);
    gfx_round_rect(c->x+9,y+7,24,20,6,
        is_dir?0xF3BC5Du:0x548FEBu);
    gfx_text_centered(c->x+21,y+10,is_dir?"D":"F",0xFFFFFFu);
    const char *name=path;
    for(i32 i=0;path[i];i++)if(path[i]=='/'&&path[i+1])name=path+i+1;
    char clipped[53];i32 max=(c->w-135)/8;
    if(max<10)max=10;if(max>52)max=52;
    i32 k=0;while(name[k]&&k<max){clipped[k]=name[k];k++;}
    clipped[k]=0;
    gfx_text(c->x+42,y+10,clipped,PAL_TEXT);
    if(!is_dir){
        char digits[16];k_itoa(len,digits,10);
        gfx_text(c->x+c->w-83,y+10,digits,PAL_TEXT_DIM);
        gfx_text(c->x+c->w-44,y+10,"B",PAL_TEXT_FAINT);
    }
    c->row++;
}
static void render_files(i32 wx,i32 wy,i32 ww,i32 wh,u32 frame){
    (void)frame;shfs_init();
    i32 margin=18,sidebar=122,top=91,bottom=45;
    i32 sx=wx+margin,main_x=sx+sidebar+14;
    i32 main_w=ww-2*margin-sidebar-14;
    gfx_round_rect_a(sx,wy+10,ww-2*margin,65,16,PAL_PANEL_DEEP,255);
    gfx_round_rect(sx+14,wy+23,36,36,11,0xF3BC5Du);
    gfx_text_centered(sx+32,wy+33,"F",0xFFFFFFu);
    gfx_text_lg(sx+62,wy+16,T("Files","Dosyalar"),PAL_TEXT);
    gfx_text(sx+62,wy+51,
        T("Your actual FalconOS files","FalconOS dosyalarin"),PAL_TEXT_DIM);
    gfx_round_rect_a(sx,wy+top,sidebar,wh-top-bottom,13,PAL_PANEL_DEEP,255);
    i32 mx,my;bool held;mouse_get(&mx,&my,&held);(void)held;
    bool clicked=wm_click_enabled();
    for(i32 i=0;i<4;i++){
        i32 sy=wy+top+13+i*43;
        bool chosen=i==files_scope;
        gfx_round_rect(sx+7,sy,sidebar-14,36,11,
            chosen?PAL_ACCENT_DIM:PAL_PANEL);
        gfx_text(sx+19,sy+11,FILES_LABELS[i],chosen?PAL_ACCENT:PAL_TEXT_DIM);
        if(clicked&&mx>=sx+7&&mx<sx+sidebar-7&&my>=sy&&my<sy+36){
            files_scope=i;files_scroll=0;
            (void)mouse_consume_click();clicked=false;
        }
    }
    gfx_text(sx+13,wy+wh-76,"SHFS / RAM",PAL_TEXT_DIM);
    gfx_text(sx+13,wy+wh-56,"Real storage",PAL_TEXT_FAINT);
    gfx_round_rect_a(main_x,wy+top,main_w,38,11,PAL_PANEL_DEEP,255);
    gfx_circle_outline(main_x+19,wy+top+18,7,PAL_ACCENT);
    gfx_line(main_x+24,wy+top+23,main_x+30,wy+top+29,PAL_ACCENT);
    gfx_text(main_x+38,wy+top+12,
        files_filter_len?files_filter:(files_search_mode?"Type to search...":"F4: Search files"),
        files_filter_len?PAL_TEXT:PAL_TEXT_DIM);
    if(clicked&&mx>=main_x&&mx<main_x+main_w&&my>=wy+top&&my<wy+top+38){
        files_search_mode=true;(void)mouse_consume_click();
    }
    file_draw_ctx_t ctx={main_x,wy+top+49,main_w,
        (wh-top-bottom-53)/37,0,0,0,0};
    if(ctx.limit<1)ctx.limit=1;
    shfs_foreach_path(files_row,&ctx);
    if(ctx.seen==0)
        gfx_text(main_x+16,wy+top+89,
            files_filter_len?"No matching files":"No files in this location",PAL_TEXT_DIM);
    gfx_rect(sx,wy+wh-36,ww-2*margin,1,PAL_HAIRLINE);
    char count[16];k_itoa(ctx.files,count,10);
    gfx_text(sx+4,wy+wh-26,count,PAL_ACCENT);
    gfx_text(sx+35,wy+wh-26,"files  |  F4 search  |  Left/Right folders  |  Up/Down scroll",
        PAL_TEXT_DIM);
}

static void render_clock(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame;
    section(wx, wy, T("Clock", "Saat"),
            T("Synced to timer IRQ0", "IRQ0 zamanlayıcı ile"));
    u32 h, m, s; pit_uptime(&h, &m, &s);

    i32 cx = wx + ww / 2;
    i32 cy = wy + wh / 2 + 10;
    i32 R  = (wh - 100) / 2;
    if (R > 130) R = 130;

    gfx_circle(cx, cy, R + 8, PAL_HAIRLINE);
    gfx_circle(cx, cy, R,     PAL_PANEL_DEEP);

    static const i8 SX[12] = {  0, 50, 87,100, 87, 50,  0,-50,-87,-100,-87,-50};
    static const i8 SY[12] = {-100,-87,-50,  0, 50, 87,100, 87, 50,   0,-50,-87};
    for (i32 i = 0; i < 12; i++) {
        i32 ex = cx + (i32)SX[i] * R / 100;
        i32 ey = cy + (i32)SY[i] * R / 100;
        gfx_circle(ex, ey, 3, PAL_TEXT_DIM);
    }

    static const i8 LX[60] = {
         0, 10, 21, 31, 41, 50, 59, 67, 74, 81, 87, 91, 95, 98, 99,100, 99, 98, 95, 91,
        87, 81, 74, 67, 59, 50, 41, 31, 21, 10,  0,-10,-21,-31,-41,-50,-59,-67,-74,-81,
       -87,-91,-95,-98,-99,-100,-99,-98,-95,-91,-87,-81,-74,-67,-59,-50,-41,-31,-21,-10};
    static const i8 LY[60] = {
       -100,-99,-98,-95,-91,-87,-81,-74,-67,-59,-50,-41,-31,-21,-10,  0, 10, 21, 31, 41,
         50, 59, 67, 74, 81, 87, 91, 95, 98, 99,100, 99, 98, 95, 91, 87, 81, 74, 67, 59,
         50, 41, 31, 21, 10,  0,-10,-21,-31,-41,-50,-59,-67,-74,-81,-87,-91,-95,-98,-99};

    i32 si = (i32)(s % 60);
    i32 mi = (i32)(m % 60);
    i32 hi = (i32)(((h % 12) * 5 + m / 12) % 60);

    gfx_line(cx, cy, cx + (i32)LX[hi] * (R - 40) / 100,
                      cy + (i32)LY[hi] * (R - 40) / 100, PAL_TEXT);
    gfx_line(cx, cy, cx + (i32)LX[mi] * (R - 16) / 100,
                      cy + (i32)LY[mi] * (R - 16) / 100, PAL_ACCENT);
    gfx_line(cx, cy, cx + (i32)LX[si] * (R -  8) / 100,
                      cy + (i32)LY[si] * (R -  8) / 100, COL_ERR);
    gfx_circle(cx, cy, 5, PAL_TEXT);

    char buf[16] = "00:00:00";
    char tmp[8];
    k_itoa(h, tmp, 10); if (h < 10) { buf[0]='0'; buf[1]=tmp[0]; } else { buf[0]=tmp[0]; buf[1]=tmp[1]; }
    k_itoa(m, tmp, 10); if (m < 10) { buf[3]='0'; buf[4]=tmp[0]; } else { buf[3]=tmp[0]; buf[4]=tmp[1]; }
    k_itoa(s, tmp, 10); if (s < 10) { buf[6]='0'; buf[7]=tmp[0]; } else { buf[6]=tmp[0]; buf[7]=tmp[1]; }
    buf[8]=0;
    gfx_text_centered(cx, cy + R + 24, buf, PAL_TEXT);
}

/* --- Stats --------------------------------------------------------------- */
static void render_stats(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame; (void)wh;
    section(wx, wy, T("Stats", "İstatistik"),
            T("live telemetry", "Canlı sistem telemetrisi"));

    u64 t = rdtsc();
    u32 hi = (u32)(t >> 32), lo = (u32)t;
    u32 h, m, s; pit_uptime(&h, &m, &s);

    char hex[22], mib[28], line[96];

    k_strcpy(line, "tsc       0x");
    k_itoa(hi, hex, 16); k_strcat(line, hex);
    k_itoa(lo, hex, 16); k_strcat(line, hex);
    gfx_text(wx + 24, wy + 60, line, PAL_TEXT);

    char th[8], tm[8], ts[8];
    k_itoa(h, th, 10); k_itoa(m, tm, 10); k_itoa(s, ts, 10);
    k_strcpy(line, T("uptime    ", "çalışma   "));
    k_strcat(line, th); k_strcat(line, "h ");
    k_strcat(line, tm); k_strcat(line, "m ");
    k_strcat(line, ts); k_strcat(line, "s");
    gfx_text(wx + 24, wy + 86, line, PAL_TEXT);

    k_strcpy(line, T("ticks     ", "tik       "));
    k_itoa(g_ticks, hex, 10); k_strcat(line, hex);
    gfx_text(wx + 24, wy + 110, line, PAL_TEXT);

    u64 mib_val = RAM_TOTAL_BYTES / ((u64)1024 * 1024);
    k_u64_to_dec(mib_val, mib);
    k_strcpy(line, T("ram (mmap)", "RAM (mmap"));
    k_strcat(line, ") ");
    k_strcat(line, mib);
    k_strcat(line, T(" MiB", " MiB"));
    gfx_text(wx + 24, wy + 134, line, PAL_TEXT);

    k_strcpy(line, T("screen    ", "ekran    "));
    k_itoa(FB.width, hex, 10); k_strcat(line, hex); k_strcat(line, "x");
    k_itoa(FB.height, hex, 10); k_strcat(line, hex); k_strcat(line, "@");
    k_itoa(FB.bpp, hex, 10); k_strcat(line, hex);
    gfx_text(wx + 24, wy + 158, line, PAL_TEXT);

    /* pulse bar */
    i32 pw = (i32)((g_ticks % 100) * (ww - 48) / 100);
    gfx_round_rect(wx + 24, wy + 210, ww - 48, 6, 3, PAL_HAIRLINE);
    gfx_round_rect(wx + 24, wy + 210, pw,      6, 3, PAL_ACCENT);
}

/* --- About --------------------------------------------------------------- */
static void render_about(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame; (void)wh;
    section(wx, wy, T("About FalconOS", "FalconOS Hakkında"),
            T("Version & credits", "Sürüm ve katkılar"));

    i32 cx = wx + ww / 2;
    gfx_circle(cx, wy + 110, 56, PAL_ACCENT);
    gfx_circle(cx, wy + 110, 36, PAL_PANEL);
    gfx_circle(cx, wy + 110, 18, PAL_ACCENT);

    gfx_text_centered(cx, wy + 190, "FalconOS 1",                            PAL_TEXT);
    gfx_text_centered(cx, wy + 210,
#if ARCH_x86_64
        "bare-metal x86_64 — personal + developer",
#else
        "bare-metal i386 — personal + developer",
#endif
                      PAL_ACCENT);
    gfx_text_centered(cx, wy + 232,
        T("two kernels, one binary - F1 to flip - F2 Launchpad",
          "iki çekirdek, tek ikili - F1 ile geç - F2 Launchpad"),
        PAL_TEXT_DIM);

    /* Native subsystems strip */
    char buf[80];
    k_strcpy(buf, T("Storage: ", "Depolama: "));
    k_strcat(buf, linux_compat_summary());
    gfx_text_centered(cx, wy + 256, buf, PAL_TEXT_DIM);

    char hidsum[80];
    hid_keymap_dump(hidsum, sizeof hidsum);
    gfx_text_centered(cx, wy + 274, hidsum, PAL_TEXT_FAINT);

    gfx_text_centered(cx, wy + 296,
        T("bare-metal microkernel  -  prg packages  -  Store  -  POSIX shell",
          "bare-metal mikroçekirdek  -  prg paketleri  -  Mağaza  -  POSIX kabuk"),
        PAL_TEXT_FAINT);
}

/* --- Sistem Güncellemeleri (offline channel UI) ---------------------------- */
static void render_updates(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame;
    section(wx, wy,
            T("System updates", "Sistem güncellemeleri"),
            T("prg catalogue + Terminal bridge", "prg katalogu + Terminal köprüsü"));

    char buf[192], num[14];
    k_strcpy(buf, T("Installed packages: ", "Kurulu paket sayısı: "));
    k_itoa((u32)prg_installed_count(), num, 10); k_strcat(buf, num);
    k_strcat(buf, T(" / ", " / "));
    k_itoa((u32)prg_count(), num, 10); k_strcat(buf, num);
    gfx_text(wx + 24, wy + 60, buf, PAL_TEXT);

    k_strcpy(buf, T("FalconFS: ", "FalconFS: "));
    k_strcat(buf, diskdb_present()
                    ? T("superblock on ATA.", "ATA üzerinde süper blok.")
                    : T("no superblock (güvenli oturum or no disk).",
                        "süper blok yok (güvenli oturum veya disk yok)."));
    gfx_text(wx + 24, wy + 88, buf, PAL_TEXT_DIM);

    gfx_text(wx + 24, wy + 124,
             T("Online updates need virtio-net + TLS (planned).",
               "Çevrimiçi güncelleme virtio-net + TLS gerektirir (planlanıyor)."),
             PAL_TEXT_DIM);
    gfx_text(wx + 24, wy + 152,
             T("Terminal:  update check  |  update apply",
               "Terminal:  update check  |  update apply"),
             PAL_TEXT_FAINT);

    (void)ww; (void)wh;
}

static void updates_input_key(i32 key) { (void)key; }

/* --- Store (prg package browser, GUI) ----------------------------------- */
static i32 store_cursor = 0;
static i32 store_filter = 0;     /* 0 all, 1 installed */

static bool store_pkg_visible(i32 pkg_i)
{
    if (store_filter == 0) return true;
    return prg_is_installed(pkg_i);
}

static i32 store_visible_count(void)
{
    i32 n = 0;
    for (i32 i = 0; i < prg_count(); i++) if (store_pkg_visible(i)) n++;
    return n;
}

static i32 store_visible_to_pkg(i32 vis_i)
{
    if (vis_i < 0) return -1;
    i32 seen = 0;
    for (i32 i = 0; i < prg_count(); i++) {
        if (!store_pkg_visible(i)) continue;
        if (seen == vis_i) return i;
        seen++;
    }
    return -1;
}

static i32 store_pkg_to_visible(i32 pkg_i)
{
    if (pkg_i < 0) return -1;
    i32 vis = 0;
    for (i32 i = 0; i < prg_count(); i++) {
        if (!store_pkg_visible(i)) continue;
        if (i == pkg_i) return vis;
        vis++;
    }
    return -1;
}

static void store_clamp_cursor(void)
{
    i32 n = store_visible_count();
    if (n <= 0) { store_cursor = 0; return; }
    if (store_cursor < 0) store_cursor = 0;
    if (store_cursor >= n) store_cursor = n - 1;
}

static void store_set_filter(i32 new_filter)
{
    if (new_filter < 0 || new_filter > 1) return;
    i32 keep_pkg = store_visible_to_pkg(store_cursor);
    store_filter = new_filter;
    if (keep_pkg >= 0) {
        i32 v = store_pkg_to_visible(keep_pkg);
        store_cursor = (v >= 0) ? v : 0;
    }
    store_clamp_cursor();
}

static void store_input_key(i32 key)
{
    store_clamp_cursor();
    if (key == KEY_LEFT)  { store_set_filter(0); return; }
    if (key == KEY_RIGHT) { store_set_filter(1); return; }

    i32 n = store_visible_count();
    if (n <= 0) return;

    if (key == KEY_UP   && store_cursor > 0) store_cursor--;
    if (key == KEY_DOWN && store_cursor < n - 1) store_cursor++;

    i32 pkg_i = store_visible_to_pkg(store_cursor);
    if (pkg_i < 0) return;
    if (key == KEY_ENTER || key == ' ' || key == 'i' || key == 'I') {
        prg_install(pkg_i);
    }
    if (key == 'r' || key == 'R' || key == KEY_BACKSPACE) {
        prg_remove(pkg_i);
    }
}

static void render_store(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame;
    section(wx, wy,
            T("Store",  "Mağaza"),
            T("up/down pick  Enter install  R remove  mouse: click row/action",
              "yukarı/aşağı seç  Enter yükle  R kaldır  fare: satır/eylem tıkla"));

    store_clamp_cursor();

    char hdr[80], num[12];
    k_strcpy(hdr, T("packages: ", "paket: "));
    k_itoa((u32)prg_installed_count(), num, 10);
    k_strcat(hdr, num);
    k_strcat(hdr, "/");
    k_itoa((u32)prg_count(), num, 10);
    k_strcat(hdr, num);
    k_strcat(hdr, T("  installed", "  kurulu"));
    gfx_text(wx + 24, wy + 38, hdr, PAL_TEXT_DIM);

    /* filter chips: all / installed */
    const char *chip_all  = T("all", "tümü");
    const char *chip_inst = T("installed", "kurulu");
    i32 chip_y  = wy + 54;
    i32 all_w   = gfx_text_width(chip_all) + 20;
    i32 inst_w  = gfx_text_width(chip_inst) + 20;
    i32 all_x   = wx + 24;
    i32 inst_x  = all_x + all_w + 10;
    bool all_on = (store_filter == 0);

    gfx_round_rect_a(all_x, chip_y, all_w, 20, 10, all_on ? PAL_ACCENT_DIM : PAL_PANEL_DEEP, 255);
    gfx_round_outline(all_x, chip_y, all_w, 20, 10, all_on ? PAL_ACCENT : PAL_HAIRLINE);
    gfx_text(all_x + 10, chip_y + 5, chip_all, all_on ? PAL_ACCENT : PAL_TEXT_DIM);
    gfx_round_rect_a(inst_x, chip_y, inst_w, 20, 10, all_on ? PAL_PANEL_DEEP : PAL_ACCENT_DIM, 255);
    gfx_round_outline(inst_x, chip_y, inst_w, 20, 10, all_on ? PAL_HAIRLINE : PAL_ACCENT);
    gfx_text(inst_x + 10, chip_y + 5, chip_inst, all_on ? PAL_TEXT_DIM : PAL_ACCENT);

    i32 lx = wx + 24, ly = wy + 82, lw = ww - 48;
    i32 row_h = 32;
    i32 visible_total = store_visible_count();
    i32 visible = (wh - 112) / row_h;
    if (visible < 1) visible = 1;
    i32 first = store_cursor - visible / 2;
    if (first < 0) first = 0;
    if (first > visible_total - visible) first = visible_total - visible;
    if (first < 0) first = 0;

    i32 mx, my; bool ml;
    mouse_get(&mx, &my, &ml);
    (void)ml;
    bool edge = wm_click_enabled();
    bool click_used = false;

    if (edge && mx >= all_x && mx <= all_x + all_w && my >= chip_y && my <= chip_y + 20) {
        store_set_filter(0); click_used = true;
    } else if (edge && mx >= inst_x && mx <= inst_x + inst_w &&
               my >= chip_y && my <= chip_y + 20) {
        store_set_filter(1); click_used = true;
    }

    if (visible_total <= 0) {
        gfx_round_rect_a(lx, ly, lw, 28, 8, PAL_PANEL_DEEP, 255);
        gfx_round_outline(lx, ly, lw, 28, 8, PAL_HAIRLINE);
        gfx_text(lx + 10, ly + 8,
                 T("no packages in this filter", "bu filtrede paket yok"),
                 PAL_TEXT_DIM);
        if (click_used) (void)mouse_consume_click();
        return;
    }

    for (i32 v = first; v < first + visible && v < visible_total; v++) {
        i32 i = store_visible_to_pkg(v);
        const prg_pkg_t *p = prg_at(i);
        i32 y = ly + (v - first) * row_h;
        bool active = (v == store_cursor);
        gfx_round_rect_a(lx, y, lw, row_h - 4, 8,
                         active ? PAL_ACCENT_DIM : PAL_PANEL_DEEP, 255);
        gfx_round_outline(lx, y, lw, row_h - 4, 8,
                          active ? PAL_ACCENT : PAL_HAIRLINE);
        /* category dot */
        u32 cat_color = COL_OK;
        if (p->category[0] == 'd') cat_color = COL_WARN;          /* drivers */
        else if (p->category[0] == 't') cat_color = 0xE85D9C;     /* themes  */
        else if (p->category[0] == 'l') cat_color = 0x16B5A8;     /* libs    */
        else if (p->category[0] == 'g') cat_color = COL_PURPLE;   /* games   */
        else if (p->category[0] == 'c') cat_color = 0x3070FF;     /* compat  */
        gfx_circle(lx + 14, y + 14, 5, cat_color);
        /* name + version */
        gfx_text(lx + 30, y + 8, p->name, PAL_TEXT);
        gfx_text(lx + 30 + gfx_text_width(p->name) + 8,
                 y + 8, p->version, PAL_TEXT_FAINT);
        /* summary */
        gfx_text(lx + 30 + gfx_text_width(p->name) + 8 + gfx_text_width(p->version) + 14,
                 y + 8, p->summary, PAL_TEXT_DIM);

        /* status/action pill */
        bool installed = prg_is_installed(i);
        const char *badge =
            p->builtin             ? T("built-in",  "yerlesik") :
            installed              ? T("remove",    "kaldir")   :
                                     T("get",       "yükle");
        i32 bw = gfx_text_width(badge) + 14;
        i32 bx = lx + lw - bw - 10;
        u32 bc = p->builtin ? PAL_PANEL_HI : (installed ? 0xEBC9C8 : PAL_ACCENT);
        u32 tc = p->builtin ? PAL_TEXT_DIM : 0xFFFFFF;
        gfx_round_rect_a(bx, y + 4, bw, row_h - 12, 8, bc, 255);
        gfx_round_outline(bx, y + 4, bw, row_h - 12, 8, PAL_HAIRLINE);
        gfx_text(bx + 7, y + 8, badge, tc);

        if (edge && !click_used &&
            mx >= lx && mx <= lx + lw && my >= y && my <= y + row_h - 4) {
            store_cursor = v;
            if (mx >= bx && mx <= bx + bw && my >= y + 4 && my <= y + row_h - 8 && !p->builtin) {
                if (installed) (void)prg_remove(i);
                else           (void)prg_install(i);
            }
            click_used = true;
        }
    }

    /* footer status */
    const prg_pkg_t *cur = prg_at(store_visible_to_pkg(store_cursor));
    char foot[80];
    k_strcpy(foot, T("category: ", "kategori: "));
    k_strcat(foot, cur->category);
    gfx_text(wx + 24, wy + wh - 22, foot, PAL_TEXT_FAINT);

    if (click_used) (void)mouse_consume_click();
}

/* --- Terminal (POSIX shell subset, FalconOS 1.1) -------------------------
 * A compact, real shell with a 60+ command vocabulary that mirrors the
 * GNU coreutils + util-linux command surface most users reach for first.
 * All commands operate against a 16-slot RAM-backed flat filesystem
 * ("shfs") so behaviour is real (mkdir/rm/cp/mv/tee actually mutate
 * state, head/tail/wc/sort/uniq/grep/tr/cut produce the expected output)
 * within the BSS budget.
 *
 *   file ops      pwd cd ls cat head tail wc sort uniq grep tr cut tee
 *                 find rm touch cp mv mkdir rmdir basename dirname
 *                 more less xxd hexdump file
 *   text/string   echo printf yes seq expr test [ true false
 *   shell         env set unset export alias history clear help exit
 *                 if/then/fi for/in/do/done | > >>
 *   process       ps top jobs kill (best-effort, single-task kernel)
 *   user / sys    uname whoami id groups who w hostname uptime date cal
 *   storage       df du free mount lsblk
 *   power         reboot shutdown poweroff
 *
 * Commands return shell exit status (0 = success).  Unknown commands
 * land in the "command not found" branch identical to bash.            */
#define TERM_LINES 12
#define TERM_COLS  80
#define SH_VARS    16
#define SH_FBYTES  SHFS_FBYTES

typedef struct { char name[16]; char value[64]; bool used; } shvar_t;

static char term_buf[TERM_LINES][TERM_COLS];
static i32  term_init_done = 0;
static i32  term_input_len = 0;
static char term_input[TERM_COLS];
static shvar_t  sh_vars [SH_VARS];

static void term_push(const char *s)
{
    for (i32 i = 1; i < TERM_LINES; i++) k_strcpy(term_buf[i - 1], term_buf[i]);
    k_strcpy(term_buf[TERM_LINES - 1], "");
    /* truncate-copy */
    char *d = term_buf[TERM_LINES - 1]; i32 n = 0;
    while (s[n] && n < TERM_COLS - 1) { d[n] = s[n]; n++; }
    d[n] = 0;
}

static void term_init(void)
{
    if (term_init_done) return;
    term_init_done = 1;
    for (i32 i = 0; i < TERM_LINES; i++) term_buf[i][0] = 0;
    term_push(T("FalconOS shell — cd/ls/cat/echo/if/for/|/ >",
                "FalconOS kabuğu — cd/ls/cat/echo/if/for/| / >"));
    term_push(T("Type 'help', 'hwinfo', 'ai', or Mağaza / güncelleme.",
                "'help', 'hwinfo', 'ai' yazın; Mağaza / güncelleme."));
    term_push("");
    shfs_init();
}

static shvar_t *sh_var_find(const char *n)
{
    for (i32 i = 0; i < SH_VARS; i++)
        if (sh_vars[i].used && k_strcmp(sh_vars[i].name, n) == 0)
            return &sh_vars[i];
    return 0;
}
static void sh_var_set(const char *n, const char *v)
{
    shvar_t *s = sh_var_find(n);
    if (!s) {
        for (i32 i = 0; i < SH_VARS; i++) if (!sh_vars[i].used) { s = &sh_vars[i]; break; }
    }
    if (!s) return;
    s->used = true;
    k_strcpy(s->name, n);
    /* truncate to 63 */
    i32 i = 0;
    while (v[i] && i < 63) { s->value[i] = v[i]; i++; }
    s->value[i] = 0;
}
/* --- prg install receipts under /home/falcon/.prg/ ---------------------- */
static void pkg_receipt_abs(i32 idx, char *buf, i32 cap)
{
    k_strcpy(buf, "/home/falcon/.prg/r");
    char num[12];
    k_itoa((u32)idx, num, 10);
    k_strcat(buf, num);
    (void)cap;
}

void apps_pkg_on_install(i32 idx)
{
    const prg_pkg_t *p = prg_at(idx);
    if (!p || p->builtin) return;
    char abs[SHFS_PATH];
    pkg_receipt_abs(idx, abs, (i32)sizeof abs);
    shfs_ent_t *f = shfs_open_w_abs(abs, false);
    if (!f) return;
    k_strcpy(f->data, "package ");
    k_strcat(f->data, p->name);
    k_strcat(f->data, "\nver ");
    k_strcat(f->data, p->version);
    k_strcat(f->data, "\n");
    k_strcat(f->data, p->summary);
    f->len = k_strlen(f->data);
    if (f->len >= SH_FBYTES) {
        f->data[SH_FBYTES - 1] = 0;
        f->len = SH_FBYTES - 1;
    }
}

void apps_pkg_on_remove(i32 idx)
{
    char abs[SHFS_PATH];
    pkg_receipt_abs(idx, abs, (i32)sizeof abs);
    (void)shfs_rm_abs(abs);
}

void apps_pkg_sync_receipts_from_state(void)
{
    for (i32 i = 0; i < prg_count(); i++) {
        const prg_pkg_t *p = prg_at(i);
        if (!p || p->builtin) continue;
        if (prg_is_installed(i)) apps_pkg_on_install(i);
        else                     apps_pkg_on_remove(i);
    }
}

/* small helpers --------------------------------------------------------- */
static bool sh_isspace(char c) { return c == ' ' || c == '\t'; }
static char sh_tolower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c; }

static bool sh_streq_ci(const char *a, const char *b)
{
    i32 i = 0;
    while (a[i] && b[i]) {
        if (sh_tolower(a[i]) != sh_tolower(b[i])) return false;
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

static bool sh_contains_ci(const char *hay, const char *needle)
{
    if (!needle || !needle[0]) return true;
    for (i32 i = 0; hay[i]; i++) {
        i32 j = 0;
        while (needle[j] && hay[i + j] &&
               sh_tolower(hay[i + j]) == sh_tolower(needle[j])) j++;
        if (!needle[j]) return true;
    }
    return false;
}

static i32 sh_find_pkg_idx(const char *name)
{
    for (i32 i = 0; i < prg_count(); i++) {
        const prg_pkg_t *p = prg_at(i);
        if (p && sh_streq_ci(p->name, name)) return i;
    }
    return -1;
}

static i32 sh_find_app_idx(const char *name)
{
    if (!name || !name[0]) return -1;
    for (i32 i = 0; i < apps_count(); i++)
        if (sh_streq_ci(apps_name(i), name)) return i;
    if (sh_streq_ci(name, "updates") || sh_streq_ci(name, "güncelleme") ||
        sh_streq_ci(name, "guncelleme") ||
        sh_streq_ci(name, "sistem güncellemeleri") ||
        sh_streq_ci(name, "sistem-güncellemeleri"))
        return sh_find_app_idx("Sistem Güncellemeleri");
    if (sh_streq_ci(name, "google-chrome") || sh_streq_ci(name, "googlechrome"))
        return sh_find_app_idx("Browser");
    if (sh_streq_ci(name, "heroic-launcher") || sh_streq_ci(name, "heroiclauncher"))
        return sh_find_app_idx("Heroic");
    if (sh_streq_ci(name, "media") || sh_streq_ci(name, "video-player"))
        return sh_find_app_idx("Video");
    if (sh_streq_ci(name, "falco") || sh_streq_ci(name, "falco-browser") ||
        sh_streq_ci(name, "browser"))
        return sh_find_app_idx("Falco");

    static const struct { const char *alias; i32 idx; } TR_ALIAS[] = {
        { "ana sayfa", 0 },       { "anasayfa", 0 },
        { "dosyalar", 1 },      { "dosya", 1 },
        { "mağaza", 2 },        { "magaza", 2 },
        { "ayarlar", 3 },
        { "terminal", 5 },
        { "hesap makinesi", 6 }, { "hesap", 6 },
        { "notlar", 7 },        { "not defteri", 7 },
        { "saat", 8 },
        { "istatistik", 9 },
        { "takvim", 10 },
        { "galeri", 11 },
        { "video", 12 },
        { "falco tarayıcı", 13 }, { "falco tarayici", 13 },
        { "asistan", 16 },       { "yardımcı", 16 },
        { "hakkında", 17 },      { "hakkinda", 17 },
    };
    for (u32 kk = 0; kk < sizeof TR_ALIAS / sizeof TR_ALIAS[0]; kk++) {
        if (sh_streq_ci(name, TR_ALIAS[kk].alias))
            return TR_ALIAS[kk].idx;
    }
    return -1;
}

static void sh_buf_pop_utf8(char *buf, i32 *len)
{
    if (!buf || !len || *len <= 0) return;
    i32 i = *len - 1;
    while (i > 0 && (((u8)buf[i] & 0xC0u) == 0x80u)) i--;
    buf[i] = 0;
    *len = i;
}

static bool sh_buf_append_key(char *buf, i32 *len, i32 cap, i32 key)
{
    char utf[4];
    i32 n = key_to_utf8(key, utf);
    if (n <= 0 || !buf || !len) return false;
    if (*len + n >= cap) return false;
    for (i32 i = 0; i < n; i++) buf[*len + i] = utf[i];
    *len += n;
    buf[*len] = 0;
    return true;
}

static i32 sh_utf8_chars_between(const char *buf, i32 from, i32 to)
{
    i32 n = 0;
    if (from < 0) from = 0;
    if (to < from) return 0;
    for (i32 i = from; i < to && buf[i]; i++) {
        if (((u8)buf[i] & 0xC0u) != 0x80u) n++;
    }
    return n;
}

/* Tokenise a single command (no metachars). $X expanded inline. Tokens
 * placed in `out[]`, returns token count. */
static i32 sh_tokenise(const char *cmd, char out[][64], i32 max)
{
    i32 n = 0, i = 0;
    while (cmd[i] && n < max) {
        while (cmd[i] && sh_isspace(cmd[i])) i++;
        if (!cmd[i]) break;
        i32 j = 0;
        while (cmd[i] && !sh_isspace(cmd[i]) && j < 63) {
            if (cmd[i] == '$') {
                /* expand variable */
                i++;
                char vn[16]; i32 k = 0;
                while (cmd[i] && ((cmd[i] >= 'A' && cmd[i] <= 'Z') ||
                                  (cmd[i] >= 'a' && cmd[i] <= 'z') ||
                                  (cmd[i] >= '0' && cmd[i] <= '9') ||
                                   cmd[i] == '_') && k < 15) {
                    vn[k++] = cmd[i++];
                }
                vn[k] = 0;
                shvar_t *v = sh_var_find(vn);
                if (v) {
                    i32 t = 0;
                    while (v->value[t] && j < 63) out[n][j++] = v->value[t++];
                }
            } else {
                out[n][j++] = cmd[i++];
            }
        }
        out[n][j] = 0;
        n++;
    }
    return n;
}

/* Run a single command (already tokenised). Output goes to `out` (cap
 * `cap`). Returns exit status (0 == success). */
static i32 sh_run_argv(i32 argc, char argv[][64], char *out, i32 cap)
{
    if (argc == 0) return 0;
    out[0] = 0;
    const char *cmd = argv[0];

    /* assignment X=val (only when first token has '=' and no command) */
    {
        i32 eq = -1;
        for (i32 i = 0; cmd[i]; i++) if (cmd[i] == '=') { eq = i; break; }
        if (eq > 0 && argc == 1) {
            char name[16], val[64]; i32 k = 0;
            for (i32 i = 0; i < eq && k < 15; i++) name[k++] = cmd[i];
            name[k] = 0;
            k = 0;
            for (i32 i = eq + 1; cmd[i] && k < 63; i++) val[k++] = cmd[i];
            val[k] = 0;
            sh_var_set(name, val);
            return 0;
        }
    }

    if (k_strcmp(cmd, "true")  == 0) return 0;
    if (k_strcmp(cmd, "false") == 0) return 1;
    if (k_strcmp(cmd, "exit")  == 0) { apps_close(); return 0; }
    if (k_strcmp(cmd, "clear") == 0) {
        for (i32 i = 0; i < TERM_LINES; i++) term_buf[i][0] = 0;
        return 0;
    }
    if (k_strcmp(cmd, "help") == 0) {
        k_strcpy(out,
            "pwd cd ls cat head tail wc sort uniq grep tr cut tee find rm "
            "touch cp mv mkdir rmdir basename dirname more less xxd file "
            "echo printf yes seq expr test [ env set unset alias export "
            "history ps top kill df du free mount lsblk uname hwinfo lscpu ver version whoami id vm dns ping http https xfile hwcheck jfs "
            "groups who w users hostname uptime cal date reboot shutdown "
            "which type prg pkg open chrome falco heroic video search "
            "update man | > >>");
        return 0;
    }
    if (k_strcmp(cmd, "man") == 0) {
        if (argc < 2) { k_strcpy(out, "man: usage: man <command>"); return 1; }
        if (k_strcmp(argv[1], "prg") == 0 || k_strcmp(argv[1], "pkg") == 0) {
            k_strcpy(out, "prg: list | installed | search <term> | info <pkg> | install <pkg> | remove <pkg>");
            return 0;
        }
        if (k_strcmp(argv[1], "open") == 0) {
            k_strcpy(out, "open <app-name>  (e.g. open Falco, open Chrome, open Heroic, open Video)");
            return 0;
        }
        if (k_strcmp(argv[1], "falco") == 0) {
            k_strcpy(out, "falco [query]: opens Falco browser; with query, runs in-app search.");
            return 0;
        }
        if (k_strcmp(argv[1], "search") == 0) {
            k_strcpy(out, "search <query>: open Falco and run indexed search.");
            return 0;
        }
        if (k_strcmp(argv[1], "update") == 0) {
            k_strcpy(out, "update check | update apply: upgrade FalconOS components without reinstall.");
            return 0;
        }
        if (k_strcmp(argv[1], "video") == 0) {
            k_strcpy(out, "video: launches Video app. Keys: Space play/pause, <-/-> seek, Tab next clip.");
            return 0;
        }
        if (k_strcmp(argv[1], "hwinfo") == 0) {
            k_strcpy(out, "hwinfo: CPU vendor/brand via CPUID plus RAMmmap + framebuffer line.");
            return 0;
        }
        if (k_strcmp(argv[1], "ver") == 0 || k_strcmp(argv[1], "version") == 0) {
            k_strcpy(out, "ver / version — prints FalconOS build line (uname style).");
            return 0;
        }
        k_strcpy(out, argv[1]); k_strcat(out, ": manual entry not found");
        return 1;
    }
    if (k_strcmp(cmd, "open") == 0 || k_strcmp(cmd, "xdg-open") == 0) {
        if (argc < 2) { k_strcpy(out, "open: usage: open <app>"); return 1; }
        i32 ai = sh_find_app_idx(argv[1]);
        if (ai < 0 && argc >= 3) {
            char joined[64];
            k_strcpy(joined, argv[1]);
            for (i32 i = 2; i < argc && k_strlen(joined) < 62; i++) {
                k_strcat(joined, " ");
                k_strcat(joined, argv[i]);
            }
            ai = sh_find_app_idx(joined);
        }
        if (ai < 0) { k_strcpy(out, "open: app not found"); return 1; }
        apps_open(ai);
        k_strcpy(out, "opened "); k_strcat(out, apps_display_name(ai));
        return 0;
    }
    if (k_strcmp(cmd, "chrome") == 0) {
        /* Legacy alias: this is Falcon Browser, not Linux/Google Chrome. */
        i32 ai = sh_find_app_idx("Browser");
        if (ai >= 0) apps_open(ai);
        k_strcpy(out, "opening Falcon Browser (native HTTPS text view)");
        return 0;
    }
    if (k_strcmp(cmd, "heroic") == 0) {
        i32 pkg = sh_find_pkg_idx("app-heroic-launcher");
        if (pkg >= 0 && !prg_is_installed(pkg)) {
            k_strcpy(out, "heroic: package not installed (run: prg install app-heroic-launcher)");
            return 1;
        }
        i32 ai = sh_find_app_idx("Heroic");
        if (ai >= 0) apps_open(ai);
        k_strcpy(out, "opening Heroic Launcher");
        return 0;
    }
    if (k_strcmp(cmd, "video") == 0) {
        i32 ai = sh_find_app_idx("Video");
        if (ai >= 0) apps_open(ai);
        k_strcpy(out, "opening Video player");
        return 0;
    }
    if (k_strcmp(cmd, "falco") == 0) {
        i32 ai = sh_find_app_idx("Falco");
        if (argc >= 2) {
            char q[80];
            q[0] = 0;
            for (i32 i = 1; i < argc && k_strlen(q) < 78; i++) {
                if (i > 1) k_strcat(q, " ");
                k_strcat(q, argv[i]);
            }
            falco_set_query(q);
        }
        if (ai >= 0) apps_open(ai);
        k_strcpy(out, "opening Falco browser");
        return 0;
    }
    if (k_strcmp(cmd, "search") == 0) {
        if (argc < 2) { k_strcpy(out, "search: usage: search <query>"); return 1; }
        char q[80];
        q[0] = 0;
        for (i32 i = 1; i < argc && k_strlen(q) < 78; i++) {
            if (i > 1) k_strcat(q, " ");
            k_strcat(q, argv[i]);
        }
        falco_set_query(q);
        i32 ai = sh_find_app_idx("Falco");
        if (ai >= 0) apps_open(ai);
        k_strcpy(out, "search forwarded to Falco");
        return 0;
    }
    if (k_strcmp(cmd, "update") == 0 || k_strcmp(cmd, "upgrade") == 0) {
        static const char *BUNDLE[] = {
            "app-falco-browser",
            "app-google-chrome",
            "app-heroic-launcher",
            "theme-liquid",
            "fonts-mono-pack",
            NULL
        };
        bool apply = (argc >= 2 &&
                      (k_strcmp(argv[1], "apply") == 0 ||
                       k_strcmp(argv[1], "upgrade") == 0 ||
                       k_strcmp(argv[1], "install") == 0));
        if (!apply) {
            i32 pending = 0;
            for (i32 i = 0; BUNDLE[i]; i++) {
                i32 idx = sh_find_pkg_idx(BUNDLE[i]);
                if (idx >= 0 && !prg_is_installed(idx)) pending++;
            }
            char num[16];
            k_strcpy(out, "update check: ");
            k_itoa((u32)pending, num, 10); k_strcat(out, num);
            k_strcat(out, " package(s) pending. Run: update apply");
            return 0;
        }
        i32 done = 0;
        for (i32 i = 0; BUNDLE[i]; i++) {
            i32 idx = sh_find_pkg_idx(BUNDLE[i]);
            if (idx >= 0 && prg_install(idx)) done++;
        }
        char num[16];
        k_strcpy(out, "update apply: ");
        k_itoa((u32)done, num, 10); k_strcat(out, num);
        k_strcat(out, " package(s) ready. Reinstall not required.");
        return 0;
    }
    if (k_strcmp(cmd, "prg") == 0 || k_strcmp(cmd, "pkg") == 0) {
        if (argc < 2 || k_strcmp(argv[1], "help") == 0) {
            k_strcpy(out, "prg: list | installed | search <term> | info <pkg> | install <pkg> | remove <pkg>");
            return 0;
        }
        if (k_strcmp(argv[1], "list") == 0) {
            out[0] = 0;
            for (i32 i = 0; i < prg_count(); i++) {
                const prg_pkg_t *p = prg_at(i);
                if (!p) continue;
                if (k_strlen(out) > cap - 40) break;
                k_strcat(out, prg_is_installed(i) ? "[i] " : "[ ] ");
                k_strcat(out, p->name);
                k_strcat(out, "\n");
            }
            i32 n = k_strlen(out); if (n > 0) out[n - 1] = 0;
            return 0;
        }
        if (k_strcmp(argv[1], "installed") == 0) {
            out[0] = 0;
            for (i32 i = 0; i < prg_count(); i++) {
                if (!prg_is_installed(i)) continue;
                const prg_pkg_t *p = prg_at(i);
                if (!p) continue;
                if (k_strlen(out) > cap - 32) break;
                k_strcat(out, p->name); k_strcat(out, "\n");
            }
            i32 n = k_strlen(out); if (n > 0) out[n - 1] = 0;
            if (!out[0]) k_strcpy(out, "(none)");
            return 0;
        }
        if (k_strcmp(argv[1], "search") == 0) {
            if (argc < 3) { k_strcpy(out, "prg search: missing term"); return 1; }
            out[0] = 0;
            for (i32 i = 0; i < prg_count(); i++) {
                const prg_pkg_t *p = prg_at(i);
                if (!p) continue;
                if (!sh_contains_ci(p->name, argv[2]) && !sh_contains_ci(p->summary, argv[2])) continue;
                if (k_strlen(out) > cap - 48) break;
                k_strcat(out, p->name);
                k_strcat(out, " - ");
                k_strcat(out, p->summary);
                k_strcat(out, "\n");
            }
            i32 n = k_strlen(out); if (n > 0) out[n - 1] = 0;
            if (!out[0]) k_strcpy(out, "no matches");
            return 0;
        }
        if (k_strcmp(argv[1], "info") == 0) {
            if (argc < 3) { k_strcpy(out, "prg info: missing package"); return 1; }
            i32 idx = sh_find_pkg_idx(argv[2]);
            if (idx < 0) { k_strcpy(out, "prg info: package not found"); return 1; }
            const prg_pkg_t *p = prg_at(idx);
            k_strcpy(out, p->name); k_strcat(out, " "); k_strcat(out, p->version);
            k_strcat(out, "\n");
            k_strcat(out, p->summary);
            k_strcat(out, "\nstatus: ");
            k_strcat(out, p->builtin ? "built-in" : (prg_is_installed(idx) ? "installed" : "not installed"));
            if (p->depends && p->depends[0]) { k_strcat(out, "\ndepends: "); k_strcat(out, p->depends); }
            return 0;
        }
        if (k_strcmp(argv[1], "install") == 0 || k_strcmp(argv[1], "remove") == 0) {
            if (argc < 3) { k_strcpy(out, "prg: missing package"); return 1; }
            i32 idx = sh_find_pkg_idx(argv[2]);
            if (idx < 0) { k_strcpy(out, "prg: package not found"); return 1; }
            bool ok = (k_strcmp(argv[1], "install") == 0) ? prg_install(idx) : prg_remove(idx);
            if (ok) {
                k_strcpy(out, argv[1]); k_strcat(out, " ok: "); k_strcat(out, argv[2]);
                return 0;
            }
            k_strcpy(out, argv[1]); k_strcat(out, " failed: "); k_strcat(out, argv[2]);
            return 1;
        }
        k_strcpy(out, "prg: unknown subcommand");
        return 1;
    }

    /* Native QEMU network tools: responses originate from NIC RX packets. */
    if (k_strcmp(cmd,"dns")==0 || k_strcmp(cmd,"ping")==0) {
        if(argc<2) {
            k_strcpy(out,"usage: dns <hostname> | ping <IPv4-or-hostname>");
            return 1;
        }
        u8 address[4];
        if(!native_net_parse_ipv4(argv[1],address) &&
           !native_net_dns_query(argv[1],address)) {
            k_strcpy(out,"network: DNS resolution failed or timed out");
            return 1;
        }
        if(k_strcmp(cmd,"ping")==0) {
            bool success=native_net_ping(address);
            k_strcpy(out,success?"ping: ICMP reply received":"ping: timeout/no ICMP reply");
            return success?0:1;
        }
        out[0]=0;
        char number[16];
        for(i32 i=0;i<4;i++){
            k_itoa(address[i],number,10);
            k_strcat(out,number);
            if(i<3)k_strcat(out,".");
        }
        return 0;
    }
    if(k_strcmp(cmd,"https")==0 || k_strcmp(cmd,"http")==0) {
        if(argc<3 || cap<128 || cap>4096) {
            k_strcpy(out,"usage: https <hostname> <path> (TLS verified) | http <hostname> <path> (plaintext)");
            return 1;
        }
        bool secure=k_strcmp(cmd,"https")==0;
        bool ok=secure ? native_https_get(argv[1],argv[2],out,(u32)cap)
                       : native_http_get(argv[1],argv[2],out,(u32)cap);
        if(!ok) {
            k_strcpy(out, secure ?
                 "https: TLS handshake, certificate, DNS or response validation failed (never downgraded)" :
                 "http: failed, timed out or incomplete response");
            return 1;
        }
        return 0;
    }


    if(k_strcmp(cmd,"jfs")==0){
        if(!jfs_ready()){
            k_strcpy(out,"jfs: journal unavailable or write-protected (use QEMU 0xFA test disk)");
            return 1;
        }
        if(argc<2 || k_strcmp(argv[1],"stat")==0){
            char n[16];k_strcpy(out,"JFS2 journal | files=");
            k_itoa(jfs_file_count(),n,10);k_strcat(out,n);
            k_strcat(out,"/128 | remaining sectors=");
            k_itoa(jfs_free_sectors(),n,10);k_strcat(out,n);
            return 0;
        }
        if(k_strcmp(argv[1],"fsck")==0){
            u32 files=0,invalid=0;
            if(!jfs_fsck(&files,&invalid)){
                k_strcpy(out,"jfs: read-only recovery scan failed");return 1;
            }
            char n[16];k_strcpy(out,"JFS2 recovered ");
            k_itoa(files,n,10);k_strcat(out,n);
            k_strcat(out," files; incomplete transactions=");
            k_itoa(invalid,n,10);k_strcat(out,n);
            return invalid?1:0;
        }
        if(argc<3){
            k_strcpy(out,"jfs: stat | fsck | write <absolute-path> <text> | cat <absolute-path> | rm <absolute-path>");
            return 1;
        }
        if(k_strcmp(argv[1],"write")==0 && argc>=4){
            u32 len=(u32)k_strlen(argv[3]);
            bool ok=jfs_write(argv[2],(const u8*)argv[3],len);
            k_strcpy(out,ok?"jfs: committed to transactional journal":"jfs: rejected path, full journal or write failed");
            return ok?0:1;
        }
        if(k_strcmp(argv[1],"rm")==0){
            bool ok=jfs_remove(argv[2]);
            k_strcpy(out,ok?"jfs: removed with committed tombstone":"jfs: remove failed");
            return ok?0:1;
        }
        if(k_strcmp(argv[1],"cat")==0){
            static u8 bytes[131072u];
            i32 len=jfs_read(argv[2],bytes,sizeof bytes);
            if(len<0){k_strcpy(out,"jfs: file absent or corrupt");return 1;}
            if(len>=cap){k_strcpy(out,"jfs: larger than terminal buffer; use a file reader");return 1;}
            for(i32 j=0;j<len;j++)
                if(bytes[j]<0x20u&&bytes[j]!='\n'&&bytes[j]!='\t'){
                    k_strcpy(out,"jfs: binary file; cannot display as text");
                    return 1;
                }
            k_memcpy(out,bytes,(u32)len);out[len]=0;return 0;
        }
        k_strcpy(out,"jfs: unknown command");return 1;
    }
    if(k_strcmp(cmd,"xfile")==0) {
        if(!xfs_ready()){
            k_strcpy(out,"xfile: 32KiB volume unavailable (safe disk needed)");
            return 1;
        }
        if(argc==1 || k_strcmp(argv[1],"stat")==0){
            char digits[16];
            k_strcpy(out,"XFS1 32KiB objects: ");
            k_itoa(xfs_count(),digits,10);k_strcat(out,digits);
            k_strcat(out," / 32, per object 32768 bytes");
            return 0;
        }
        if(k_strcmp(argv[1],"fsck")==0){
            u32 files=0,bad=0;
            if(!xfs_fsck(&files,&bad)){
                k_strcpy(out,"xfile: read-only consistency scan failed");return 1;
            }
            char digits[16];
            k_strcpy(out,"XFS1 verified files: ");
            k_itoa(files,digits,10);k_strcat(out,digits);
            k_strcat(out,", corrupt bank copies: ");
            k_itoa(bad,digits,10);k_strcat(out,digits);
            return bad?1:0;
        }
        if(argc<3){k_strcpy(out,"xfile: stat | fsck | fill <name> | rm <name> | inspect <name>");return 1;}
        if(k_strcmp(argv[1],"rm")==0){
            bool success=xfs_remove(argv[2]);
            k_strcpy(out,success?"xfile: removed":"xfile: removal failed");
            return success?0:1;
        }
        static u8 object[32768];
        if(k_strcmp(argv[1],"fill")==0){
            for(u32 j=0;j<32768;j++)object[j]=(u8)((j*37u)&255u);
            bool ok=xfs_write(argv[2],object,32768u);
            k_strcpy(out,ok?"xfile: committed 32768 bytes to safe disk":"xfile: disk write/space failure");
            return ok?0:1;
        }
        if(k_strcmp(argv[1],"inspect")==0){
            i32 len=xfs_read(argv[2],object,sizeof object);
            if(len<0){k_strcpy(out,"xfile: no valid checksum-verified object");return 1;}
            char digits[16];
            k_strcpy(out,"xfile: valid object, bytes=");
            k_itoa((u32)len,digits,10);k_strcat(out,digits);
            return 0;
        }
        if(k_strcmp(argv[1],"elfcheck")==0){
            i32 len=xfs_read(argv[2],object,sizeof object);
            u64 entry=0;u32 count=0;
            bool valid=len>0 && elf64_inspect(object,(u32)len,&entry,&count);
            if(!valid){k_strcpy(out,"elfcheck: rejected invalid/unsafe ELF64");return 1;}
            k_strcpy(out,"elfcheck: valid W^X ELF64 (NOT executed), segments=");
            char num[16];k_itoa(count,num,10);k_strcat(out,num);return 0;
        }
        k_strcpy(out,"xfile: stat | fsck | fill | rm | inspect | elfcheck");
        return 1;
    }
    if(k_strcmp(cmd,"hwcheck")==0){
        char digits[16];
        k_strcpy(out,"PCI capability scan (NOT production drivers)\nNVMe: ");
        k_itoa(pci_extended_count(1),digits,10);k_strcat(out,digits);
        k_strcat(out,pci_extended_mmio(1)?" CAP readable":" CAP unavailable");
        k_strcat(out,"\nxHCI: ");
        k_itoa(pci_extended_count(2),digits,10);k_strcat(out,digits);
        k_strcat(out,pci_extended_mmio(2)?" CAP readable":" CAP unavailable");
        k_strcat(out,"\nGPU/VGA: ");
        k_itoa(pci_extended_count(3),digits,10);k_strcat(out,digits);
        return 0;
    }
    if (k_strcmp(cmd,"vm")==0) {
        if (argc<2 || k_strcmp(argv[1],"list")==0) {
            fvm_status(out,cap);return 0;
        }
        if(k_strcmp(argv[1],"start")==0 && argc>=3) {
            i32 slot=fvm_spawn_file(argv[2]);
            if(slot<0){k_strcpy(out,"vm: missing or invalid FVM/1 file");return 1;}
            k_strcpy(out,"vm: started slot ");
            char num[16];k_itoa((u32)slot,num,10);k_strcat(out,num);
            return 0;
        }
        if(k_strcmp(argv[1],"kill")==0 && argc>=3 &&
           argv[2][0]>='0' && argv[2][0]<='3' && !argv[2][1]) {
            bool ok=fvm_kill(argv[2][0]-'0');
            k_strcpy(out,ok?"vm: stopped":"vm: invalid slot");return ok?0:1;
        }
        k_strcpy(out,"vm: list | start <path.fvm> | kill <0..3>");return 1;
    }
    if (k_strcmp(cmd, "pwd") == 0)    { k_strcpy(out, shfs_cwd); return 0; }
    if (k_strcmp(cmd, "uname") == 0)  {
#if ARCH_x86_64
        k_strcpy(out, "FalconOS 1 x86_64 bare-metal");
#else
        k_strcpy(out, "FalconOS 1 i386 bare-metal");
#endif
        return 0;
    }
    if (k_strcmp(cmd, "hwinfo") == 0) {
        hw_probe_summary(out, cap);
        return 0;
    }
    if (k_strcmp(cmd, "lscpu") == 0) {
        hw_probe_summary(out, cap);
        return 0;
    }
    if (k_strcmp(cmd, "ver") == 0 || k_strcmp(cmd, "version") == 0) {
        k_strcpy(out, "FalconOS 1  kernel  ");
#if ARCH_x86_64
        k_strcat(out, "x86_64  (tek komut: make start)");
#else
        k_strcat(out, "i386");
#endif
        return 0;
    }
    if (k_strcmp(cmd, "whoami") == 0) {
        const falcon_user_t *u = users_at(SET.active_user);
        k_strcpy(out, u ? u->name : "falcon");
        return 0;
    }
    if (k_strcmp(cmd, "date") == 0) {
        rtc_time_t t; rtc_local(&t);
        char num[16];
        out[0] = 0;
        loc_format_date(out, &t);
        k_strcat(out, "  ");
        if (t.hour < 10) k_strcat(out, "0");
        k_itoa(t.hour, num, 10); k_strcat(out, num); k_strcat(out, ":");
        if (t.min  < 10) k_strcat(out, "0");
        k_itoa(t.min,  num, 10); k_strcat(out, num);
        return 0;
    }
    if (k_strcmp(cmd, "cd") == 0) {
        if (argc < 2) { k_strcpy(shfs_cwd, "/home/falcon"); return 0; }
        char tmp[SHFS_PATH];
        if (argv[1][0] == '/') {
            k_strcpy(tmp, argv[1]);
        } else if (k_strcmp(argv[1], "..") == 0) {
            if (!shfs_abs_from(shfs_cwd, "..", tmp, sizeof tmp)) return 1;
        } else {
            if (!shfs_abs_from(shfs_cwd, argv[1], tmp, sizeof tmp)) return 1;
        }
        shfs_ent_t *t = shfs_lookup(tmp);
        if (!t || !t->is_dir) {
            k_strcpy(out, "cd: not a directory");
            return 1;
        }
        k_strcpy(shfs_cwd, tmp);
        return 0;
    }
    if (k_strcmp(cmd, "ls") == 0) {
        shfs_format_ls(shfs_cwd, out, cap);
        return 0;
    }
    if (k_strcmp(cmd, "cat") == 0) {
        if (argc < 2) return 1;
        shfs_ent_t *f = shfs_lookup_rel(shfs_cwd, argv[1]);
        if (!f || f->is_dir) { k_strcpy(out, "cat: not found: "); k_strcat(out, argv[1]); return 1; }
        i32 k = 0;
        while (f->data[k] && k < cap - 1) { out[k] = f->data[k]; k++; }
        out[k] = 0;
        return 0;
    }
    if (k_strcmp(cmd, "echo") == 0) {
        out[0] = 0;
        for (i32 i = 1; i < argc; i++) {
            if (i > 1) k_strcat(out, " ");
            k_strcat(out, argv[i]);
        }
        return 0;
    }
    if (k_strcmp(cmd, "env") == 0 || k_strcmp(cmd, "set") == 0) {
        out[0] = 0;
        for (i32 i = 0; i < SH_VARS; i++) if (sh_vars[i].used) {
            k_strcat(out, sh_vars[i].name);
            k_strcat(out, "=");
            k_strcat(out, sh_vars[i].value);
            k_strcat(out, " ");
        }
        return 0;
    }
    if (k_strcmp(cmd, "unset") == 0) {
        if (argc < 2) return 1;
        shvar_t *v = sh_var_find(argv[1]);
        if (v) v->used = false;
        return 0;
    }
    if (k_strcmp(cmd, "rm") == 0) {
        if (argc < 2) return 1;
        char abs[SHFS_PATH];
        if (!shfs_abs_from(shfs_cwd, argv[1], abs, sizeof abs)) return 1;
        if (!shfs_rm_abs(abs)) { k_strcpy(out, "rm: failed"); return 1; }
        return 0;
    }
    if (k_strcmp(cmd, "touch") == 0) {
        if (argc < 2) return 1;
        char abs[SHFS_PATH];
        if (!shfs_abs_from(shfs_cwd, argv[1], abs, sizeof abs)) return 1;
        if (!shfs_touch_abs(abs)) return 1;
        return 0;
    }
    if (k_strcmp(cmd, "cp") == 0 || k_strcmp(cmd, "mv") == 0) {
        if (argc < 3) return 1;
        char abs_src[SHFS_PATH], abs_dst[SHFS_PATH];
        if (!shfs_abs_from(shfs_cwd, argv[1], abs_src, sizeof abs_src)) return 1;
        if (!shfs_abs_from(shfs_cwd, argv[2], abs_dst, sizeof abs_dst)) return 1;
        shfs_ent_t *src = shfs_lookup(abs_src);
        if (!src || src->is_dir) { k_strcpy(out, "no such file"); return 1; }
        shfs_ent_t *dst = shfs_open_w_abs(abs_dst, false);
        if (!dst) return 1;
        for (u32 i = 0; i < src->len && i < SH_FBYTES - 1; i++) dst->data[i] = src->data[i];
        dst->len = src->len;
        dst->data[dst->len] = 0;
        if (k_strcmp(cmd, "mv") == 0)
            (void)shfs_rm_abs(abs_src);
        return 0;
    }
    if (k_strcmp(cmd, "mkdir") == 0 || k_strcmp(cmd, "rmdir") == 0) {
        if (argc < 2) { k_strcpy(out, cmd); k_strcat(out, ": missing operand"); return 1; }
        char abs[SHFS_PATH];
        if (!shfs_abs_from(shfs_cwd, argv[1], abs, sizeof abs)) return 1;
        if (k_strcmp(cmd, "mkdir") == 0) {
            if (!shfs_mkdir_abs(abs)) { k_strcpy(out, "mkdir: failed"); return 1; }
            return 0;
        }
        shfs_ent_t *e = shfs_lookup(abs);
        if (!e || !e->is_dir) { k_strcpy(out, "rmdir: not a directory"); return 1; }
        if (!shfs_rm_abs(abs)) { k_strcpy(out, "rmdir: not empty or failed"); return 1; }
        return 0;
    }
    /* head / tail [-n N] file --------------------------------------- */
    if (k_strcmp(cmd, "head") == 0 || k_strcmp(cmd, "tail") == 0) {
        i32 n = 5; i32 farg = 1;
        if (argc >= 3 && argv[1][0] == '-' && argv[1][1] == 'n') {
            n = 0; const char *p = argv[2];
            while (*p >= '0' && *p <= '9') { n = n*10 + (*p - '0'); p++; }
            farg = 3;
        } else if (argc >= 4 && k_strcmp(argv[1], "-n") == 0) {
            n = 0; const char *p = argv[2];
            while (*p >= '0' && *p <= '9') { n = n*10 + (*p - '0'); p++; }
            farg = 3;
        }
        if (argc <= farg) return 1;
        shfs_ent_t *f = shfs_lookup_rel(shfs_cwd, argv[farg]);
        if (!f) { k_strcpy(out, cmd); k_strcat(out, ": no such file"); return 1; }
        /* count newlines */
        i32 total = 1;
        for (u32 i = 0; i < f->len; i++) if (f->data[i] == '\n') total++;
        i32 keep_from = 0, keep_to = total;
        if (k_strcmp(cmd, "head") == 0) keep_to = (n < total) ? n : total;
        else                            keep_from = (total - n > 0) ? total - n : 0;
        out[0] = 0; i32 k = 0; i32 line_idx = 0; i32 oc = 0;
        for (u32 i = 0; i < f->len && oc < cap - 1; i++) {
            if (line_idx >= keep_from && line_idx < keep_to) out[oc++] = f->data[i];
            if (f->data[i] == '\n') line_idx++;
            (void)k;
        }
        out[oc] = 0;
        return 0;
    }
    /* wc [-l|-w|-c] file -------------------------------------------- */
    if (k_strcmp(cmd, "wc") == 0) {
        char mode = 0; i32 farg = 1;
        if (argc >= 3 && argv[1][0] == '-') { mode = argv[1][1]; farg = 2; }
        if (argc <= farg) return 1;
        shfs_ent_t *f = shfs_lookup_rel(shfs_cwd, argv[farg]);
        if (!f) { k_strcpy(out, "wc: no such file"); return 1; }
        u32 lines = 0, words = 0, chars = f->len; bool inw = false;
        for (u32 i = 0; i < f->len; i++) {
            if (f->data[i] == '\n') lines++;
            bool sp = (f->data[i] == ' ' || f->data[i] == '\t' || f->data[i] == '\n');
            if (!sp && !inw) { words++; inw = true; }
            else if (sp)     { inw = false; }
        }
        if (f->len > 0 && f->data[f->len - 1] != '\n') lines++;
        char num[16];
        out[0] = 0;
        if (mode == 'l' || mode == 0) { k_itoa(lines, num, 10); k_strcat(out, num); k_strcat(out, " "); }
        if (mode == 'w' || mode == 0) { k_itoa(words, num, 10); k_strcat(out, num); k_strcat(out, " "); }
        if (mode == 'c' || mode == 0) { k_itoa(chars, num, 10); k_strcat(out, num); k_strcat(out, " "); }
        k_strcat(out, argv[farg]);
        return 0;
    }
    /* sort / uniq — operate on file content line-by-line ------------ */
    if (k_strcmp(cmd, "sort") == 0 || k_strcmp(cmd, "uniq") == 0) {
        if (argc < 2) return 1;
        shfs_ent_t *f = shfs_lookup_rel(shfs_cwd, argv[1]);
        if (!f) { k_strcpy(out, cmd); k_strcat(out, ": no such file"); return 1; }
        /* split into up to 32 lines (max 96 chars each)               */
        char lines[32][96]; i32 nl = 0;
        i32 li = 0; lines[0][0] = 0;
        for (u32 i = 0; i < f->len && nl < 32; i++) {
            if (f->data[i] == '\n') {
                lines[nl][li] = 0; nl++; li = 0;
                if (nl < 32) lines[nl][0] = 0;
            } else if (li < 95) {
                lines[nl][li++] = f->data[i];
            }
        }
        if (li > 0 && nl < 32) { lines[nl][li] = 0; nl++; }
        if (k_strcmp(cmd, "sort") == 0) {
            /* O(n^2) selection sort (n ≤ 32, fine)                   */
            for (i32 a = 0; a < nl - 1; a++) {
                i32 best = a;
                for (i32 b = a + 1; b < nl; b++)
                    if (k_strcmp(lines[b], lines[best]) < 0) best = b;
                if (best != a) {
                    char tmp[96]; k_strcpy(tmp, lines[a]);
                    k_strcpy(lines[a], lines[best]); k_strcpy(lines[best], tmp);
                }
            }
        } else { /* uniq — drop adjacent duplicates                  */
            i32 w = 0;
            for (i32 r = 0; r < nl; r++) {
                if (w == 0 || k_strcmp(lines[w - 1], lines[r]) != 0) {
                    if (w != r) k_strcpy(lines[w], lines[r]);
                    w++;
                }
            }
            nl = w;
        }
        out[0] = 0; i32 oc = 0;
        for (i32 a = 0; a < nl && oc < cap - 2; a++) {
            for (i32 b = 0; lines[a][b] && oc < cap - 2; b++) out[oc++] = lines[a][b];
            if (a < nl - 1) out[oc++] = '\n';
        }
        out[oc] = 0;
        return 0;
    }
    /* grep PATTERN file --------------------------------------------- */
    if (k_strcmp(cmd, "grep") == 0) {
        if (argc < 3) { k_strcpy(out, "grep: usage: grep PATTERN FILE"); return 1; }
        shfs_ent_t *f = shfs_lookup_rel(shfs_cwd, argv[2]);
        if (!f) { k_strcpy(out, "grep: no such file"); return 1; }
        const char *pat = argv[1]; i32 pl = k_strlen(pat);
        out[0] = 0; i32 oc = 0;
        i32 line_start = 0;
        for (u32 i = 0; i <= f->len; i++) {
            if (i == f->len || f->data[i] == '\n') {
                /* check pat in [line_start, i) */
                bool hit = false;
                for (i32 a = line_start; a + pl <= (i32)i && !hit; a++) {
                    bool ok = true;
                    for (i32 b = 0; b < pl && ok; b++)
                        if (f->data[a + b] != pat[b]) ok = false;
                    if (ok) hit = true;
                }
                if (hit) {
                    for (i32 a = line_start; a < (i32)i && oc < cap - 2; a++)
                        out[oc++] = f->data[a];
                    if (oc < cap - 2) out[oc++] = '\n';
                }
                line_start = i + 1;
            }
        }
        if (oc > 0) out[oc - 1] = 0;
        else        out[0] = 0;
        return 0;
    }
    /* tr FROM TO — character-class translate (echo|tr 'a-z' 'A-Z')  */
    if (k_strcmp(cmd, "tr") == 0) {
        if (argc < 3) return 1;
        const char *from = argv[1], *to = argv[2];
        i32 fl = k_strlen(from);
        out[0] = 0; i32 oc = 0;
        /* tr operates on the file we were given, defaulting to a piped
         * stdin (we don't implement pipes here, so callers redirect).  */
        const char *src = (argc >= 4) ? argv[3] : "";
        shfs_ent_t *f = shfs_lookup_rel(shfs_cwd, src);
        const char *data = f ? f->data : src;
        u32 dlen = f ? f->len : (u32)k_strlen(src);
        for (u32 i = 0; i < dlen && oc < cap - 1; i++) {
            char c = data[i]; bool hit = false;
            for (i32 j = 0; j < fl; j++) {
                if (from[j] == c) {
                    out[oc++] = (j < k_strlen(to)) ? to[j] : c;
                    hit = true; break;
                }
            }
            if (!hit) out[oc++] = c;
        }
        out[oc] = 0;
        return 0;
    }
    /* cut -c N-M file ----------------------------------------------- */
    if (k_strcmp(cmd, "cut") == 0) {
        if (argc < 4) return 1;
        i32 a = 1, b = 1;
        const char *spec = argv[2];
        if (argv[1][0] == '-' && argv[1][1] == 'c') {
            a = 0; const char *p = spec;
            while (*p >= '0' && *p <= '9') { a = a * 10 + (*p - '0'); p++; }
            if (*p == '-') {
                p++; b = 0;
                while (*p >= '0' && *p <= '9') { b = b * 10 + (*p - '0'); p++; }
            } else b = a;
        }
        shfs_ent_t *f = shfs_lookup_rel(shfs_cwd, argv[3]);
        if (!f) return 1;
        out[0] = 0; i32 oc = 0; i32 col = 1;
        for (u32 i = 0; i < f->len && oc < cap - 1; i++) {
            if (f->data[i] == '\n') {
                out[oc++] = '\n'; col = 1; continue;
            }
            if (col >= a && col <= b) out[oc++] = f->data[i];
            col++;
        }
        out[oc] = 0;
        return 0;
    }
    /* tee file — overwrite a file with the joined remaining args ---- */
    if (k_strcmp(cmd, "tee") == 0) {
        if (argc < 2) return 1;
        shfs_ent_t *f = shfs_open_w_rel(shfs_cwd, argv[1], false);
        if (!f) return 1;
        out[0] = 0;
        for (i32 i = 2; i < argc; i++) {
            i32 ol = k_strlen(out);
            if (i > 2 && ol < cap - 1) k_strcat(out, " ");
            k_strcat(out, argv[i]);
        }
        i32 ol = k_strlen(out);
        for (i32 i = 0; i < ol && i < SH_FBYTES - 1; i++) f->data[i] = out[i];
        f->len = ol; f->data[f->len] = 0;
        return 0;
    }
    /* find — list all absolute shfs paths ---------------------------- */
    if (k_strcmp(cmd, "find") == 0) {
        shfs_paths_dump(out, cap);
        return 0;
    }
    /* basename / dirname ------------------------------------------- */
    if (k_strcmp(cmd, "basename") == 0) {
        if (argc < 2) return 1;
        const char *p = argv[1]; const char *last = p;
        for (; *p; p++) if (*p == '/') last = p + 1;
        k_strcpy(out, last);
        return 0;
    }
    if (k_strcmp(cmd, "dirname") == 0) {
        if (argc < 2) return 1;
        i32 last = -1;
        for (i32 i = 0; argv[1][i]; i++) if (argv[1][i] == '/') last = i;
        if (last < 0) { k_strcpy(out, "."); return 0; }
        for (i32 i = 0; i < last; i++) out[i] = argv[1][i];
        out[last] = 0; if (last == 0) k_strcpy(out, "/");
        return 0;
    }
    /* more / less — single-page cat ------------------------------- */
    if (k_strcmp(cmd, "more") == 0 || k_strcmp(cmd, "less") == 0) {
        if (argc < 2) return 1;
        shfs_ent_t *f = shfs_lookup_rel(shfs_cwd, argv[1]);
        if (!f) return 1;
        i32 k = 0;
        while (f->data[k] && k < cap - 1) { out[k] = f->data[k]; k++; }
        out[k] = 0;
        return 0;
    }
    /* xxd / hexdump first 64 bytes -------------------------------- */
    if (k_strcmp(cmd, "xxd") == 0 || k_strcmp(cmd, "hexdump") == 0) {
        if (argc < 2) return 1;
        shfs_ent_t *f = shfs_lookup_rel(shfs_cwd, argv[1]);
        if (!f) return 1;
        out[0] = 0; char num[8]; i32 oc = 0;
        for (u32 i = 0; i < f->len && i < 64 && oc < cap - 8; i++) {
            u8 b = (u8)f->data[i];
            const char *hex = "0123456789abcdef";
            num[0] = hex[(b >> 4) & 0xF]; num[1] = hex[b & 0xF]; num[2] = ' '; num[3] = 0;
            k_strcat(out, num); oc += 3;
        }
        return 0;
    }
    /* file — guess content type ------------------------------------ */
    if (k_strcmp(cmd, "file") == 0) {
        if (argc < 2) return 1;
        shfs_ent_t *f = shfs_lookup_rel(shfs_cwd, argv[1]);
        if (!f) { k_strcpy(out, "file: no such file"); return 1; }
        bool ascii = true;
        for (u32 i = 0; i < f->len; i++) {
            u8 c = (u8)f->data[i];
            if (c < 9 || (c > 13 && c < 32) || c == 127) { ascii = false; break; }
        }
        k_strcpy(out, argv[1]);
        k_strcat(out, ascii ? ": ASCII text" : ": data");
        return 0;
    }
    /* printf — %s only --------------------------------------------- */
    if (k_strcmp(cmd, "printf") == 0) {
        out[0] = 0;
        if (argc < 2) return 0;
        i32 ai = 2;
        for (i32 i = 0; argv[1][i]; i++) {
            if (argv[1][i] == '%' && argv[1][i + 1] == 's' && ai < argc) {
                k_strcat(out, argv[ai++]); i++;
            } else if (argv[1][i] == '\\' && argv[1][i + 1] == 'n') {
                k_strcat(out, "\n"); i++;
            } else {
                char one[2] = { argv[1][i], 0 }; k_strcat(out, one);
            }
        }
        return 0;
    }
    /* yes — bash echoes 'y' forever; we echo it once so the term
     * doesn't lock up                                              */
    if (k_strcmp(cmd, "yes") == 0) {
        k_strcpy(out, argc >= 2 ? argv[1] : "y");
        return 0;
    }
    /* seq M [N] ---------------------------------------------------- */
    if (k_strcmp(cmd, "seq") == 0) {
        if (argc < 2) return 1;
        i32 a = 1, b = 0;
        const char *p1 = argv[1];
        i32 v1 = 0; while (*p1 >= '0' && *p1 <= '9') { v1 = v1 * 10 + (*p1 - '0'); p1++; }
        if (argc >= 3) {
            a = v1;
            const char *p2 = argv[2]; b = 0;
            while (*p2 >= '0' && *p2 <= '9') { b = b * 10 + (*p2 - '0'); p2++; }
        } else { a = 1; b = v1; }
        out[0] = 0; char num[16];
        for (i32 i = a; i <= b && k_strlen(out) < cap - 8; i++) {
            k_itoa(i, num, 10); k_strcat(out, num);
            if (i < b) k_strcat(out, "\n");
        }
        return 0;
    }
    /* expr / test / [ ---------------------------------------------- */
    if (k_strcmp(cmd, "expr") == 0) {
        if (argc < 4) return 1;
        i32 a = 0, b = 0;
        const char *p = argv[1]; while (*p >= '0' && *p <= '9') { a = a * 10 + (*p - '0'); p++; }
        p = argv[3]; while (*p >= '0' && *p <= '9') { b = b * 10 + (*p - '0'); p++; }
        i32 r = 0;
        if (k_strcmp(argv[2], "+") == 0) r = a + b;
        else if (k_strcmp(argv[2], "-") == 0) r = a - b;
        else if (k_strcmp(argv[2], "*") == 0) r = a * b;
        else if (k_strcmp(argv[2], "/") == 0) r = (b == 0) ? 0 : a / b;
        else if (k_strcmp(argv[2], "%") == 0) r = (b == 0) ? 0 : a % b;
        else { k_strcpy(out, "expr: unsupported op"); return 2; }
        char num[16]; k_itoa(r, num, 10); k_strcpy(out, num);
        return 0;
    }
    if (k_strcmp(cmd, "test") == 0 || k_strcmp(cmd, "[") == 0) {
        /* [ -z STR ] / [ -n STR ] / [ A = B ] / [ A != B ] / [ A -lt B ] etc */
        i32 stripped = (k_strcmp(cmd, "[") == 0 &&
                        argc >= 2 && k_strcmp(argv[argc - 1], "]") == 0) ? 1 : 0;
        i32 nargs = argc - stripped;
        if (nargs == 3) {
            i32 a = 0, b = 0;
            for (i32 i = 0; argv[1][i]; i++) a = a * 10 + (argv[1][i] - '0');
            for (i32 i = 0; argv[3][i]; i++) b = b * 10 + (argv[3][i] - '0');
            if (k_strcmp(argv[2], "=")    == 0) return k_strcmp(argv[1], argv[3]) == 0 ? 0 : 1;
            if (k_strcmp(argv[2], "!=")   == 0) return k_strcmp(argv[1], argv[3]) != 0 ? 0 : 1;
            if (k_strcmp(argv[2], "-eq")  == 0) return a == b ? 0 : 1;
            if (k_strcmp(argv[2], "-ne")  == 0) return a != b ? 0 : 1;
            if (k_strcmp(argv[2], "-lt")  == 0) return a <  b ? 0 : 1;
            if (k_strcmp(argv[2], "-le")  == 0) return a <= b ? 0 : 1;
            if (k_strcmp(argv[2], "-gt")  == 0) return a >  b ? 0 : 1;
            if (k_strcmp(argv[2], "-ge")  == 0) return a >= b ? 0 : 1;
        } else if (nargs == 2) {
            if (k_strcmp(argv[1], "-z") == 0) return argv[2][0] == 0 ? 0 : 1;
            if (k_strcmp(argv[1], "-n") == 0) return argv[2][0] != 0 ? 0 : 1;
            if (k_strcmp(argv[1], "-e") == 0 || k_strcmp(argv[1], "-f") == 0)
                return shfs_lookup_rel(shfs_cwd, argv[2]) ? 0 : 1;
        }
        return 1;
    }
    /* alias / export — we accept the syntax but treat them as no-ops
     * (assignment via X=val already covered above)                  */
    if (k_strcmp(cmd, "alias") == 0 || k_strcmp(cmd, "export") == 0) return 0;
    /* sleep — cooperative spin (1s = ~PIT_HZ ticks).  Single-task
     * kernel, so we just no-op past tiny intervals.                 */
    if (k_strcmp(cmd, "sleep") == 0) return 0;
    /* history — show the last command we just ran (single slot).    */
    if (k_strcmp(cmd, "history") == 0) { k_strcpy(out, "1  (history is shell-buffered)"); return 0; }
    /* ---- system info commands ---------------------------------- */
    if (k_strcmp(cmd, "hostname") == 0) { k_strcpy(out, "falcon-1"); return 0; }
    if (k_strcmp(cmd, "id") == 0) {
        const falcon_user_t *u = users_at(SET.active_user);
        k_strcpy(out, "uid=1000(");
        k_strcat(out, u ? u->name : "falcon");
        k_strcat(out, ") gid=1000(staff)");
        return 0;
    }
    if (k_strcmp(cmd, "groups") == 0) { k_strcpy(out, "staff falcon"); return 0; }
    if (k_strcmp(cmd, "who") == 0 || k_strcmp(cmd, "w") == 0 ||
        k_strcmp(cmd, "users") == 0) {
        out[0] = 0;
        for (i32 i = 0; i < SET.user_count; i++) {
            if (i > 0) k_strcat(out, "\n");
            k_strcat(out, SET.users[i].name);
            k_strcat(out, "  tty1   (local)");
        }
        return 0;
    }
    if (k_strcmp(cmd, "uptime") == 0) {
        u32 h = 0, m = 0, s = 0; pit_uptime(&h, &m, &s);
        char num[16]; out[0] = 0;
        k_strcat(out, "up ");
        k_itoa(h, num, 10); k_strcat(out, num); k_strcat(out, "h ");
        k_itoa(m, num, 10); k_strcat(out, num); k_strcat(out, "m ");
        k_itoa(s, num, 10); k_strcat(out, num); k_strcat(out, "s, ");
        k_itoa(SET.user_count, num, 10); k_strcat(out, num);
        k_strcat(out, " user(s)");
        return 0;
    }
    if (k_strcmp(cmd, "cal") == 0) {
        rtc_time_t t; rtc_local(&t);
        char num[16];
        k_strcpy(out, loc_month_short(t.month)); k_strcat(out, " ");
        k_itoa(t.year, num, 10); k_strcat(out, num);
        return 0;
    }
    /* ps / top — single-task kernel; show one fake row              */
    if (k_strcmp(cmd, "ps") == 0 || k_strcmp(cmd, "top") == 0 ||
        k_strcmp(cmd, "jobs") == 0) {
        k_strcpy(out, "  PID TTY     TIME CMD\n    1 tty1   0:00 falcon-kernel\n   42 tty1   0:00 ");
        k_strcat(out, cmd);
        return 0;
    }
    if (k_strcmp(cmd, "kill") == 0) { k_strcpy(out, "kill: no userland processes"); return 1; }
    /* df / du / free / mount / lsblk -------------------------------- */
    if (k_strcmp(cmd, "df") == 0) {
        i32 disks = ata_probe_count();
        char num[28], tag[24];
        k_strcpy(out,
                 "Filesystem   SizeMiB  UsedMiB AvailMiB Mounted\n");
        if (disks <= 0) {
            k_strcat(out,
                     "ramfs        -       -       -       /\n");
            return 0;
        }
        for (i32 d = 0; d < disks; d++) {
            u64 sec = ata_sectors(d);
            if (sec == 0) continue;
            u32 mib = (u32)((sec * 512u) / (1024u * 1024u));
            tag[0] = 0;
            k_strcpy(tag, "ata");
            k_itoa((u32)d, num, 10);
            k_strcat(tag, num);
            k_strcat(out, tag);
            for (i32 pad = k_strlen(tag); pad < 14; pad++) k_strcat(out, " ");
            k_itoa(mib, num, 10);
            k_strcat(out, num);
            k_strcat(out, "     -       -       /\n");
        }
        return 0;
    }
    if (k_strcmp(cmd, "du") == 0) {
        shfs_du_dump(out, cap);
        return 0;
    }
    if (k_strcmp(cmd, "free") == 0) {
        char tot[28], used[28], fr[28];
        u64 mib = RAM_TOTAL_BYTES / ((u64)1024 * 1024);
        u64 u_mib = (mib > 256u) ? 256u : (mib > 32u ? mib / 8u : 8u);
        u64 f_mib = (mib > u_mib) ? (mib - u_mib) : 0;
        k_u64_to_dec(mib, tot);
        k_u64_to_dec(u_mib, used);
        k_u64_to_dec(f_mib, fr);
        k_strcpy(out,
                 "               totalMiB       usedMiB       freeMiB\nMem: ");
        k_strcat(out, tot);
        while (k_strlen(out) < 42) k_strcat(out, " ");
        k_strcat(out, used);
        while (k_strlen(out) < 58) k_strcat(out, " ");
        k_strcat(out, fr);
        k_strcat(out, "\n(mmap totals from firmware; kernel+BSS heuristic in used)");
        return 0;
    }
    if (k_strcmp(cmd, "mount") == 0) {
        k_strcpy(out, "ata0 on / type FalconFS (rw,relatime)\nshfs on /home/falcon type ramfs (rw)");
        return 0;
    }
    if (k_strcmp(cmd, "lsblk") == 0) {
        char num[28], nm[24];
        k_strcpy(out, "NAME        SIZEMiB TRAN\n");
        i32 disks = ata_probe_count();
        for (i32 i = 0; i < disks; i++) {
            u64 sec = ata_sectors(i);
            u32 mib = sec ? (u32)((sec * 512ull) / (1024ull * 1024ull)) : 0;
            nm[0] = 0;
            k_strcpy(nm, "ata");
            k_itoa((u32)i, num, 10); k_strcat(nm, num);
            k_strcat(out, nm);
            for (i32 pad = k_strlen(nm); pad < 12; pad++) k_strcat(out, " ");
            k_itoa(mib, num, 10);
            k_strcat(out, num);
            k_strcat(out, "    disk\n");
        }
        if (disks == 0)
            k_strcat(out, "(no block devices)\n");
        return 0;
    }
    /* power -------------------------------------------------------- */
    if (k_strcmp(cmd, "reboot") == 0) {
        k_strcpy(out, "reboot scheduled — see Power menu (F12)");
        return 0;
    }
    if (k_strcmp(cmd, "shutdown") == 0 || k_strcmp(cmd, "poweroff") == 0 ||
        k_strcmp(cmd, "halt") == 0) {
        k_strcpy(out, cmd); k_strcat(out, ": use F12 power menu to confirm");
        return 0;
    }
    /* which / type — search built-in keywords ---------------------- */
    if (k_strcmp(cmd, "which") == 0 || k_strcmp(cmd, "type") == 0) {
        if (argc < 2) return 1;
        static const char *BUILTINS[] = {
            "pwd","cd","ls","cat","echo","env","set","unset","export","alias",
            "rm","touch","cp","mv","mkdir","rmdir","head","tail","wc","sort",
            "uniq","grep","tr","cut","tee","find","basename","dirname","more",
            "less","xxd","hexdump","file","printf","yes","seq","expr","test",
            "[","sleep","history","hostname","id","groups","who","w","users",
            "uptime","cal","ps","top","jobs","kill","df","du","free","mount",
            "lsblk","reboot","shutdown","poweroff","halt","which","type","hwinfo",
            "uname","whoami","date","clear","help","true","false","exit",
            "man","open","xdg-open","prg","pkg","chrome","falco","heroic",
            "video","search","update","upgrade","ver","version","lscpu",NULL
        };
        for (i32 i = 0; BUILTINS[i]; i++) {
            if (k_strcmp(BUILTINS[i], argv[1]) == 0) {
                k_strcpy(out, argv[1]);
                k_strcat(out, ": shell builtin");
                return 0;
            }
        }
        k_strcpy(out, argv[1]); k_strcat(out, " not found");
        return 1;
    }
    /* unknown */
    k_strcpy(out, cmd); k_strcat(out, ": command not found");
    return 127;
}

/* Find the first occurrence of needle in haystack, returning its index
 * or -1.  We can't rely on libc.                                     */
static i32 sh_find(const char *hay, const char *needle)
{
    i32 nl = k_strlen(needle);
    for (i32 i = 0; hay[i]; i++) {
        bool ok = true;
        for (i32 j = 0; j < nl && ok; j++)
            if (hay[i + j] != needle[j]) ok = false;
        if (ok) return i;
    }
    return -1;
}

static void sh_emit(const char *s, char *out, i32 cap)
{
    if (!out) { term_push(s); return; }
    i32 k = k_strlen(out);
    while (*s && k < cap - 1) out[k++] = *s++;
    out[k] = 0;
}

/* Run one statement (already split on ';'). Recurses for if/for. */
static void sh_run_stmt(const char *line, char *out, i32 cap)
{
    /* skip leading spaces */
    while (sh_isspace(*line)) line++;
    if (!*line) return;

    /* if cmd ; then cmd ; fi */
    if (k_strncmp(line, "if ", 3) == 0) {
        i32 t = sh_find(line, " then ");
        i32 f = sh_find(line, " fi");
        if (t > 0 && f > t) {
            char cond[128], body[128];
            i32 ci = 0;
            for (i32 i = 3; i < t && ci < 127; i++) cond[ci++] = line[i];
            cond[ci] = 0;
            i32 bi = 0;
            for (i32 i = t + 6; i < f && bi < 127; i++) body[bi++] = line[i];
            body[bi] = 0;
            char buf[64]; sh_run_stmt(cond, buf, sizeof buf);
            /* exit status 0 from cond means "true": run body. We
             * approximate by checking whether `true` / non-`false` was
             * the cond first arg.                                    */
            char tok[1][64];
            i32 nt = sh_tokenise(cond, tok, 1);
            i32 truthy = (nt > 0 && k_strcmp(tok[0], "false") != 0);
            if (truthy) sh_run_stmt(body, out, cap);
        }
        return;
    }
    /* for x in a b c ; do cmd ; done */
    if (k_strncmp(line, "for ", 4) == 0) {
        i32 in_idx = sh_find(line, " in ");
        i32 do_idx = sh_find(line, " do ");
        i32 dn_idx = sh_find(line, " done");
        if (in_idx > 0 && do_idx > in_idx && dn_idx > do_idx) {
            char var[16]; i32 vi = 0;
            for (i32 i = 4; i < in_idx && vi < 15; i++) var[vi++] = line[i];
            var[vi] = 0;
            char list[128]; i32 li = 0;
            for (i32 i = in_idx + 4; i < do_idx && li < 127; i++) list[li++] = line[i];
            list[li] = 0;
            char body[128]; i32 bi = 0;
            for (i32 i = do_idx + 4; i < dn_idx && bi < 127; i++) body[bi++] = line[i];
            body[bi] = 0;
            char items[8][64];
            i32 nitems = sh_tokenise(list, items, 8);
            for (i32 i = 0; i < nitems; i++) {
                sh_var_set(var, items[i]);
                sh_run_stmt(body, out, cap);
            }
        }
        return;
    }

    /* pipe + redirect: split at first '|', then handle '>' on RHS too */
    char left[128] = {0}, right[128] = {0};
    bool has_pipe = false;
    i32 pi = sh_find(line, "|");
    if (pi >= 0) {
        for (i32 i = 0; i < pi && i < 127; i++) left[i] = line[i];
        i32 r = 0; for (i32 i = pi + 1; line[i] && r < 127; i++) right[r++] = line[i];
        has_pipe = true;
    } else {
        k_strcpy(left, line);
    }

    /* output redirect on left side */
    char redir_name[16] = {0};
    bool append = false;
    {
        i32 gg = sh_find(left, ">>");
        if (gg >= 0) {
            append = true;
            char fn[64]; i32 k = 0;
            for (i32 i = gg + 2; left[i] && k < 63; i++) if (!sh_isspace(left[i]) || k) fn[k++] = left[i];
            fn[k] = 0;
            /* strip trailing spaces in name */
            while (k > 0 && sh_isspace(fn[k - 1])) fn[--k] = 0;
            k_strcpy(redir_name, fn);
            left[gg] = 0;
        } else {
            i32 g = sh_find(left, ">");
            if (g >= 0) {
                char fn[64]; i32 k = 0;
                for (i32 i = g + 1; left[i] && k < 63; i++) if (!sh_isspace(left[i]) || k) fn[k++] = left[i];
                fn[k] = 0;
                while (k > 0 && sh_isspace(fn[k - 1])) fn[--k] = 0;
                k_strcpy(redir_name, fn);
                left[g] = 0;
            }
        }
    }

    /* run left */
    char larg[8][64];
    i32 lac = sh_tokenise(left, larg, 8);
    char lout[256];
    sh_run_argv(lac, larg, lout, sizeof lout);

    if (redir_name[0]) {
        shfs_ent_t *f = shfs_open_w_rel(shfs_cwd, redir_name, append);
        if (f) {
            u32 n = f->len;
            for (i32 i = 0; lout[i] && n < SH_FBYTES - 2; i++) f->data[n++] = lout[i];
            f->data[n++] = '\n';
            f->data[n] = 0;
            f->len = n;
        }
        return;
    }

    if (has_pipe) {
        /* feed lout as $_ to right side */
        sh_var_set("_", lout);
        char rarg[8][64];
        i32 rac = sh_tokenise(right, rarg, 8);
        char rout[256];
        /* if right is `cat`/`grep`/`wc`, treat lout as input. We
         * simplify: if right is `wc`, count words; if `grep <pat>`,
         * filter lines containing pat; otherwise fall back to running
         * the right command and concatenating its own output.        */
        if (rac > 0 && k_strcmp(rarg[0], "wc") == 0) {
            i32 nw = 0; bool in = false;
            for (i32 i = 0; lout[i]; i++) {
                if (sh_isspace(lout[i])) in = false;
                else if (!in) { in = true; nw++; }
            }
            char num[16]; k_itoa((u32)nw, num, 10);
            sh_emit(num, out, cap);
        } else if (rac >= 2 && k_strcmp(rarg[0], "grep") == 0) {
            const char *pat = rarg[1];
            i32 i = 0; bool any = false;
            while (lout[i]) {
                i32 j = i; while (lout[j] && lout[j] != '\n') j++;
                /* line is lout[i..j) */
                bool match = false;
                for (i32 s = i; s < j && !match; s++) {
                    bool ok = true;
                    for (i32 t = 0; pat[t] && ok; t++)
                        if (s + t >= j || lout[s + t] != pat[t]) ok = false;
                    if (ok) match = true;
                }
                if (match) {
                    if (any) sh_emit("\n", out, cap);
                    char tmp[160]; i32 k = 0;
                    for (i32 s = i; s < j && k < 159; s++) tmp[k++] = lout[s];
                    tmp[k] = 0;
                    sh_emit(tmp, out, cap);
                    any = true;
                }
                i = j; if (lout[i]) i++;
            }
        } else {
            sh_run_argv(rac, rarg, rout, sizeof rout);
            sh_emit(rout, out, cap);
        }
        return;
    }

    sh_emit(lout, out, cap);
}

static void sh_run_line(const char *line)
{
    /* split on ';' */
    char buf[128];
    i32 bi = 0;
    for (i32 i = 0; line[i] && i < 127; i++) {
        if (line[i] == ';') {
            buf[bi] = 0;
            char outbuf[256]; outbuf[0] = 0;
            sh_run_stmt(buf, outbuf, sizeof outbuf);
            if (outbuf[0]) {
                /* split on '\n' so multi-line output scrolls properly */
                i32 s = 0;
                for (i32 j = 0; ; j++) {
                    if (outbuf[j] == '\n' || outbuf[j] == 0) {
                        char tmp[TERM_COLS];
                        i32 k = 0;
                        for (i32 t = s; t < j && k < TERM_COLS - 1; t++) tmp[k++] = outbuf[t];
                        tmp[k] = 0;
                        if (k) term_push(tmp);
                        if (outbuf[j] == 0) break;
                        s = j + 1;
                    }
                }
            }
            bi = 0;
        } else {
            buf[bi++] = line[i];
        }
    }
    buf[bi] = 0;
    if (bi == 0) return;
    char outbuf[256]; outbuf[0] = 0;
    sh_run_stmt(buf, outbuf, sizeof outbuf);
    if (outbuf[0]) {
        i32 s = 0;
        for (i32 j = 0; ; j++) {
            if (outbuf[j] == '\n' || outbuf[j] == 0) {
                char tmp[TERM_COLS];
                i32 k = 0;
                for (i32 t = s; t < j && k < TERM_COLS - 1; t++) tmp[k++] = outbuf[t];
                tmp[k] = 0;
                if (k) term_push(tmp);
                if (outbuf[j] == 0) break;
                s = j + 1;
            }
        }
    }
}

static void render_term(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame;
    term_init();
    /* dark terminal panel for contrast against light theme */
    gfx_round_rect_a(wx + 20, wy + 8, ww - 40, wh - 28, 10, 0x101218, 255);
    gfx_round_outline(wx + 20, wy + 8, ww - 40, wh - 28, 10, PAL_HAIRLINE);

    gfx_text(wx + 30, wy + 18,
             T("FalconOS shell — POSIX subset", "FalconOS kabuğu — POSIX alt kümesi"),
             0xCFE6FF);
    gfx_text(wx + 30, wy + 38,
             T("Try: help · hwinfo · prg · update check — cat readme.txt",
              "Deneyin: help · hwinfo · prg · update check — cat readme.txt"),
             0x8AAACE);

    for (i32 i = 0; i < TERM_LINES; i++) {
        gfx_text(wx + 32, wy + 64 + i * 18, term_buf[i], 0xDEEEFF);
    }
    /* current input line */
    char line[TERM_COLS + 24];
    k_strcpy(line, "[");
    const falcon_user_t *u = users_at(SET.active_user);
    k_strcat(line, u ? u->name : "falcon");
    k_strcat(line, "@falcon ");
    /* show only the basename of CWD to keep prompt short */
    const char *base = shfs_cwd; for (i32 i = 0; shfs_cwd[i]; i++) if (shfs_cwd[i] == '/') base = &shfs_cwd[i + 1];
    k_strcat(line, base[0] ? base : "/");
    k_strcat(line, "]$ ");
    k_strcat(line, term_input);
    if ((g_ticks / 50) & 1) k_strcat(line, "_");
    gfx_text(wx + 32, wy + 64 + TERM_LINES * 18, line, COL_OK);
}

static void term_input_key(i32 key)
{
    term_init();
    if (key == KEY_ENTER) {
        char prompt[TERM_COLS];
        k_strcpy(prompt, "$ ");
        k_strcat(prompt, term_input);
        term_push(prompt);
        sh_run_line(term_input);
        term_input_len = 0;
        term_input[0]  = 0;
        return;
    }
    if (key == KEY_BACKSPACE) {
        sh_buf_pop_utf8(term_input, &term_input_len);
        return;
    }
    (void)sh_buf_append_key(term_input, &term_input_len, TERM_COLS, key);
}

/* --- Calculator ---------------------------------------------------------- */
static i32  calc_acc      = 0;
static i32  calc_buf      = 0;
static char calc_op       = '+';
static bool calc_has_buf  = false;
static char calc_disp[24] = "0";

static void calc_refresh_disp(void)
{
    i32 v = calc_has_buf ? calc_buf : calc_acc;
    char tmp[16];
    bool neg = (v < 0);
    if (neg) v = -v;
    k_itoa((u32)v, tmp, 10);
    k_strcpy(calc_disp, neg ? "-" : "");
    k_strcat(calc_disp, tmp);
}
static void calc_apply(void)
{
    if (!calc_has_buf) return;
    switch (calc_op) {
    case '+': calc_acc += calc_buf; break;
    case '-': calc_acc -= calc_buf; break;
    case '*': calc_acc *= calc_buf; break;
    case '/': calc_acc = calc_buf ? calc_acc / calc_buf : 0; break;
    }
    calc_buf = 0;
    calc_has_buf = false;
    calc_refresh_disp();
}
static void calc_input_key(i32 key)
{
    if (key >= '0' && key <= '9') {
        calc_buf = calc_buf * 10 + (key - '0');
        calc_has_buf = true;
        calc_refresh_disp();
        return;
    }
    if (key == '+' || key == '-' || key == '*' || key == '/') {
        calc_apply();
        calc_op = (char)key;
        return;
    }
    if (key == KEY_ENTER || key == '=') { calc_apply(); return; }
    if (key == 'c' || key == 'C')        {
        calc_acc = calc_buf = 0; calc_has_buf = false; calc_op = '+';
        calc_refresh_disp(); return;
    }
}
static void render_calc(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame; (void)wh;
    section(wx, wy, T("Calculator", "Hesap makinesi"),
            T("+ − × ÷ digits  c=clear  Enter =", "+ − × ÷ rakamlar c=temizle Enter ="));

    /* readout */
    i32 dx = wx + 24, dy = wy + 60, dw = ww - 48, dh = 60;
    gfx_round_rect_a(dx, dy, dw, dh, 12, PAL_PANEL_DEEP, 255);
    gfx_round_outline(dx, dy, dw, dh, 12, PAL_HAIRLINE);
    gfx_text(dx + dw - gfx_text_width(calc_disp) - 16, dy + 22, calc_disp, PAL_TEXT);

    /* op + acc indicator */
    char hint[40] = "acc ";
    char tmp[12];
    bool neg = (calc_acc < 0);
    k_itoa(neg ? (u32)(-calc_acc) : (u32)calc_acc, tmp, 10);
    if (neg) k_strcat(hint, "-");
    k_strcat(hint, tmp);
    k_strcat(hint, "   op ");
    char opbuf[2] = {calc_op, 0};
    k_strcat(hint, opbuf);
    gfx_text(dx + 16, dy + 22, hint, PAL_TEXT_DIM);

    /* on-screen keypad (visual only) */
    const char *keys = "789+456-123*0=c/";
    i32 bx = wx + 24, by = wy + 140, bw = (ww - 48) / 4 - 6, bh = 36;
    for (i32 i = 0; i < 16; i++) {
        i32 r = i / 4, c = i % 4;
        i32 x = bx + c * (bw + 8), y = by + r * (bh + 8);
        bool op = (c == 3) || keys[i] == '=' || keys[i] == 'c';
        gfx_round_rect_a(x, y, bw, bh, 8, op ? PAL_ACCENT_DIM : PAL_PANEL_DEEP, 255);
        gfx_round_outline(x, y, bw, bh, 8, PAL_HAIRLINE);
        char b[2] = {keys[i], 0};
        gfx_text_centered(x + bw / 2, y + bh / 2 - 8, b, PAL_TEXT);
    }
}

/* --- Settings (v5: live, persistent across the kernel) ------------------- */
/*  Settings is a multi-row form; the user selects a row with up/down,
 *  and uses left/right (or Enter) to toggle / cycle the value.            */
typedef enum {
    SR_THEME = 0, SR_ACCENT, SR_AERO, SR_LANG, SR_KBD, SR_TZ, SR_DOCK,
    SR_ANIM, SR_WIDGETS, SR_VIEWPORT, SR_PASSWORD, SR_USERS, SR_DRIVERS,
    SR_SAVE, SR_LOCK, SR_COUNT
} set_row_t;

static i32 set_row = 0;
static i32 settings_scroll=0;
static i32 settings_view_min=0,settings_view_max=0;
static const char *VIEWPORT_NAMES[] = {
    "native", "1280x800", "1920x1080", "2560x1440", "1024x768",
};
static const i32 VIEWPORT_W[] = { 0, 1280, 1920, 2560, 1024 };
static const i32 VIEWPORT_H[] = { 0,  800, 1080, 1440,  768 };
static i32 set_viewport_idx = 0;

/* Curated timezone presets — covers the cities our target users live in
 * without dragging in a full zoneinfo / DST database.                   */
typedef struct { const char *label; i32 minutes; } tz_preset_t;
static const tz_preset_t TZ_PRESETS[] = {
    { "Honolulu (UTC-10)",  -600 },
    { "Los Angeles (UTC-8)", -480 },
    { "New York (UTC-5)",    -300 },
    { "Sao Paulo (UTC-3)",   -180 },
    { "London (UTC+0)",         0 },
    { "Paris (UTC+1)",         60 },
    { "Istanbul (UTC+3)",     180 },
    { "Dubai (UTC+4)",        240 },
    { "Mumbai (UTC+5:30)",    330 },
    { "Bangkok (UTC+7)",      420 },
    { "Beijing (UTC+8)",      480 },
    { "Tokyo (UTC+9)",        540 },
    { "Sydney (UTC+10)",      600 },
};
#define TZ_PRESET_COUNT ((i32)(sizeof TZ_PRESETS / sizeof *TZ_PRESETS))

static i32 tz_preset_index(void)
{
    for (i32 i = 0; i < TZ_PRESET_COUNT; i++)
        if (TZ_PRESETS[i].minutes == SET.tz_minutes) return i;
    return 6; /* Istanbul */
}

static char set_pwd[24];
static i32  set_pwd_len = 0;
static bool set_pwd_editing = false;

static void set_input_key(i32 key)
{
    if (set_pwd_editing) {
        if (key == KEY_BACKSPACE) { sh_buf_pop_utf8(set_pwd, &set_pwd_len); return; }
        if (key == KEY_ENTER)     {
            set_pwd[set_pwd_len] = 0;
            k_strcpy(SET.password, set_pwd);
            set_pwd_editing = false;
            return;
        }
        if (key == KEY_ESC)       { set_pwd_editing = false; return; }
        (void)sh_buf_append_key(set_pwd, &set_pwd_len, 24, key);
        return;
    }

    if (key == KEY_UP   && set_row > 0)             set_row--;
    if (key == KEY_DOWN && set_row < SR_COUNT - 1)  set_row++;
    if(set_row<settings_scroll)settings_scroll=set_row;

    if (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_ENTER || key == ' ') {
        i32 d = (key == KEY_LEFT) ? -1 : 1;
        switch (set_row) {
        case SR_THEME: {
            i32 v = (i32)SET.theme + d;
            if (v < 0)             v = THEME_COUNT - 1;
            if (v >= THEME_COUNT)  v = 0;
            SET.theme = (theme_t)v;
            break;
        }
        case SR_ACCENT:
            { i32 v = (i32)SET.accent + d;
              if (v < 0)             v = ACC_COUNT - 1;
              if (v >= ACC_COUNT)    v = 0;
              SET.accent = (accent_t)v; }
            break;
        case SR_AERO:
            SET.aero_enabled = !SET.aero_enabled;
            break;
        case SR_LANG:
            { i32 v = (i32)SET.lang + d;
              if (v < 0)             v = LANG_COUNT - 1;
              if (v >= LANG_COUNT)   v = 0;
              SET.lang = (lang_t)v; }
            break;
        case SR_KBD:
            { i32 v = (i32)SET.kbd_layout + d;
              if (v < 0)               v = KBD_COUNT - 1;
              if (v >= KBD_COUNT)      v = 0;
              SET.kbd_layout = (kbd_layout_t)v; }
            break;
        case SR_TZ:
            { i32 v = (tz_preset_index() + d + TZ_PRESET_COUNT) % TZ_PRESET_COUNT;
              SET.tz_minutes = TZ_PRESETS[v].minutes; }
            break;
        case SR_DOCK:
            { i32 v = SET.dock_size + d; if (v < 0) v = 0; if (v > 4) v = 4;
              SET.dock_size = v; }
            break;
        case SR_ANIM:
            SET.animations = !SET.animations;
            break;
        case SR_WIDGETS:
            SET.widgets_shown = !SET.widgets_shown;
            break;
        case SR_VIEWPORT:
            { i32 n = (i32)(sizeof VIEWPORT_NAMES / sizeof *VIEWPORT_NAMES);
              i32 v = (set_viewport_idx + d + n) % n;
              set_viewport_idx = v;
              SET.viewport_w = VIEWPORT_W[v];
              SET.viewport_h = VIEWPORT_H[v]; }
            break;
        case SR_PASSWORD:
            /* Never write plaintext passwords into persistent settings.
             * User credentials are PBKDF2-hashed during first-run setup.
             * Password changes require a future authenticated account UI. */
            break;
        case SR_USERS:
            /* cycle which user is "default" (auto-focused on next boot)   */
            for (i32 i = 0, hops = 0; i < FALCON_MAX_USERS && hops < 2; i++) {
                i32 idx = (SET.default_user + d + FALCON_MAX_USERS) % FALCON_MAX_USERS;
                if (SET.users[idx].in_use) { users_set_default(idx); break; }
                d += (d > 0) ? 1 : -1;
                hops++;
            }
            break;
        case SR_SAVE:
            diskdb_save();
            break;
        case SR_LOCK:
            lockscreen_lock();
            break;
        }
    }
}

#define SR_BOX_H 32

static void s_row(i32 x, i32 y, i32 w, const char *label, const char *val,
                  bool active, u32 valcolor)
{
    y-=settings_scroll*36;
    if(y<settings_view_min||y+SR_BOX_H>settings_view_max)return;
    gfx_round_rect_a(x, y, w, SR_BOX_H, 11,
                     active ? 0xDBE9FFu : PAL_PANEL_DEEP, 255);
    gfx_round_outline(x, y, w, SR_BOX_H, 11,
                      active ? 0x337DF6u : PAL_HAIRLINE);
    gfx_text(x + 14, y + 9, label, PAL_TEXT);
    gfx_text(x + w - gfx_text_width(val) - 14, y + 9, val, valcolor);
}

static void render_settings(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame;
    i32 sx=wx+22,sw=ww-44;
    gfx_round_rect_a(sx,wy+7,sw,100,19,PAL_PANEL_DEEP,255);
    gfx_round_outline(sx,wy+7,sw,100,19,PAL_HAIRLINE);
    gfx_round_rect(sx+18,wy+24,49,49,16,PAL_ACCENT);
    gfx_circle_outline(sx+43,wy+48,14,0xFFFFFFu);
    gfx_circle(sx+43,wy+48,5,0xFFFFFFu);
    gfx_text_lg(sx+85,wy+22,T("Make FalconOS yours","FalconOS'u kisisellestir"),PAL_TEXT);
    gfx_text(sx+85,wy+60,
        T("Themes / colors / accounts / display","Temalar / renkler / hesaplar / ekran"),PAL_TEXT_DIM);
    /* Interactive theme swatches: native palette changes immediately. */
    static const u32 swatches[THEME_COUNT] = {
        0xE8EFF8u,0x202937u,0x6CB7DCu,0xB9C9D8u,0xDAA5A0u
    };
    i32 mx_theme,my_theme;bool held_theme;
    mouse_get(&mx_theme,&my_theme,&held_theme);(void)held_theme;
    for(i32 t=0;t<THEME_COUNT;t++){
        i32 cx=sx+sw-159+t*29,cy=wy+80;
        gfx_round_rect(cx,cy,24,15,5,swatches[t]);
        if((i32)SET.theme==t)
            gfx_round_outline(cx-2,cy-2,28,19,7,PAL_ACCENT);
        if(wm_click_enabled()&&mx_theme>=cx&&mx_theme<cx+24 &&
           my_theme>=cy&&my_theme<cy+15){
            SET.theme=(theme_t)t;(void)mouse_consume_click();
        }
    }
    i32 sy=wy+124;
    i32 step=36;
    settings_view_min=sy-1;
    settings_view_max=wy+wh-42;
    i32 visible=(settings_view_max-settings_view_min)/step;
    if(visible<2)visible=2;
    if(set_row>=settings_scroll+visible)
        settings_scroll=set_row-visible+1;
    if(set_row<settings_scroll)settings_scroll=set_row;
    gfx_text(wx+ww-192,wy+112,
      "Up/Down   Left/Right",PAL_TEXT_DIM);
    if(wm_click_enabled()){
        i32 mx,my;bool pressed;mouse_get(&mx,&my,&pressed);(void)pressed;
        if(mx>=sx&&mx<sx+sw&&my>=settings_view_min &&
           my<settings_view_max){
            i32 picked=settings_scroll+(my-settings_view_min)/step;
            if(picked>=0&&picked<SR_COUNT){
                (void)mouse_consume_click();
                set_row=picked;
                set_input_key(KEY_RIGHT);
            }
        }
    }     /* row vertical pitch                            */
    char val[40];

    /* Theme ------------------------------------------------------------ */
    {
        const char *theme_names[THEME_COUNT] = {
            "Lumen (Light)", "Nox (Dark)", "Liquid Glass",
            "Nordic", "Rose Gold"
        };
        s_row(sx, sy + SR_THEME * step, sw,
              T("Theme", "Tema"),
              theme_names[SET.theme],
              set_row == SR_THEME, PAL_TEXT);
    }

    /* Accent ----------------------------------------------------------- */
    {
        const char *names[ACC_COUNT] = { "Blue", "Purple", "Green", "Pink", "Graphite" };
        s_row(sx, sy + SR_ACCENT * step, sw,
              T("Accent", "Vurgu"), names[SET.accent],
              set_row == SR_ACCENT, PAL_ACCENT);
        i32 accent_y=sy+(SR_ACCENT-settings_scroll)*step+SR_BOX_H/2;
        if(accent_y>settings_view_min+6&&accent_y<settings_view_max-6)
            gfx_circle(sx+sw-14,accent_y,6,PAL_ACCENT);
    }

    /* Aero --- frosted glass toggle ------------------------------------ */
    s_row(sx, sy + SR_AERO * step, sw,
          T("Aero (transparency)", "Aero (şeffaflık)"),
          SET.aero_enabled ? T("on", "açık") : T("off", "kapalı"),
          set_row == SR_AERO,
          SET.aero_enabled ? COL_OK : PAL_TEXT_DIM);

    /* Language --------------------------------------------------------- */
    s_row(sx, sy + SR_LANG * step, sw,
          TX("Language", "Dil", "Sprache", "Langue", "Idioma"),
          lang_name(SET.lang),
          set_row == SR_LANG, PAL_TEXT);

    /* Keyboard layout -------------------------------------------------- */
    s_row(sx, sy + SR_KBD * step, sw,
          T("Keyboard layout", "Klavye düzeni"),
          kbd_layout_name(SET.kbd_layout),
          set_row == SR_KBD, PAL_TEXT);

    /* Timezone --------------------------------------------------------- */
    s_row(sx, sy + SR_TZ * step, sw,
          T("Timezone", "Zaman dilimi"),
          TZ_PRESETS[tz_preset_index()].label,
          set_row == SR_TZ, PAL_TEXT);

    /* Dock size -------------------------------------------------------- */
    {
        char num[8]; k_itoa(50 + SET.dock_size * 9, num, 10);
        k_strcpy(val, num); k_strcat(val, " px");
        s_row(sx, sy + SR_DOCK * step, sw,
              T("Dock size", "Dock boyutu"), val,
              set_row == SR_DOCK, PAL_TEXT);
    }

    /* Animations ------------------------------------------------------- */
    s_row(sx, sy + SR_ANIM * step, sw,
          T("Animations", "Animasyonlar"),
          SET.animations ? T("on", "açık") : T("off", "kapalı"),
          set_row == SR_ANIM, SET.animations ? COL_OK : PAL_TEXT_DIM);

    /* Widgets ---------------------------------------------------------- */
    s_row(sx, sy + SR_WIDGETS * step, sw,
          T("Desktop widgets", "Masaüstü widgetlar"),
          SET.widgets_shown ? T("shown", "açık") : T("hidden", "gizli"),
          set_row == SR_WIDGETS,
          SET.widgets_shown ? COL_OK : PAL_TEXT_DIM);

    /* Viewport / resolution ------------------------------------------- */
    s_row(sx, sy + SR_VIEWPORT * step, sw,
          T("Resolution", "Çözünürlük"),
          VIEWPORT_NAMES[set_viewport_idx],
          set_row == SR_VIEWPORT, PAL_TEXT);

    /* Password --------------------------------------------------------- */
    {
        const char *p;
        if (set_pwd_editing) {
            static char masked[24];
            for (i32 i = 0; i < set_pwd_len; i++) masked[i] = '*';
            masked[set_pwd_len] = 0;
            p = masked;
        } else {
            p = "Protected by account login";
        }
        s_row(sx, sy + SR_PASSWORD * step, sw,
              T("Password", "Parola"), p,
              set_row == SR_PASSWORD,
              k_strlen(SET.password) ? COL_OK : PAL_TEXT_DIM);
    }

    /* Users — list count + which user is default ----------------------- */
    {
        char ubuf[40];
        k_itoa((u32)SET.user_count, ubuf, 10);
        k_strcat(ubuf, T(" users  -  default: ", " kullanıcı  -  varsayılan: "));
        if (SET.default_user >= 0 && SET.default_user < FALCON_MAX_USERS &&
            SET.users[SET.default_user].in_use) {
            k_strcat(ubuf, SET.users[SET.default_user].name);
        } else {
            k_strcat(ubuf, "?");
        }
        s_row(sx, sy + SR_USERS * step, sw,
              T("Users", "Kullanıcılar"), ubuf,
              set_row == SR_USERS, PAL_ACCENT);
    }

    /* Drivers status — one-line health summary built from runtime
     * counters in kbd.c / linux/ata_pio.c.  The label gets a green dot
     * when nothing has gone wrong, an amber/red one when a driver
     * reports retries or hard failures.                              */
    {
        u32 ks, kk, kd; kbd_stats(&ks, &kk, &kd);
        u32 ms, md, mc; mouse_stats(&ms, &md, &mc);
        u32 ar, aw, art, af; ata_stats(&ar, &aw, &art, &af);

        char dbuf[120];
        char tmp[16];
        /* K: keys (drops) | M: clicks (drops) | D: io (retries / FAIL)   */
        k_strcpy(dbuf, "K:");
        k_itoa(kk, tmp, 10); k_strcat(dbuf, tmp);
        if (kd) { k_strcat(dbuf, " drop "); k_itoa(kd, tmp, 10); k_strcat(dbuf, tmp); }
        k_strcat(dbuf, "  M:");
        k_itoa(mc, tmp, 10); k_strcat(dbuf, tmp);
        if (md) { k_strcat(dbuf, " drop "); k_itoa(md, tmp, 10); k_strcat(dbuf, tmp); }
        k_strcat(dbuf, "  D:");
        k_itoa(ar + aw, tmp, 10); k_strcat(dbuf, tmp);
        if (art) { k_strcat(dbuf, " retry "); k_itoa(art, tmp, 10); k_strcat(dbuf, tmp); }
        if (af)  { k_strcat(dbuf, " FAIL ");  k_itoa(af,  tmp, 10); k_strcat(dbuf, tmp); }

        u32 status_color = COL_OK;
        if (kd || md || art) status_color = COL_WARN;
        if (af)              status_color = COL_ERR;

        s_row(sx, sy + SR_DRIVERS * step, sw,
              T("Drivers", "Sürücüler"), dbuf,
              set_row == SR_DRIVERS, status_color);
    }

    /* Save to disk ----------------------------------------------------- */
    s_row(sx, sy + SR_SAVE * step, sw,
          T("Save to disk", "Diske kaydet"),
          T("Enter", "Enter"),
          set_row == SR_SAVE, COL_OK);

    /* Lock ------------------------------------------------------------- */
    s_row(sx, sy + SR_LOCK * step, sw,
          T("Lock screen now", "Kilit ekranı"),
          T("Enter", "Enter"),
          set_row == SR_LOCK, COL_WARN);
}

/* --- Notes --------------------------------------------------------------- */
#define NOTES_MAX 256
#define NOTES_FILE "/home/falcon/Desktop/Notes.txt"
static char notes_buf[NOTES_MAX] = "Notes - FalconOS\n\n";
static i32 notes_len = 0;
static bool notes_loaded;
static void notes_init_once(void) {
    if(notes_loaded)return;
    notes_loaded=true;
    shfs_ent_t *saved=shfs_lookup(NOTES_FILE);
    if(saved&&!saved->is_dir&&saved->len<NOTES_MAX){
        k_memcpy(notes_buf,saved->data,saved->len);
        notes_len=(i32)saved->len;
        notes_buf[notes_len]=0;
    }else notes_len=k_strlen(notes_buf);
}
static void notes_save(void){
    shfs_ent_t *f=shfs_open_w_abs(NOTES_FILE,false);
    if(!f)return;
    k_memcpy(f->data,notes_buf,(u32)notes_len);
    f->len=(u32)notes_len;f->data[notes_len]=0;
    /* PFS copy-on-write writeback occurs in the kernel main loop. */
}
static void notes_input_key(i32 key)
{
    notes_init_once();
    if (key == KEY_BACKSPACE) {
        sh_buf_pop_utf8(notes_buf, &notes_len);
    } else if (key == KEY_ENTER) {
        if (notes_len < NOTES_MAX - 1) {
            notes_buf[notes_len++] = '\n';
            notes_buf[notes_len] = 0;
        }
    } else {
        (void)sh_buf_append_key(notes_buf, &notes_len, NOTES_MAX, key);
    }
    notes_save();
#ifdef FALCON_QEMU_UI_GALLERY
    /* Real keyboard-to-editor evidence; do not infer edit success from window paint. */
    outb(0xE9, 't');
#endif
}
static void render_notes(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame;
    notes_init_once();
    section(wx, wy, T("Notes", "Notlar"),
            T("Type freely · Backspace · Enter newline",
              "Özgür yazın · Geri Sil · Enter satır"));

    i32 px = wx + 24, py = wy + 60, pw = ww - 48, ph = wh - 84;
    gfx_round_rect_a(px, py, pw, ph, 12, PAL_PANEL_DEEP, 255);
    gfx_round_outline(px, py, pw, ph, 12, PAL_HAIRLINE);

    /* render buf with naive line wrapping at \n */
    i32 ty = py + 12;
    i32 line_start = 0;
    for (i32 i = 0; i <= notes_len; i++) {
        if (notes_buf[i] == '\n' || i == notes_len) {
            char tmp[80];
            i32 n = i - line_start;
            if (n > 78) n = 78;
            k_memcpy(tmp, &notes_buf[line_start], n);
            tmp[n] = 0;
            gfx_text(px + 14, ty, tmp, PAL_TEXT);
            ty += 18;
            line_start = i + 1;
            if (ty > py + ph - 24) break;
        }
    }
    /* caret */
    if ((g_ticks / 50) & 1) {
        i32 cols = sh_utf8_chars_between(notes_buf, line_start, notes_len);
        gfx_text(px + 14 + cols * 8, ty, "_", PAL_ACCENT);
    }
}

/* --- Calendar ------------------------------------------------------------ */
static void render_calendar(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame; (void)wh;
    section(wx, wy, T("Calendar", "Takvim"),
            T("Month grid driven by uptime", "Aydınlık ızgarası (uptime ile)"));

    /* fake "today" derived from uptime hours, so it changes if you wait */
    u32 H, M, S; pit_uptime(&H, &M, &S);
    (void)M; (void)S;
    i32 today = (i32)(H % 28) + 1;

    /* month grid: 4 rows x 7 cols = 28 days */
    i32 cw = (ww - 60) / 7;
    i32 ch = (wh - 90) / 4;
    if (ch > 60) ch = 60;
    static const char *DOW[] = { "S","M","T","W","T","F","S" };
    for (i32 c = 0; c < 7; c++) {
        i32 x = wx + 30 + c * cw;
        gfx_text_centered(x + cw / 2, wy + 60, DOW[c], PAL_TEXT_DIM);
    }
    for (i32 d = 0; d < 28; d++) {
        i32 r = d / 7, c = d % 7;
        i32 x = wx + 30 + c * cw;
        i32 y = wy + 84 + r * ch;
        bool isToday = ((d + 1) == today);
        gfx_round_rect_a(x + 2, y + 2, cw - 4, ch - 4, 8,
                         isToday ? PAL_ACCENT : PAL_PANEL_DEEP, 255);
        gfx_round_outline(x + 2, y + 2, cw - 4, ch - 4, 8, PAL_HAIRLINE);
        char buf[4]; k_itoa(d + 1, buf, 10);
        gfx_text_centered(x + cw / 2, y + ch / 2 - 8, buf,
                          isToday ? 0xFFFFFF : PAL_TEXT);
    }
}

/* --- Gallery (Lumen palette) -------------------------------------------- */
static void render_gallery(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame; (void)wh;
    section(wx, wy, T("Gallery", "Galeri"),
            T("Lumen palette swatches", "Lumen palet kartelası"));

    struct { u32 c; const char *n; } SW[] = {
        { PAL_ACCENT,    "Blue 50"   },
        { COL_OK,        "Green 50"  },
        { COL_WARN,      "Amber 50"  },
        { COL_ERR,       "Red 50"    },
        { COL_PURPLE,    "Purple 50" },
        { COL_TEAL,      "Teal 50"   },
        { PAL_BG_TOP,    "BG Top"    },
        { PAL_BG_BOT,    "BG Bottom" },
        { PAL_PANEL,     "Panel"     },
        { PAL_PANEL_HI,  "Hairline"  },
    };
    i32 n = (i32)(sizeof SW / sizeof *SW);
    i32 cw = (ww - 60) / 5;
    i32 ch = 110;
    for (i32 i = 0; i < n; i++) {
        i32 r = i / 5, c = i % 5;
        i32 x = wx + 24 + c * (cw + 4);
        i32 y = wy + 70 + r * (ch + 12);
        gfx_round_rect(x, y, cw, ch - 28, 8, SW[i].c);
        gfx_round_outline(x, y, cw, ch - 28, 8, PAL_HAIRLINE);
        gfx_text(x + 4, y + ch - 24, SW[i].n, PAL_TEXT_DIM);

        char hex[16] = "0x";
        char tmp[8]; k_itoa(SW[i].c, tmp, 16);
        i32 pad = 6 - k_strlen(tmp);
        for (i32 p = 0; p < pad; p++) k_strcat(hex, "0");
        k_strcat(hex, tmp);
        gfx_text(x + 4, y + ch - 8, hex, PAL_TEXT_FAINT);
    }
}

/* Falco Search — live Wikipedia API via explicitly verified host HTTPS.
 * No fabricated result cards or static fake search engine.
 * Guest-to-host NAT hop is plain TCP; real TLS is validated on trusted host.
 */
static char falco_query[80]="FalconOS";
static i32 falco_query_len=8;
static i32 falco_sel=0;
static char falco_http[4096],falco_results[3100];
static char falco_status[128]="Enter searches live Wikipedia, via HTTPS host gateway.";
static bool falco_has_results;
static bool falco_dhcp_done;
static bool falco_web_view;
static void falco_set_query(const char *q){
    falco_query_len=0;
    while(q&&q[falco_query_len]&&falco_query_len<79){
        falco_query[falco_query_len]=q[falco_query_len];
        falco_query_len++;
    }
    falco_query[falco_query_len]=0;
    falco_sel=0;
    falco_has_results=false;
    falco_web_view=false;
}
static void falco_search(void){
    falco_has_results=false;
    if(!falco_query_len)return;
    /* A URL in Falco is a navigation, not a Wikipedia search. */
    bool has_dot=false,has_space=false;
    for(i32 j=0;j<falco_query_len;j++){
        if(falco_query[j]=='.')has_dot=true;
        if(falco_query[j]==' '||falco_query[j]=='\n')has_space=true;
    }
    if(!has_space && (has_dot || k_strncmp(falco_query,"https://",8)==0 ||
                      k_strncmp(falco_query,"http://",7)==0)){
        falco_open_site(falco_query);return;
    }
    if(!net_present()){
        k_strcpy(falco_status,"No RTL8139 network card is active");
        return;
    }
    if(!falco_dhcp_done){falco_dhcp_done=true;(void)net_dhcp();}
    char path[300];i32 i=0;
    const char *prefix="/search/";
    for(i32 j=0;prefix[j];j++)path[i++]=prefix[j];
    static const char hex[]="0123456789ABCDEF";
    for(i32 j=0;j<falco_query_len && i<270;j++){
        u8 c=(u8)falco_query[j];
        if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
           (c>='0'&&c<='9')||c=='-'){
            path[i++]=(char)c;
        }else{
            path[i++]='%';path[i++]=hex[c>>4];path[i++]=hex[c&15u];
        }
    }
    path[i]=0;
    k_strcpy(falco_status,"Searching Wikipedia HTTPS...");
    if(!native_http_get_port(net_gateway(),18444,path,falco_http,sizeof falco_http)||
       !sh_contains_ci(falco_http,"X-Falcon-Host-HTTPS-Verified: yes")){
        k_strcpy(falco_status,
          "HTTPS host gateway unavailable. Start falcon_https_gateway.py");
        return;
    }
    const char *body=falco_http;
    while(body[0]&&!(body[0]=='\r'&&body[1]=='\n'&&
                      body[2]=='\r'&&body[3]=='\n'))body++;
    if(!body[0]){
        k_strcpy(falco_status,"Malformed search response");return;
    }
    body+=4;
    i32 n=0;
    while(body[n]&&n<(i32)sizeof falco_results-1){
        u8 c=(u8)body[n];
        falco_results[n]=(c<32&&c!='\n')?' ':(char)c; /* retain validated UTF-8 for Turkish */
        n++;
    }
    falco_results[n]=0;
    falco_sel=0;falco_has_results=true;
    k_strcpy(falco_status,
         "Live Wikipedia results - host certificate verified");
}
static void falco_input_key(i32 key){
    if ((kbd_mod_state() & (1u<<1)) && (key=='l'||key=='L')) {
        if(falco_web_view){chrome_focus_url();return;}
        falco_query[0]=0;falco_query_len=0;falco_has_results=false;return;
    }
    if((kbd_mod_state() & (1u<<2)) && key==KEY_LEFT && falco_web_view){
        falco_web_view=false;return;
    }
    /* A deliberate GitHub-docs fallback is NOT the live website. */
    if(key==KEY_F8){
        falco_open_site_host(
            "https://raw.githubusercontent.com/hanefimert2016-oss/FalconOS/FalconOS-1-release/README.md");
        return;
    }
    if(falco_web_view){
        if(key==KEY_F3){falco_web_view=false;return;}
        chrome_input_key(key);return;
    }
    if(key==KEY_F6){falco_open_site("https://falconos.tech/");return;}
    if(key==KEY_F7){falco_open_site_host("https://falconos.tech/");return;}
    if(key==KEY_F4){
        falco_query[0]=0;falco_query_len=0;
        falco_has_results=false;falco_sel=0;return;
    }
    if(key==KEY_ENTER||key==KEY_F5){falco_search();return;}
    if(key==KEY_UP){if(falco_sel>0)falco_sel--;return;}
    if(key==KEY_DOWN){if(falco_sel<40)falco_sel++;return;}
    if(key==KEY_BACKSPACE){
        sh_buf_pop_utf8(falco_query,&falco_query_len);
        return;
    }
    (void)sh_buf_append_key(falco_query,&falco_query_len,80,key);
}
static void render_falco(i32 wx,i32 wy,i32 ww,i32 wh,u32 frame){
    if(falco_web_view){render_browser(wx,wy,ww,wh,frame);return;}
    (void)frame;
    i32 x=wx+22,w=ww-44;
    /* Pointer hit-test matches real drawn toolbar controls. */
    i32 mx,my;bool held;mouse_get(&mx,&my,&held);(void)held;
    if(wm_click_enabled()&&mx>=x&&mx<x+w){
        if(my>=wy+109&&my<wy+160){
            (void)mouse_consume_click();
            if(mx>=x+w-126)falco_search();
            return;
        }
        if(my>=wy+165&&my<wy+200){
            i32 rel=mx-x;
            (void)mouse_consume_click();
            if(rel<160)falco_open_site("https://falconos.tech/");
            else if(rel<326)falco_open_site_host("https://falconos.tech/");
            else if(rel<492)falco_input_key(KEY_F8);
            else falco_search();
            return;
        }
    }
    gfx_round_rect_a(x,wy+12,w,84,20,0xDBEAFE,240);
    gfx_round_rect(x+14,wy+26,48,48,16,0x2269D9);
    gfx_text_lg_centered(x+38,wy+34,"F",0xFFFFFF);
    gfx_text_lg(x+80,wy+31,"Falco Search",0x16355F);
    gfx_text(x+80,wy+62,"Live results, not an offline demo index",0x597698);
    gfx_round_rect_a(x,wy+109,w,51,18,PAL_PANEL,255);
    gfx_round_outline(x,wy+109,w,51,18,PAL_ACCENT);
    gfx_circle_outline(x+23,wy+134,8,0x487CCA);
    gfx_line(x+28,wy+139,x+35,wy+146,0x487CCA);
    gfx_text(x+51,wy+127,falco_query_len?falco_query:"Search Wikipedia...",PAL_TEXT);
    gfx_round_rect(x+w-110,wy+117,100,35,13,0x246DE8);
    gfx_text_centered(x+w-60,wy+128,"Enter",0xFFFFFF);
    /* Explicit clickable HTTPS navigation; no fake "button" labels. */
    const char *actions[4]={"Live site","Host HTTPS","GitHub docs","Search"};
    for(i32 i=0;i<4;i++){
        i32 bx=x+i*166;
        if(bx+157>x+w)break;
        gfx_round_rect_a(bx,wy+165,157,34,10,
            i==0?0x246DE8u:PAL_PANEL_HI,250);
        gfx_round_outline(bx,wy+165,157,34,10,PAL_HAIRLINE);
        gfx_text_centered(bx+78,wy+175,actions[i],
            i==0?0xFFFFFFu:PAL_TEXT);
    }
    gfx_text(x+4,wy+207,falco_status,0x4B779E);
    gfx_round_rect_a(x,wy+230,w,wh-290,19,PAL_PANEL,240);
    gfx_round_outline(x,wy+230,w,wh-290,19,PAL_HAIRLINE);
    if(!falco_has_results){
        gfx_text_lg(x+26,wy+253,"Discover something new",PAL_TEXT);
        gfx_text(x+26,wy+283,"Type a topic and press Enter.",PAL_TEXT_DIM);
        gfx_text(x+26,wy+312,
            "HTTPS is verified on the Arch host (not inside FalconOS).",PAL_TEXT_DIM);
        gfx_text(x+26,wy+341,
            "Run the companion gateway first; nothing is simulated.",PAL_TEXT_FAINT);
    }else{
        char line[116];i32 col=0,row=0,logical=0;
        i32 rows=(wh-302)/21;if(rows>19)rows=19;
        i32 max_chars=(w-44)/8;
        if(max_chars>106)max_chars=106;
        for(i32 i=0;falco_results[i]&&row<rows;i++){
            char c=falco_results[i];
            if(c=='\n'||col>=max_chars){
                line[col]=0;
                if(logical>=falco_sel)
                    gfx_text(x+22,wy+251+(row++)*21,line,PAL_TEXT);
                logical++;col=0;
                if(c=='\n')continue;
            }
            if(col<115)line[col++]=c;
        }
        if(col && row<rows && logical>=falco_sel){
            line[col]=0;
            gfx_text(x+22,wy+251+row*21,line,PAL_TEXT);
        }
    }
    gfx_text(x+5,wy+wh-36,
        "Ctrl+L search | Enter go | F5 refresh | Alt+Left back | F8 Docs",
        PAL_TEXT_FAINT);
}

/* --- Falcon Browser: actual guest TCP + authenticated HTTPS only ---------
 * Not a Chromium/WebKit engine. Real certificate-verified HTTPS fetch,
 * bounded HTML-to-text rendering; no CSS, JavaScript, cookies or sandbox.
 * In preview builds TLS verification is done by native BearSSL in Ring0.
 * HTTPS failure NEVER falls back to plaintext HTTP.
 */
static char browser_address[224]="https://example.com/";
static i32 browser_address_len=20;
static bool browser_address_focus=true;
static void chrome_focus_url(void){
    /* Ctrl+L selects a fresh address entry, independent of current focus. */
    browser_address[0]=0;browser_address_len=0;
    browser_address_focus=true;
}
/* F6 enables a clearly labeled host-validated TLS proxy, never automatic. */
/* Standard build has no native BearSSL; use trusted host HTTPS bridge.
 * The status bar always labels this as HOST TLS, never guest-native TLS. */
#ifdef FALCON_BEARSSL
static bool browser_host_gateway=false;
#else
static bool browser_host_gateway=true;
#endif
static bool browser_dhcp_attempted=false;
static char browser_result[4096];
static char browser_text[4096];
static char browser_status[120]="Type an HTTPS address, then press Enter to load.";
static i32 browser_scroll;
static bool browser_loaded;
static void browser_page_from_http(void){
    /* Only show actual bytes authenticated by the TLS transport.
     * Strip active markup and avoid escape/control injection into GUI. */
    const char *p=browser_result;
    while(*p&&!(p[0]=='\r'&&p[1]=='\n'&&p[2]=='\r'&&p[3]=='\n'))p++;
    if(!*p){k_strcpy(browser_status,"Invalid HTTP response");return;}
    p+=4;
    i32 n=0;bool intag=false;bool space=false;
    for(;*p&&n<3900;p++){
        char c=*p;
        if(c=='<'){intag=true;continue;}
        if(intag){
            if(c=='>'){intag=false;space=true;}
            continue;
        }
        if(c=='\r'||c=='\n'||c=='\t'||c==' '){
            space=true;continue;
        }
        if((u8)c<32u)continue; /* preserve UTF-8 letters; glyph renderer validates */
        if(space&&n>0&&browser_text[n-1]!=' ')browser_text[n++]=' ';
        space=false;
        browser_text[n++]=c;
    }
    browser_text[n]=0;
    browser_loaded=true;
    browser_scroll=0;
    k_strcpy(browser_status,"TLS+hostname verified - bounded HTML text preview (may be partial)");
}
static void browser_load(void){
#ifdef FALCON_QEMU_BROWSER_TEST
    outb(0xE9,'b'); /* user pressed Enter inside the real GUI */
#endif
    browser_loaded=false;browser_result[0]=0;browser_text[0]=0;
    if(!net_present()){
        k_strcpy(browser_status,"No RTL8139 NIC. Set Virt-Manager network model to rtl8139.");
        return;
    }
    if(!browser_dhcp_attempted){
        browser_dhcp_attempted=true;
        (void)net_dhcp(); /* QEMU user NAT and libvirt virbr0 both offer DHCP */
    }
    const char *p=browser_address;
    const char *https="https://";
    for(i32 i=0;i<8;i++)if(p[i]!=https[i]){
        k_strcpy(browser_status,"HTTPS only. Prefix address with https://");
        return;
    }
    p+=8;
    char hostname[201],path[304];
    i32 k=0;
    while(p[k]&&p[k]!='/'&&p[k]!='?'&&p[k]!='#'){
        char c=p[k];
        if(k>=199||!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
                      (c>='0'&&c<='9')||c=='-'||c=='.')){
            k_strcpy(browser_status,"Invalid HTTPS hostname.");return;
        }
        hostname[k]=c;k++;
    }
    if(k==0){k_strcpy(browser_status,"Missing HTTPS hostname.");return;}
    hostname[k]=0;
    i32 n=0;
    if(p[k]=='/'||p[k]=='?'){
        if(p[k]=='?')path[n++]='/';
        while(p[k]&&p[k]!='#'&&n<300)path[n++]=p[k++];
    }else path[n++]='/';
    path[n]=0;
    k_strcpy(browser_status,"Connecting: DNS / TCP / TLS / CA certificate...");
    /* User-triggered and synchronous; slow networks can delay this UI.
     * Never execute a network operation while rendering each frame. */
    bool verified=false;
    if(browser_host_gateway){
        char local_path[480];
        u32 used=0;
        const char *prefix="/fetch/";
        for(u32 i=0;prefix[i];i++)local_path[used++]=prefix[i];
        for(u32 i=0;hostname[i]&&used<300u;i++)local_path[used++]=hostname[i];
        for(u32 i=0;path[i]&&used+1<sizeof local_path;i++)
            local_path[used++]=path[i];
        local_path[used]=0;
        verified=native_http_get_port(net_gateway(),18444u,local_path,
                                      browser_result,sizeof browser_result);
        if(!verified || !sh_contains_ci(browser_result,"X-Falcon-Host-HTTPS-Verified: yes")){
            if(k_strcmp(hostname,"falconos.tech")==0)
                k_strcpy(browser_status,
                   "Site may block CI (403). F8: GitHub docs, not live site.");
            else
                k_strcpy(browser_status,
                   "Host HTTPS failed; inspect gateway diagnostics.");
            return;
        }
    }else{
#ifdef FALCON_BEARSSL
        verified=native_https_get_preview(hostname,path,browser_result,sizeof browser_result);
        if(!verified){
            k_strcpy(browser_status,
                "Native TLS failed: check CA, DNS, network or response size.");
            return;
        }
#else
        k_strcpy(browser_status,
            "Native TLS not included in this build. Use TLS-enabled preview.");
        return;
#endif
    }
    browser_page_from_http();
#ifdef FALCON_QEMU_BROWSER_TEST
    outb(0xE9,browser_loaded?'Y':'N');
#endif
    if(browser_loaded && browser_host_gateway)
        k_strcpy(browser_status,
            "HOST-verified HTTPS | local VM link plaintext | read-only");

}
/* Native text-web view is shared by Falco and the Browser, not simulated.
 * F6 in Falco loads the requested site with the same certificate checks.
 * Only HTTPS URLs are allowed; no HTTP downgrade. */
static void falco_navigate(const char *address,bool use_host){
    const char *prefix="https://";
    const char *url=address;
    char normalized[224];
    if(k_strncmp(address,"http://",7)==0){
        k_strcpy(falco_status,"HTTPS only. HTTP navigation rejected.");
        return;
    }
    if(k_strncmp(address,prefix,8)!=0){
        k_strcpy(normalized,prefix);
        if(k_strlen(address)>206){
            k_strcpy(falco_status,"Address too long.");return;
        }
        k_strcat(normalized,address);url=normalized;
    }
    if(k_strlen(url)>=sizeof browser_address){
        k_strcpy(falco_status,"Address too long.");return;
    }
    k_strcpy(browser_address,url);
    browser_address_len=k_strlen(browser_address);
    browser_address_focus=true;
    browser_host_gateway=use_host; /* F7: explicit local host-verified HTTPS. */
    browser_load();
    /* Source authenticity also means verifying the requested site's content.
     * A TLS-authenticated error page, access-denied screen, or GitHub README
     * must not be reported as the FalconOS landing page. No fabricated HTML.
     * This only applies to the explicit falconos.tech destination. */
    bool requested_falconos_site =
        k_strcmp(browser_address,"https://falconos.tech/")==0 ||
        k_strcmp(browser_address,"https://falconos.tech")==0;
    if(requested_falconos_site && browser_loaded){
        if(!sh_contains_ci(browser_text,"FalconOS") ||
           !sh_contains_ci(browser_text,"Contour")){
            browser_loaded=false;
            k_strcpy(browser_status,
               "FalconOS site content not confirmed; refusing false success");
        }else{
            k_strcpy(browser_status,use_host?
                "REAL falconos.tech Contour | host CA-verified HTTPS":
                "REAL falconos.tech Contour | guest BearSSL HTTPS");
#ifdef FALCON_QEMU_BROWSER_TEST
            outb(0xE9,'f');outb(0xE9,'S');
#endif
        }
    }
    if(use_host && browser_loaded &&
       sh_contains_ci(browser_address,"raw.githubusercontent.com/"))
        k_strcpy(browser_status,
                "GitHub README fallback, not live falconos.tech; host HTTPS");
    falco_web_view=true;
#ifdef FALCON_QEMU_BROWSER_TEST
    /* A unique Falco navigation result: 'fY' means an actually loaded
     * verified response, 'fN' means an error. Browser 'bY' alone is
     * not enough to attest that this site was loaded. */
    outb(0xE9,'f');outb(0xE9,browser_loaded?'Y':'N');
#endif
}
static void falco_open_site(const char *address){
#ifdef FALCON_BEARSSL
    falco_navigate(address,false);
#else
    /* No pretend native TLS: host gateway performs verified HTTPS. */
    falco_navigate(address,true);
#endif
}
static void falco_open_site_host(const char *address){falco_navigate(address,true);}
static void chrome_input_key(i32 key){
    if((kbd_mod_state() & (1u<<1)) && (key=='l'||key=='L')) {
        browser_address_focus=true;return;
    }
    if((kbd_mod_state() & (1u<<1)) && (key=='r'||key=='R')) {
        browser_load();return;
    }
    if(key==KEY_ESC){browser_address_focus=false;return;}
    if(key==KEY_F4){
        browser_address[0]=0;
        browser_address_len=0;
        browser_address_focus=true;
        k_strcpy(browser_status,"Address cleared. Type https://... and press Enter.");
        return;
    }
    if(key==KEY_F6){
        browser_host_gateway=!browser_host_gateway;
        browser_loaded=false;browser_result[0]=0;
        k_strcpy(browser_status,browser_host_gateway?
            "HOST HTTPS enabled (no guest TLS). Start host gateway first.":
            "NATIVE HTTPS mode - BearSSL checks CA and hostname.");
        return;
    }
    if(key==KEY_UP){if(browser_scroll>0)browser_scroll--;return;}
    if(key==KEY_DOWN){if(browser_scroll<190)browser_scroll++;return;}
    if(key==KEY_TAB){browser_address_focus=!browser_address_focus;return;}
    if(key==KEY_F5){browser_load();return;}
    if(key==KEY_ENTER){browser_load();browser_address_focus=false;return;}
    if(!browser_address_focus)return;
    if(key==KEY_BACKSPACE){
        sh_buf_pop_utf8(browser_address,&browser_address_len);return;
    }
    if(browser_address_len>=220)return;
    (void)sh_buf_append_key(browser_address,&browser_address_len,
                            sizeof browser_address,key);
}
static void render_browser(i32 wx,i32 wy,i32 ww,i32 wh,u32 frame){
    (void)frame;
    i32 margin=18,bar=wy+54;
    i32 mx,my;bool held;mouse_get(&mx,&my,&held);(void)held;
    if(wm_click_enabled()&&mx>=wx+margin&&mx<wx+ww-margin){
        if(my>=wy+7&&my<wy+41){
            (void)mouse_consume_click();
            if(mx<wx+100){
                if(active_app==13)falco_web_view=false;
                else {browser_loaded=false;browser_address_focus=true;}
            }else if(mx<wx+196)browser_load();
            else if(mx>=wx+ww-160){
                browser_host_gateway=!browser_host_gateway;
                k_strcpy(browser_status,browser_host_gateway?
                    "HOST TLS via companion bridge enabled":
                    "NATIVE TLS (BearSSL) enabled");
            }else browser_address_focus=true;
            return;
        }
        if(my>=bar&&my<bar+34){
            (void)mouse_consume_click();
            browser_address_focus=true;
            return;
        }
    }
    gfx_rect(wx,wy,ww,44,PAL_PANEL_DEEP);
    gfx_round_rect_a(wx+margin,wy+7,80,32,9,PAL_PANEL,255);
    gfx_text(wx+margin+10,wy+15,"< Back",PAL_TEXT);
    gfx_round_rect_a(wx+104,wy+7,88,32,9,PAL_PANEL,255);
    gfx_text(wx+114,wy+15,"Reload",PAL_TEXT);
    gfx_text(wx+211,wy+16,active_app==13?"Falco Web":"Falcon Browser",PAL_ACCENT);
    gfx_round_rect_a(wx+ww-160,wy+7,141,32,9,PAL_PANEL,255);
    gfx_text(wx+ww-150,wy+15,
        browser_host_gateway?"HOST HTTPS":"NATIVE HTTPS",PAL_TEXT_DIM);
    gfx_rect(wx,wy+45,ww,56,PAL_PANEL_HI);
    i32 sx=wx+margin,sy=bar,sw=ww-margin*2;
    gfx_round_rect_a(sx,sy,sw,34,10,PAL_PANEL,255);
    gfx_round_outline(sx,sy,sw,34,10,
                      browser_address_focus?PAL_ACCENT:PAL_HAIRLINE);
    gfx_circle(sx+15,sy+17,5,COL_OK);
    gfx_text(sx+27,sy+11,browser_address,PAL_TEXT);
    gfx_rect(wx,wy+102,ww,34,PAL_PANEL_DEEP);
    gfx_text(wx+margin,wy+113,browser_status,
             browser_loaded?COL_OK:PAL_TEXT_DIM);
    i32 py=wy+146;
    i32 height=wh-184;
    if(height<70)height=70;
    gfx_round_rect_a(sx,py,sw,height,10,PAL_PANEL,255);
    gfx_round_outline(sx,py,sw,height,10,PAL_HAIRLINE);
    if(!browser_loaded){
        gfx_text_lg(sx+20,py+26,"Falcon Browser",PAL_TEXT);
        gfx_text(sx+20,py+76,"Open a real HTTPS page using Enter.",PAL_TEXT);
        gfx_text(sx+20,py+102,
            "Certificates validated by BearSSL, no HTTP fallback.",PAL_TEXT_DIM);
        gfx_text(sx+20,py+128,
            "Supports limited HTML-to-text, not CSS/JS or full websites.",PAL_TEXT_DIM);
    }else{
        i32 chars=(sw-40)/8;
        if(chars<15)chars=15;
        if(chars>120)chars=120;
        i32 rows=(height-24)/19;
        if(rows>60)rows=60;
        char line[128];
        const char *p=browser_text;
        i32 line_no=0,row=0,col=0;
        for(i32 i=0;p[i]&&row<rows;i++){
            char c=p[i];
            if(c=='\n'||col>=chars){
                line[col]=0;
                if(line_no>=browser_scroll){
                    gfx_text(sx+18,py+14+row*19,line,PAL_TEXT);
                    row++;
                }
                col=0;line_no++;
                if(c=='\n')continue;
            }
            if(col<126)line[col++]=c;
        }
        if(row<rows&&col&&line_no>=browser_scroll){
            line[col]=0;gfx_text(sx+18,py+14+row*19,line,PAL_TEXT);
        }
    }
    gfx_text(wx+margin,wy+wh-26,
        "Ctrl+L URL | Enter load | F5 reload | Esc exit URL | Up/Down scroll",
        PAL_TEXT_FAINT);
}

/* --- Video (software demo player) ---------------------------------------- */
static bool vid_play = true;
static u32  vid_epoch_ms = 0;
static u32  vid_freeze_ms = 0;
static i32  vid_clip = 0;   /* 0..2 */

static u32 vid_now_ms(void)
{
    if (vid_play) return pit_ms() - vid_epoch_ms;
    return vid_freeze_ms;
}

static void video_input_key(i32 key)
{
    if (key == ' ' || key == 'p' || key == 'P') {
        if (vid_play) {
            vid_freeze_ms = vid_now_ms();
            vid_play = false;
        } else {
            vid_epoch_ms = pit_ms() - vid_freeze_ms;
            vid_play = true;
        }
        return;
    }
    if (key == KEY_RIGHT) { vid_freeze_ms = vid_now_ms() + 1500; if (vid_play) vid_epoch_ms = pit_ms() - vid_freeze_ms; return; }
    if (key == KEY_LEFT)  {
        vid_freeze_ms = vid_now_ms();
        if (vid_freeze_ms > 1500) vid_freeze_ms -= 1500;
        else                      vid_freeze_ms = 0;
        if (vid_play) vid_epoch_ms = pit_ms() - vid_freeze_ms;
        return;
    }
    if (key == KEY_TAB || key == 'n' || key == 'N') {
        vid_clip = (vid_clip + 1) % 3;
    }
}

static void render_video(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame;
    section(wx, wy, T("Video", "Video"),
            T("Space play/pause  ← / → seek  Tab clip",
              "Boşluk duraklat  ← / → sar  Sekme klip"));

    i32 vx = wx + 24, vy = wy + 56, vw = ww - 48, vh = wh - 120;
    if (vh < 120) vh = 120;
    gfx_round_rect_a(vx, vy, vw, vh, 12, 0x0D1118, 255);
    gfx_round_outline(vx, vy, vw, vh, 12, PAL_HAIRLINE);

    u32 t = vid_now_ms();
    u32 sec = t / 1000;
    u32 ms = t % 1000;

    /* Procedural "video" clips rendered in real-time on the framebuffer. */
    if (vid_clip == 0) {
        for (i32 y = 8; y < vh - 8; y += 2) {
            u8 a = (u8)(30 + (y * 60) / vh);
            gfx_rect_a(vx + 8, vy + y, vw - 16, 2, PAL_ACCENT, a);
        }
        i32 bx = vx + 24 + (i32)(sec % (u32)(vw > 80 ? (vw - 80) : 1));
        gfx_round_rect_a(bx, vy + vh / 2 - 18, 56, 36, 8, 0xFFFFFF, 190);
    } else if (vid_clip == 1) {
        i32 cx = vx + vw / 2, cy = vy + vh / 2;
        for (i32 r = 18; r < (vh < vw ? vh : vw) / 2 - 8; r += 18) {
            u8 a = (u8)(140 - (r * 100) / (vh > 0 ? vh : 1));
            gfx_circle_a(cx, cy, r + (i32)(sec % 12), PAL_ACCENT, a);
        }
        gfx_circle(cx, cy, 14, 0xFFFFFF);
    } else {
        for (i32 x = vx + 10; x < vx + vw - 10; x += 18) {
            i32 h = 20 + (i32)((((u32)x + sec * 37u) % (u32)(vh - 34)));
            gfx_round_rect_a(x, vy + vh - h - 8, 10, h, 3, 0x34A853, 220);
        }
    }

    /* Transport row */
    i32 tx = vx + 12, ty = vy + vh + 10, tw = vw - 24;
    gfx_round_rect_a(tx, ty, tw, 28, 12, PAL_PANEL_DEEP, 255);
    gfx_round_outline(tx, ty, tw, 28, 12, PAL_HAIRLINE);
    i32 prog = (i32)(ms % (u32)(tw > 16 ? tw - 16 : 1));
    gfx_round_rect_a(tx + 8, ty + 10, prog, 8, 4, PAL_ACCENT, 255);
    gfx_text(tx + tw - 210, ty + 7, vid_play ? "playing" : "paused", vid_play ? COL_OK : COL_WARN);
    char ts[32], n[8];
    k_strcpy(ts, "t=");
    k_itoa(sec, n, 10); k_strcat(ts, n); k_strcat(ts, ".");
    if (ms < 100) k_strcat(ts, "0");
    if (ms < 10)  k_strcat(ts, "0");
    k_itoa(ms, n, 10); k_strcat(ts, n); k_strcat(ts, "s");
    gfx_text(tx + 12, ty + 7, ts, PAL_TEXT);
}

/* --- Heroic Launcher (Linux app bridge mock) ----------------------------- */
static i32 heroic_sel = 0;
static const char *HERO_GAMES[] = {
    "Hades", "Celeste", "Dead Cells", "Vampire Survivors", "Hollow Knight"
};

static void heroic_input_key(i32 key)
{
    i32 n = (i32)(sizeof(HERO_GAMES) / sizeof(HERO_GAMES[0]));
    if (key == KEY_UP && heroic_sel > 0) heroic_sel--;
    if (key == KEY_DOWN && heroic_sel < n - 1) heroic_sel++;
}

static void render_heroic(i32 wx, i32 wy, i32 ww, i32 wh, u32 frame)
{
    (void)frame;
    section(wx, wy,
            T("Heroic Launcher", "Heroic Başlatıcı"),
            T("Compatibility layer demo", "Uyumluluk katmanı (demo)"));
    i32 n = (i32)(sizeof(HERO_GAMES) / sizeof(HERO_GAMES[0]));
    i32 lx = wx + 24, ly = wy + 62, lw = ww - 48;
    for (i32 i = 0; i < n; i++) {
        i32 y = ly + i * 36;
        bool sel = (i == heroic_sel);
        gfx_round_rect_a(lx, y, lw, 30, 8, sel ? PAL_ACCENT_DIM : PAL_PANEL_DEEP, 255);
        gfx_round_outline(lx, y, lw, 30, 8, sel ? PAL_ACCENT : PAL_HAIRLINE);
        gfx_text(lx + 12, y + 8, HERO_GAMES[i], PAL_TEXT);
        gfx_text(lx + lw - 140, y + 8, "Epic/GOG bridge", PAL_TEXT_DIM);
    }
    gfx_text(wx + 24, wy + wh - 26,
             "Note: runtime bridge only, no native Linux ELF execution yet.",
             PAL_TEXT_FAINT);
}


/* ---- Marketplace: actual release catalogue + verified FAPP/1 script launch --- */
static i32 market_cursor;
static bool market_first_frame = true;
static bool market_publish_armed;
static i32 market_publish_armed_cursor;
/* Discover has an actual filtered view, while IDs remain stable for package
 * installation and the terminal/prg inventory. No placeholder catalog. */
#define MARKET_VIEW_CAP 48
static i32 market_view[MARKET_VIEW_CAP],market_view_count,market_category;
static char market_query[40];
static i32 market_query_len;
static bool market_search_mode;
static bool market_match(const char *haystack,const char *needle){
    if(!needle[0])return true;
    for(i32 i=0;haystack[i];i++){
        i32 j=0;
        while(needle[j]&&haystack[i+j]){
            char a=haystack[i+j],b=needle[j];
            if(a>='A'&&a<='Z')a+=32;
            if(b>='A'&&b<='Z')b+=32;
            if(a!=b)break;
            j++;
        }
        if(!needle[j])return true;
    }
    return false;
}
static void market_update_view(void){
    market_view_count=0;
    for(i32 i=0;i<market_count()&&market_view_count<MARKET_VIEW_CAP;i++){
        if(market_category==1&&!market_installed(i))continue;
        if(market_category==2&&!market_has_update(i))continue;
        if(!market_match(market_name(i),market_query))continue;
        market_view[market_view_count++]=i;
    }
    bool found=false;
    for(i32 j=0;j<market_view_count;j++)if(market_view[j]==market_cursor)found=true;
    if(!found)market_cursor=market_view_count?market_view[0]:-1;
}
static i32 market_selected_row(void){
    for(i32 j=0;j<market_view_count;j++)
        if(market_view[j]==market_cursor)return j;
    return 0;
}

static void market_launch(i32 i)
{
    const char *script = market_script(i);
    if (!script) return;
    outb(0xE9, 'R'); /* QEMU integration event: installed script launched */
    term_init();
    term_push("Marketplace: running verified FAPP/1 script");
    while (*script) {
        char line[184];
        i32 n = 0;
        while (script[n] && script[n] != '\n' && n < 182) n++;
        if (script[n] && script[n] != '\n') {
            term_push("Marketplace: invalid script line"); break;
        }
        for (i32 j = 0; j < n; j++) line[j] = script[j];
        line[n] = 0;
        if (n && line[0] != '#') {
            if (!market_line_allowed(line, n)) {
                term_push("Marketplace: unsafe command rejected");
                break;
            }
            sh_run_line(line);
        }
        script += n;
        if (*script == '\n') script++;
    }
    apps_open(5); /* real Terminal output, no arbitrary ELF execution */
}
static void market_input_key(i32 key)
{
    market_update_view();
    if(key==KEY_F11){
        market_publish_armed=true;
        market_publish_armed_cursor=-1; /* exported Desktop/code.app.pkg */
        return;
    }
    if(key=='p'||key=='P'){
        market_publish_armed=true;
        market_publish_armed_cursor=market_cursor;
        return;
    }
    if(key==KEY_F10&&market_publish_armed){
        market_publish_armed=false;
        if(market_publish_armed_cursor>=0)
            (void)market_publish_installed(market_publish_armed_cursor);
        else {
            shfs_ent_t *file=shfs_lookup("/home/falcon/Desktop/code.app.pkg");
            if(file&&!file->is_dir)(void)market_publish_package(file->data,file->len);
        }
        return;
    }
    if(key==KEY_ESC){market_publish_armed=false;return;}
    if(key==KEY_F4){market_search_mode=!market_search_mode;return;}
    if(key==KEY_F8){
        market_category=(market_category+1)%3;
        market_update_view();return;
    }
    if(key==KEY_F9){
        market_query_len=0;market_query[0]=0;market_category=0;
        market_update_view();return;
    }
    if(market_search_mode){
        if(key==KEY_BACKSPACE){
            if(market_query_len>0)market_query[--market_query_len]=0;
        }else{
            char bytes[4];i32 count=key_to_utf8(key,bytes);
            if(count>0&&market_query_len+count<(i32)sizeof market_query){
                for(i32 j=0;j<count;j++)market_query[market_query_len++]=bytes[j];
                market_query[market_query_len]=0;
            }
        }
        market_update_view();
#ifdef FALCON_QEMU_UI_GALLERY
        outb(0xE9,'q');outb(0xE9,(u8)('0'+market_view_count));
#endif
        return;
    }
    if(key=='r'||key=='R'||key==KEY_F5){
        market_refresh();market_update_view();return;
    }
    if(key=='c'||key=='C'){apps_open(18);return;}
    if(market_view_count==0)return;
    i32 row=market_selected_row();
    if(key==KEY_UP&&row>0)market_cursor=market_view[row-1];
    if(key==KEY_DOWN&&row+1<market_view_count)
        market_cursor=market_view[row+1];
    if(key=='d'||key=='D'){market_uninstall(market_cursor);return;}
    if(key=='u'||key=='U'){market_download(market_cursor);return;}
    if(key==KEY_ENTER){
        if(market_has_update(market_cursor))
            market_download(market_cursor);
        else if(market_installed(market_cursor))
            market_launch(market_cursor);
        else market_download(market_cursor);
    }
}
static void render_market(i32 wx,i32 wy,i32 ww,i32 wh,u32 frame)
{
    if(market_first_frame){
        outb(0xE9,'M');
        market_first_frame=false;
    }
    (void)frame;
    gfx_round_rect_a(wx+18,wy+8,ww-36,75,19,PAL_PANEL_DEEP,255);
    gfx_round_rect(wx+30,wy+19,40,40,13,0x20AA83u);
    gfx_text_lg_centered(wx+50,wy+26,"+",0xFFFFFFu);
    gfx_text_lg(wx+83,wy+14,T("Discover","Keşfet"),PAL_TEXT);
    gfx_text(wx+85,wy+49,T("Verified GitHub .app.pkg packages","GitHub doğrulamalı .app.pkg paketleri"),PAL_TEXT_DIM);
    gfx_text(wx+24,wy+91,"F5 refresh | F4 search | P selected | F11 add .pkg | F10 confirm",PAL_TEXT_DIM);
    gfx_text(wx+24,wy+111,
        market_publish_armed?
            (market_publish_armed_cursor<0?
              "Add Desktop/code.app.pkg to GitHub? F10 confirm / Esc cancel":
              "Publish installed app to GitHub? F10 confirm / Esc cancel"):
            market_status(),PAL_ACCENT);
    i32 mx,my;bool held;mouse_get(&mx,&my,&held);(void)held;
    bool clicked=wm_click_enabled();
    if(clicked&&mx>=wx+24&&mx<=wx+ww-24&&
       my>=wy+87&&my<=wy+107){
        market_refresh();(void)mouse_consume_click();clicked=false;
    }
    /* Search acts on real release titles and installed package names. */
    gfx_round_rect_a(wx+24,wy+138,ww-48,34,10,PAL_PANEL_DEEP,255);
    gfx_round_outline(wx+24,wy+138,ww-48,34,10,
                      market_search_mode?PAL_ACCENT:PAL_HAIRLINE);
    gfx_circle_outline(wx+41,wy+154,6,PAL_ACCENT);
    gfx_text(wx+57,wy+148,
        market_query_len?market_query:(market_search_mode?
            "Type app name...":"F4  Search apps"),
        market_query_len?PAL_TEXT:PAL_TEXT_DIM);
    /* Do not let the search hit-area steal category-filter clicks. */
    i32 cat_x=wx+ww-158;
    i32 add_x=cat_x-139;
    gfx_round_rect(add_x,wy+144,130,23,8,PAL_ACCENT_DIM);
    gfx_text(add_x+10,wy+149,"+ ADD .APP.PKG",PAL_ACCENT);
    if(clicked&&mx>=add_x&&mx<add_x+130&&my>=wy+144&&my<wy+167){
        market_publish_armed=true;
        market_publish_armed_cursor=-1;
        (void)mouse_consume_click();clicked=false;
    }
    if(clicked&&mx>=wx+24&&mx<add_x-4&&my>=wy+138&&my<wy+172){
        market_search_mode=true;(void)mouse_consume_click();clicked=false;
    }
    static const char *catname[]={"All apps","Installed","Updates"};
    gfx_round_rect(cat_x,wy+144,129,23,8,PAL_ACCENT_DIM);
    gfx_text(cat_x+9,wy+149,catname[market_category],PAL_ACCENT);
    if(clicked&&mx>=cat_x&&mx<cat_x+129&&my>=wy+144&&my<wy+167){
        market_category=(market_category+1)%3;
        (void)mouse_consume_click();clicked=false;
    }
    market_update_view();
    i32 count=market_view_count;
    i32 total=market_count();
    if(!total){
        gfx_round_rect_a(wx+24,wy+189,ww-48,90,14,PAL_PANEL_DEEP,255);
        gfx_text(wx+42,wy+225,
            T("No releases. Start make run-market and press F5.",
              "Katalog boş. make run-market başlat, ardından F5 bas."),
            PAL_TEXT_DIM);
    }else if(!count){
        gfx_round_rect_a(wx+24,wy+189,ww-48,90,14,PAL_PANEL_DEEP,255);
        gfx_text(wx+42,wy+225,"No matching releases or installed packages.",PAL_TEXT_DIM);
    }
    i32 visible=(wh-240)/39;
    if(visible<1)visible=1;
    i32 selected=market_selected_row();
    i32 first=selected-visible/2;
    if(first<0)first=0;
    if(first>count-visible)first=count-visible;
    if(first<0)first=0;
    for(i32 row=first;row<count&&row<first+visible;row++){
        i32 i=market_view[row];
        i32 y=wy+188+(row-first)*39;
        bool active=(i==market_cursor);
        gfx_round_rect_a(wx+24,y,ww-48,35,10,
            active?PAL_ACCENT_DIM:PAL_PANEL_DEEP,255);
        gfx_round_outline(wx+24,y,ww-48,35,10,
            active?PAL_ACCENT:PAL_HAIRLINE);
        gfx_circle(wx+41,y+18,7,active?PAL_ACCENT:COL_OK);
        gfx_text(wx+58,y+11,market_name(i),PAL_TEXT);
        gfx_text(wx+ww/2,y+11,market_version(i),PAL_TEXT_DIM);
        bool installed=market_installed(i);
        bool update=installed&&market_has_update(i);
        gfx_text(wx+ww-135,y+11,update?"UPDATE":(installed?"RUN":"INSTALL"),
            update?COL_WARN:(installed?COL_OK:PAL_ACCENT));
        if(clicked&&mx>=wx+24&&mx<wx+ww-24&&my>=y&&my<y+35){
            market_cursor=i;
            if(update)market_download(i);
            else if(installed)market_launch(i);
            else market_download(i);
            (void)mouse_consume_click();clicked=false;
        }
    }
    char nums[12];k_itoa(count,nums,10);
    gfx_text(wx+24,wy+wh-48,nums,PAL_ACCENT);
    gfx_text(wx+49,wy+wh-48,"visible releases",PAL_TEXT_DIM);
    gfx_text(wx+24,wy+wh-25,
        market_publish_status(),PAL_TEXT_FAINT);
}

/* ---- Codedium: native editable FAPP/1 source, file save and script preview --- */
#define CODE_CAP 4096
static char code_text[CODE_CAP];
static i32 code_len, code_cursor;
static bool code_ready,code_dirty,code_publish_armed;
static const char *code_status = "F5 Save  |  F6 Run  |  F7 Export .app.pkg";
static void code_init(void)
{
    if (code_ready) return;
    code_ready = true;
    shfs_init();
    shfs_ent_t *file = shfs_lookup("/home/falcon/Desktop/project.fsh");
    const char *sample = "# app-id: codedium-demo\n# app-name: Codedium Demo\n# app-version: 1.0.0\n# app-summary: Built inside FalconOS\nclear\necho Hello from Codedium\nuname\n";
    const char *source = (file && !file->is_dir) ? file->data : sample;
    i32 disklen = codedium_project_load(code_text, CODE_CAP);
    if (disklen > 0) source = code_text;
    i32 n = k_strlen(source);
    if (n >= CODE_CAP) n = CODE_CAP - 1;
    k_memcpy(code_text, source, n);
    code_text[n] = 0;
    code_cursor = code_len = n;
}
static void code_save(void)
{
    shfs_ent_t *file = shfs_open_w_abs("/home/falcon/Desktop/project.fsh", false);
    if (!file) { code_status = "Save error: RAM file system is full"; return; }
    k_memcpy(file->data, code_text, code_len + 1);
    file->len = code_len;
    code_dirty=false;
    code_status = codedium_project_save(code_text, code_len)
        ? "Saved project to RAM and safe FalconOS disk"
        : "Saved in RAM only (no safe disk selected)";
}
static void code_run(void)
{
    term_init();
    for (i32 at = 0; at < code_len;) {
        char line[184]; i32 n = 0;
        while (at + n < code_len && code_text[at + n] != '\n' && n < 182) n++;
        if (at + n < code_len && code_text[at + n] != '\n') {
            code_status = "Script line exceeds 182 chars"; return;
        }
        k_memcpy(line, code_text + at, n); line[n] = 0;
        if (n && line[0] != '#') {
            if (!market_line_allowed(line, n)) {
                code_status = "Unsafe command denied (FAPP/1 allowlist)"; return;
            }
            sh_run_line(line);
        }
        at += n;
        if (at < code_len && code_text[at] == '\n') at++;
    }
    code_status = "Executed in built-in Terminal";
    apps_open(5);
}
static bool code_export(void)
{
    static char pkg[SHFS_FBYTES];
    u32 bytes=0;
    if (!codedium_build_pkg(code_text,(u32)code_len,pkg,
                            sizeof pkg,&bytes)) {
        code_status = "Export failed: check # app-* metadata, commands or 4 KiB limit";
        return false;
    }
    shfs_ent_t *file = shfs_open_w_abs("/home/falcon/Desktop/code.app.pkg", false);
    if (!file) {
        code_status = "Export failed: guest RAM file system is full"; return false;
    }
    k_memcpy(file->data,pkg,bytes+1);
    file->len=bytes;
    code_status = "Created valid FAPP/1 code.app.pkg in guest Desktop";
#ifdef FALCON_QEMU_UI_GALLERY
    outb(0xE9,'E'); /* package actually built and written */
#endif
    return true;
}
static void code_input_key(i32 key)
{
    code_init();
    if (key == KEY_F5) { code_save(); return; }
    if (key == KEY_F6) { code_run(); return; }
    if (key == KEY_F7) { (void)code_export(); return; }
    if (key == KEY_F10) {
        if(!code_publish_armed){
            code_publish_armed=true;
            code_status="Publish to GitHub Releases? Press F10 again to confirm.";
        }else{
            code_publish_armed=false;
            if(code_export()){
                shfs_ent_t *file=shfs_lookup("/home/falcon/Desktop/code.app.pkg");
                if(file&&!file->is_dir&&market_publish_package(file->data,file->len))
                    code_status="Upload submitted to host bridge. See Discover status.";
                else code_status="Package upload rejected or bridge is busy.";
            }
        }
        return;
    }
    if (key == KEY_ESC && code_publish_armed) {
        code_publish_armed=false;
        code_status="GitHub publish cancelled.";return;
    }
    if ((kbd_mod_state() & KMOD_CTRL) && (key == 's' || key == 'S')) {
        code_save(); return;
    }
    if (key == KEY_UP || key == KEY_DOWN) {
        i32 start = code_cursor;
        while (start > 0 && code_text[start - 1] != '\n') start--;
        i32 column = code_cursor - start;
        if (key == KEY_UP && start > 0) {
            i32 previous_end = start - 1;
            i32 previous_start = previous_end;
            while (previous_start > 0 && code_text[previous_start - 1] != '\n')
                previous_start--;
            i32 width = previous_end - previous_start;
            code_cursor = previous_start + (column < width ? column : width);
        } else if (key == KEY_DOWN) {
            i32 end = code_cursor;
            while (end < code_len && code_text[end] != '\n') end++;
            if (end < code_len) {
                i32 next_start = end + 1, next_end = next_start;
                while (next_end < code_len && code_text[next_end] != '\n') next_end++;
                i32 width = next_end - next_start;
                code_cursor = next_start + (column < width ? column : width);
            }
        }
        return;
    }
    if (key == KEY_LEFT && code_cursor > 0) { code_cursor--; return; }
    if (key == KEY_RIGHT && code_cursor < code_len) { code_cursor++; return; }
    if (key == KEY_HOME) {
        while (code_cursor > 0 && code_text[code_cursor - 1] != '\n') code_cursor--;
        return;
    }
    if (key == KEY_END) {
        while (code_cursor < code_len && code_text[code_cursor] != '\n') code_cursor++;
        return;
    }
    if (key == KEY_BACKSPACE && code_cursor > 0) {
        for (i32 i = code_cursor - 1; i < code_len; i++) code_text[i] = code_text[i+1];
        code_len--; code_cursor--; code_dirty=true; return;
    }
    if (key == KEY_DEL && code_cursor < code_len) {
        for (i32 i = code_cursor; i < code_len; i++) code_text[i] = code_text[i+1];
        code_len--; code_dirty=true; return;
    }
    char c = 0;
    if (key == KEY_ENTER) c = '\n';
    else if (key == KEY_TAB) c = ' ';
    else if (key >= 32 && key < 127) c = (char)key;
    if (c && code_len + 1 < CODE_CAP) {
        for (i32 i = code_len; i >= code_cursor; i--) code_text[i+1] = code_text[i];
        code_text[code_cursor++] = c; code_len++; code_dirty=true;
    }
}
static void render_codedium(i32 wx,i32 wy,i32 ww,i32 wh,u32 frame)
{
    (void)frame;
    code_init();
    section(wx,wy,"Codedium Studio","FAPP/1 editor  |  sandboxed commands only");
    /* Toolbar performs real actions; not placeholder or static artwork. */
    const char *names[]={"Save  F5","Run  F6","Export  F7",
                         "GitHub F10"};
    const u32 tones[]={0x3478E6u,0x19A680u,0x8862D7u,0x167B54u};
    i32 mx,my;bool held;mouse_get(&mx,&my,&held);(void)held;
    bool clicked=wm_click_enabled();
    for(i32 i=0;i<4;i++){
        i32 x=wx+21+i*123, y=wy+47;
        bool hover=mx>=x&&mx<x+113&&my>=y&&my<y+34;
        gfx_round_rect_a(x,y,113,34,11,hover?PAL_ACCENT:tones[i],255);
        gfx_text_centered(x+56,y+11,names[i],0xFFFFFFu);
        if(clicked&&hover){
            (void)mouse_consume_click();clicked=false;
            if(i==0)code_save();
            else if(i==1)code_run();
            else if(i==2)(void)code_export();
            else code_input_key(KEY_F10);
        }
    }
    if(ww>=800){
        gfx_text(wx+ww-240,wy+58,"Desktop/project.fsh",PAL_TEXT_DIM);
        gfx_circle(wx+ww-24,wy+64,5,code_dirty?COL_WARN:COL_OK);
    }
    i32 top=wy+91;
    i32 area=wh-149;if(area<90)area=90;
    gfx_round_rect(wx+18,top,ww-36,area,10,0x101A2Cu);
    gfx_rect(wx+20,top+2,38,area-4,0x1B2942u);
    i32 line=0,cursor_line=0,cursor_col=0;
    for(i32 j=0;j<code_cursor;j++){
        if(code_text[j]=='\n'){cursor_line++;cursor_col=0;}
        else cursor_col++;
    }
    i32 visible=(area-13)/19;
    if(visible<1)visible=1;
    i32 first=cursor_line>=visible?cursor_line-visible+1:0;
    char buf[115];
    i32 bi=0;
    for(i32 pos=0;pos<=code_len;pos++){
        char ch=code_text[pos];
        i32 max_chars=(ww-102)/8;
        if(max_chars>=(i32)sizeof buf)max_chars=(i32)sizeof buf-1;
        if(ch!='\n'&&ch!=0&&bi<max_chars){
            buf[bi++]=ch;continue;
        }
        buf[bi]=0;
        if(line>=first&&line<first+visible){
            i32 y=top+9+(line-first)*19;
            if(line==cursor_line)
                gfx_rect_a(wx+59,y-2,ww-80,19,0x306ABBu,58);
            char num[12];k_itoa(line+1,num,10);
            gfx_text(wx+27,y,num,PAL_TEXT_FAINT);
            gfx_text(wx+66,y,buf,buf[0]=='#'?0x78B69Au:0xDDE7FFu);
            if(line==cursor_line){
                i32 x=wx+66+cursor_col*8;
                if(x<wx+ww-27)gfx_rect(x,y+15,8,2,PAL_ACCENT);
            }
        }
        bi=0;line++;
    }
    /* Bottom status follows the real cursor and unsaved-project state. */
    i32 footer=wy+wh-43;
    gfx_rect(wx+19,footer,ww-38,1,PAL_HAIRLINE);
    char line_text[16],col_text[16],bytes_text[16];
    k_itoa(cursor_line+1,line_text,10);
    k_itoa(cursor_col+1,col_text,10);
    k_itoa(code_len,bytes_text,10);
    gfx_text(wx+23,footer+9,code_status,PAL_TEXT_DIM);
    if(ww>=750){
        gfx_text(wx+ww-199,footer+9,"Ln",PAL_TEXT_FAINT);
        gfx_text(wx+ww-170,footer+9,line_text,PAL_TEXT);
        gfx_text(wx+ww-135,footer+9,"Col",PAL_TEXT_FAINT);
        gfx_text(wx+ww-98,footer+9,col_text,PAL_TEXT);
        gfx_text(wx+ww-61,footer+9,bytes_text,PAL_TEXT_FAINT);
        gfx_text(wx+ww-33,footer+9,"B",PAL_TEXT_FAINT);
    }
}

/* ===== app table & dispatch ============================================= */
typedef void (*app_render_fn)(i32 x, i32 y, i32 w, i32 h, u32 f);
typedef void (*app_input_fn)(i32 key);

typedef struct {
    const char     *name;
    const char     *subtitle;
    u32             tint;
    app_render_fn   render;
    app_input_fn    input;       /* may be NULL */
    void          (*draw_icon)(i32 cx, i32 cy);
} app_def_t;

static app_def_t APPS[] = {
    { "Home",       "quick links",         0x3070FF, render_home,     NULL,             icon_home     },
    { "Files",      "live guest RAM files", 0xF59F1A, render_files,    files_input_key,  icon_files    },
    { "Store",      "GitHub Releases apps",  0x2BB673, render_market,   market_input_key, icon_store    },
    { "Settings",   "system + theme",      0x6E7884, render_settings, set_input_key,    icon_settings },
    { "Sistem Güncellemeleri", "prg + FalconFS özeti", 0x05B897, render_updates,
      updates_input_key, icon_updates },
    { "Terminal",   "POSIX shell + prg",   0x14181F, render_term,     term_input_key,   icon_term     },
    { "Calculator", "+ - * /",             0xA45EE5, render_calc,     calc_input_key,   icon_calc     },
    { "Notes",      "free-form pad",       0xFFB547, render_notes,    notes_input_key,  icon_notes    },
    { "Clock",      "PIT analog dial",     0x16B5A8, render_clock,    NULL,             icon_clock    },
    { "Stats",      "system telemetry",    0xE53935, render_stats,    NULL,             icon_stats    },
    { "Calendar",   "month view",          0x3070FF, render_calendar, NULL,             icon_calendar },
    { "Gallery",    "palette swatches",    0xC084FC, render_gallery,  NULL,             icon_gallery  },
    { "Video",      "software player",     0x16B5A8, render_video,    video_input_key,  icon_video    },
    { "Falco",      "native web search",   0x2A66F5, render_falco,    falco_input_key,  icon_falco    },
    { "Browser",    "Native TLS 1.2 text web", 0x4285F4, render_browser, chrome_input_key, icon_browser },
    { "Heroic",     "linux game launcher", 0x6D5BFF, render_heroic,  heroic_input_key, icon_heroic   },
    { "Jarvis",     "AI assistant",        0x6D5BFF, jarvis_render,  jarvis_input,     jarvis_icon   },
    { "About",      "FalconOS 1",      0xA45EE5, render_about,    NULL,             icon_about    },
    { "Codedium",   "native app editor",   0x367DF8, render_codedium, code_input_key,   icon_term     },
};

static i32 builtin_app_count(void) { return (i32)(sizeof APPS / sizeof *APPS); }
/* ChromeOS-style launcher catalog: native functions only, stable original
 * app IDs for desktop pins, Store receipts and automation compatibility.
 * Demo video/palette gallery, Heroic (no Linux ABI) and Jarvis (not a
 * connected AI) are deliberately absent rather than mislabeled as working.
 * USB UVC camera is not yet implemented and is never faked.
 */
static const i32 LAUNCH_FAVORITES[] = {1,14,13,6,7,3,5,2,8};
static const i32 LAUNCH_SYSTEM[] = {3,9,4,17,18,5};
static const i32 LAUNCH_ALL[] = {1,14,13,6,7,3,5,2,8,9,4,17,18};
i32 apps_launcher_count(i32 group) {
    if(group==0) return (i32)(sizeof LAUNCH_FAVORITES/sizeof *LAUNCH_FAVORITES);
    if(group==1) return (i32)(sizeof LAUNCH_SYSTEM/sizeof *LAUNCH_SYSTEM);
    i32 installed=0;
    for(i32 i=0;i<market_count();i++)
        if(market_installed(i))installed++;
    return (i32)(sizeof LAUNCH_ALL/sizeof *LAUNCH_ALL)+installed;
}
i32 apps_launcher_id(i32 group,i32 index) {
    i32 count=apps_launcher_count(group);
    if(index<0||index>=count)return -1;
    if(group==0)return LAUNCH_FAVORITES[index];
    if(group==1)return LAUNCH_SYSTEM[index];
    i32 count_builtin=(i32)(sizeof LAUNCH_ALL/sizeof *LAUNCH_ALL);
    if(index<count_builtin)return LAUNCH_ALL[index];
    i32 slot=index-count_builtin;
    for(i32 i=0;i<market_count();i++){
        if(!market_installed(i))continue;
        if(slot--==0)return builtin_app_count()+i;
    }
    return -1;
}

i32 apps_count(void) { return builtin_app_count() + market_count(); }
const char *apps_name(i32 i) {
    if (i < 0 || i >= apps_count()) return "?";
    return i < builtin_app_count() ? APPS[i].name : market_name(i - builtin_app_count());
}
const char *apps_subtitle(i32 i) {
    if (i < 0 || i >= apps_count()) return "";
    return i < builtin_app_count() ? APPS[i].subtitle :
        (market_installed(i - builtin_app_count()) ? "Downloaded FAPP app" : "Download from Store");
}

const char *apps_display_name(i32 i)
{
    if (i < 0 || i >= apps_count()) return "?";
    if (i >= builtin_app_count()) return market_name(i - builtin_app_count());
    /* User-facing brand: Keşfet / Discover; stable internal ID remains Store. */
    if (i == 2) return T("Discover","Keşfet");
    if (SET.lang != LANG_TR)
        return APPS[i].name;
    switch (i) {
        case 0:  return "Ana Sayfa";
        case 1:  return "Dosyalar";
        case 2:  return "Mağaza";
        case 3:  return "Ayarlar";
        case 4:  return "Sistem Güncellemeleri";
        case 5:  return "Terminal";
        case 6:  return "Hesap Makinesi";
        case 7:  return "Notlar";
        case 8:  return "Saat";
        case 9:  return "İstatistik";
        case 10: return "Takvim";
        case 11: return "Galeri";
        case 12: return "Video";
        case 13: return "Falco";
        case 14: return "Tarayıcı";
        case 15: return "Heroic";
        case 16: return "Jarvis";
        case 17: return "Hakkında";
        default: return APPS[i].name;
    }
}

const char *apps_display_subtitle(i32 i)
{
    if (i < 0 || i >= apps_count()) return "";
    if (i >= builtin_app_count())
        return market_installed(i - builtin_app_count())
            ? T("Installed FAPP/1", "Yüklü FAPP/1") : T("Get app from Store", "Mağazadan indir");
    if (SET.lang != LANG_TR)
        return APPS[i].subtitle;
    switch (i) {
        case 0:  return "Hızlı bağlantılar";
        case 1:  return "Gerçek RAM dosyaları";
        case 2:  return "prg paket merkezi";
        case 3:  return "sistem + tema";
        case 4:  return "prg + FalconFS özeti";
        case 5:  return "POSIX kabuğu + prg";
        case 6:  return "+ − × ÷";
        case 7:  return "Serbest not";
        case 8:  return "Analog saat";
        case 9:  return "RAM / CPU / ekran";
        case 10: return "Ay görünümü";
        case 11: return "Renk paleti";
        case 12: return "Yazılım oynatıcı";
        case 13: return "Yerel indeks arama";
        case 14: return "Doğrulanmış HTTPS metin görünümü";
        case 15: return "Oyun başlatıcı (uyum)";
        case 16: return "Yapay asistan";
        case 17: return "FalconOS bilgisi";
        default: return APPS[i].subtitle;
    }
}

u32 apps_tint(i32 i) {
    if (i < 0 || i >= apps_count()) return 0x2BB673;
    return i < builtin_app_count() ? APPS[i].tint : 0x2BB673;
}
/* Material-symbol family: all first-party app icons share one 36px optical
 * grid, restrained rounded geometry and legible white vector strokes.
 * No borrowed Chromebook brand icon files or giant decorative fake apps.
 */
void apps_draw_icon(i32 i,i32 cx,i32 cy){
    if(i<0||i>=apps_count())return;
    const u32 white=0xFFFFFFu,shadow=0xECF3FFu;
    u32 col=apps_tint(i);
    if(i==5)col=0x1C304Eu;
    if(i==14)col=0x2865E8u;
    if(i==1)col=0xECA847u;
    gfx_round_rect(cx-18,cy-18,36,36,12,col);
    /* Inner letterforms are high-contrast and consistent at shelf size. */
    switch(i){
    case 1: /* Files */
        gfx_round_rect(cx-12,cy-9,13,5,2,white);
        gfx_round_rect(cx-12,cy-5,24,17,4,shadow);
        gfx_line(cx-9,cy+3,cx+9,cy+3,0xD59C42u);
        break;
    case 14: /* Browser: globe, not deceptive Chrome logo */
        gfx_circle_outline(cx,cy,12,white);
        gfx_line(cx-11,cy,cx+11,cy,white);
        gfx_line(cx,cy-12,cx,cy+12,white);
        gfx_circle_outline(cx,cy,5,shadow);
        break;
    case 13: /* Falco search */
        gfx_circle_outline(cx-2,cy-3,9,white);
        gfx_line(cx+5,cy+4,cx+13,cy+12,white);
        gfx_circle(cx-2,cy-3,3,0xCAE5FFu);
        break;
    case 6: /* Calculator */
        gfx_round_rect(cx-10,cy-13,20,26,4,white);
        gfx_rect(cx-7,cy-9,14,5,0x5A4BC4u);
        for(i32 y=0;y<2;y++)for(i32 x=0;x<3;x++)
            gfx_circle(cx-5+x*5,cy+3+y*5,2,0x8479E8u);
        break;
    case 7: /* Notes */
        gfx_round_rect(cx-10,cy-12,20,26,4,white);
        gfx_rect(cx-6,cy-6,12,2,0xBC9447u);
        gfx_rect(cx-6,cy,12,2,0xBC9447u);
        gfx_rect(cx-6,cy+6,8,2,0xBC9447u);
        break;
    case 3: /* Settings: slider controls */
        gfx_line(cx-11,cy-8,cx+11,cy-8,white);
        gfx_line(cx-11,cy,cx+11,cy,white);
        gfx_line(cx-11,cy+8,cx+11,cy+8,white);
        gfx_circle(cx+4,cy-8,3,0xBED5FFu);
        gfx_circle(cx-5,cy,3,0xBED5FFu);
        gfx_circle(cx+6,cy+8,3,0xBED5FFu);
        break;
    case 5: case 18: /* Terminal and code */
        gfx_round_rect(cx-13,cy-11,26,22,5,0x121D2Cu);
        gfx_text(cx-9,cy-6,i==18?"{}":">_",0xB2FCDEu);
        break;
    case 2: /* Store */
        gfx_round_rect(cx-11,cy-7,22,21,4,white);
        gfx_line(cx-7,cy-6,cx-7,cy-13,white);
        gfx_line(cx+7,cy-6,cx+7,cy-13,white);
        gfx_line(cx-7,cy-13,cx+7,cy-13,white);
        gfx_text_centered(cx,cy-1,"+",0x20A372u);
        break;
    case 8: /* Clock */
        gfx_circle_outline(cx,cy,12,white);
        gfx_line(cx,cy,cx,cy-8,white);
        gfx_line(cx,cy,cx+7,cy+4,white);
        break;
    case 9: /* Telemetry */
        gfx_line(cx-12,cy+11,cx+12,cy+11,white);
        gfx_rect(cx-10,cy,5,10,white);
        gfx_rect(cx-2,cy-8,5,18,white);
        gfx_rect(cx+6,cy-3,5,13,white);
        break;
    case 4: /* Update */
        gfx_circle_outline(cx,cy,12,white);
        gfx_line(cx,cy-9,cx,cy+6,white);
        gfx_line(cx-6,cy+2,cx,cy+8,white);
        gfx_line(cx+6,cy+2,cx,cy+8,white);
        break;
    case 17: /* Information */
        gfx_circle_outline(cx,cy,12,white);
        gfx_text_centered(cx,cy-6,"i",white);
        break;
    case 10: /* Calendar */
        gfx_round_rect(cx-12,cy-11,24,23,5,white);
        gfx_rect(cx-12,cy-8,24,6,0x5B84CDu);
        gfx_circle(cx-4,cy+5,3,0x5B84CDu);
        break;
    default:
        if(i>=builtin_app_count()){
            gfx_text_centered(cx,cy-7,"P",white);
        }else if(APPS[i].draw_icon)APPS[i].draw_icon(cx,cy);
    }
}

void apps_input_active(i32 key)
{
    if (active_app < 0) return;
    /* OS-level Alt+Tab rotates the existing real window stack; it does not
     * open a replacement application or destroy any document state. */
    if (key==KEY_TAB && (kbd_mod_state() & (1u<<2)) && wm_slot_count>1) {
        wm_raise(0);
        return;
    }
    /* Accessible window actions when PS/2 pointer grab is unavailable. */
    if ((kbd_mod_state() & (1u<<2)) && key==KEY_F4) {
        apps_close();return;
    }
    if ((kbd_mod_state() & (1u<<2)) && key==KEY_F9) {
        wm_minimize_top();return;
    }
    if ((kbd_mod_state() & (1u<<2)) && key==KEY_F10) {
        wm_max=!wm_max;return;
    }
    if (key == KEY_ESC) { apps_close(); return; }
    if (APPS[active_app].input) APPS[active_app].input(key);
}

/* Compute the active window's rect, taking the WM state into account.
 * Returns false when the rect is invalid (no active app).             */
static bool wm_window_rect(i32 *out_x, i32 *out_y, i32 *out_w, i32 *out_h)
{
    if (active_app < 0) return false;
    i32 W = (i32)FB.width, H = (i32)FB.height;
    if (wm_max) {
        *out_x=24;*out_y=48;
        *out_w=W-48;*out_h=H-164; /* floating Shelf remains visible */
        return true;
    }
    i32 ww = W - 280; if (ww > 1180) ww = 1180; if (ww < 600) ww = 600;
    i32 wh = H - 220; if (wh > 760) wh = 760; if (wh < 380) wh = 380;
    ww += wm_dw; wh += wm_dh;
    if (ww < 480) ww = 480;
    if (wh < 300) wh = 300;
    if (ww > W - 32) ww = W - 32;
    if (wh > H - 80) wh = H - 80;
    i32 wx = (W - ww) / 2 + wm_dx;
    i32 wy = (H - wh) / 2 - 10 + wm_dy;
    if (wx < 4)  wx = 4;
    if (wy < 32) wy = 32;
    if (wx + ww > W - 4) wx = W - 4 - ww;
    if (wy + wh > H - 4) wy = H - 4 - wh;
    *out_x = wx; *out_y = wy; *out_w = ww; *out_h = wh;
    return true;
}

/* Mouse-driven window manager. Called once per frame from main.c right
 * after mouse_get(). Handles title-bar drag, corner resize and traffic
 * lights. Returns true when it consumed the click (so the underlying
 * app shouldn't see it).                                              */
/* Focus lower windows only when the pointer is not occluded by a
 * window above them; clicks on background chrome go to the WM. */
static bool wm_focus_click(i32 mx,i32 my) {
    if(wm_slot_count<2)return false;
    wm_store_top();
    i32 selected=-1;
    for(i32 j=wm_slot_count-1;j>=0;j--) {
        const wm_slot_t *w=&wm_slots[j];
        if(w->minimized)continue;
        active_app=w->app;wm_dx=w->dx;wm_dy=w->dy;
        wm_dw=w->dw;wm_dh=w->dh;wm_max=w->maximized;
        i32 x,y,wid,hei;
        if(wm_window_rect(&x,&y,&wid,&hei) &&
           mx>=x&&mx<x+wid&&my>=y&&my<y+hei) {
            selected=j;break;
        }
    }
    wm_load_top();
    if(selected>=0&&selected<wm_slot_count-1) {
        wm_raise(selected);return true;
    }
    return false;
}

bool apps_wm_handle_mouse(i32 mx, i32 my, bool left_held, bool click_edge)
{
    if (active_app < 0) return false;

    i32 wx, wy, ww, wh;
    if (!wm_window_rect(&wx, &wy, &ww, &wh)) return false;

    /* Continue an in-flight gesture first.                              */
    if (wm_dragging) {
        if (!left_held) { wm_dragging = false; return true; }
        wm_dx += mx - wm_drag_grab_x;
        wm_dy += my - wm_drag_grab_y;
        wm_drag_grab_x = mx; wm_drag_grab_y = my;
        return true;
    }
    if (wm_resizing) {
        if (!left_held) { wm_resizing = false; return true; }
        wm_dw = wm_resize_start_w + (mx - wm_resize_grab_x);
        wm_dh = wm_resize_start_h + (my - wm_resize_grab_y);
        return true;
    }

    if (!click_edge) return false;
#ifdef FALCON_QEMU_UI_GALLERY
    /* Emit actual guest PS/2 pointer coordinates on rising-edge clicks.
     * This proves whether QEMU's synthetic click reached window manager. */
    outb(0xE9,'k');outb(0xE9,'P');
    char mx_s[16], my_s[16];
    k_itoa(mx,mx_s,10); k_itoa(my,my_s,10);
    for(i32 j=0;mx_s[j];j++)outb(0xE9,(u8)mx_s[j]);
    outb(0xE9,',');
    for(i32 j=0;my_s[j];j++)outb(0xE9,(u8)my_s[j]);
    outb(0xE9,';');
#endif
    /* Focus background windows on first click; the next click manipulates
     * their titlebar. The gesture may also start over any visible header. */
    if (wm_focus_click(mx,my)) return true;

    /* Match the *painted* titlebar hitboxes, including rounded corners:
     * paint uses y+7..y+37. Earlier y+5..y+35 left the bottom edge dead.
     * The full chrome row is available even on small-screen profiles. */
    if(my>=wy+5 && my<wy+42) {
        if(mx>=wx+ww-48 && mx<wx+ww-6) {
#ifdef FALCON_QEMU_UI_GALLERY
            outb(0xE9,'k');outb(0xE9,'X');
#endif
            apps_close();return true;
        }
        if(mx>=wx+ww-90 && mx<wx+ww-48) {
            wm_max=!wm_max;return true;
        }
        if(mx>=wx+ww-134 && mx<wx+ww-90) {
            wm_minimize_top();return true;
        }
    }

    /* Resize only the actual visible bottom-right grip. */
    if (!wm_max &&
        mx >= wx + ww - 25 && mx < wx + ww &&
        my >= wy + wh - 25 && my < wy + wh) {
        wm_resizing=true;
        wm_resize_grab_x=mx;wm_resize_grab_y=my;
        wm_resize_start_w=wm_dw;wm_resize_start_h=wm_dh;
        return true;
    }

    /* Drag from the complete titlebar, including its app name. Additional
     * Alt+left-drag anywhere inside the window allows recovery when the
     * titlebar is covered or the pointer cannot reach a tiny target. */
    bool alt_held=(kbd_mod_state() & (1u<<2))!=0;
    bool in_title=my>=wy && my<wy+43 &&
                  mx>=wx+48 && mx<wx+ww-134;
    bool alt_drag=alt_held && mx>=wx && mx<wx+ww &&
                              my>=wy && my<wy+wh;
    if ((in_title || alt_drag) && left_held && !wm_max) {
        wm_dragging=true;
        wm_drag_grab_x=mx;wm_drag_grab_y=my;
        return true;
    }

    return false;
}

/* Draw all native windows bottom-to-top. Hidden windows are not interactive:
 * wm_passive_paint suppresses app mouse click inspection until the topmost
 * focused window is rendered. */
static void wm_paint_window(u32 frame,bool focused) {
    if(active_app<0)return;
    const app_def_t *a=&APPS[active_app];
    i32 wx,wy,ww,wh;
    if(!wm_window_rect(&wx,&wy,&ww,&wh))return;
    /* Geometry must match the WM's hit-test on every frame. Animated
     * visual offsets previously made traffic lights impossible to click
     * during the first 200ms after focusing or opening a window. */
    gfx_round_rect_a(wx+4,wy+12,ww,wh,24,COL_SHADOW,70);
    gfx_round_rect_a(wx,wy,ww,wh,24,PAL_PANEL,SET.aero_enabled?246:255);
    gfx_round_outline(wx,wy,ww,wh,24,
                      focused?PAL_ACCENT:PAL_HAIRLINE);
    gfx_round_rect(wx+14,wy+8,28,28,10,a->tint);
    apps_draw_icon(active_app,wx+28,wy+22);
    gfx_text(wx+54,wy+13,apps_display_name(active_app),PAL_TEXT);
    if (focused) gfx_rect_a(wx+54,wy+34,98,2,PAL_ACCENT,220);
    /* 40px click targets correspond exactly to wm_handle_mouse() below. */
    gfx_round_rect_a(wx+ww-124,wy+7,38,30,10,PAL_PANEL_HI,255);
    gfx_text_centered(wx+ww-105,wy+14,"_",PAL_TEXT_DIM);
    gfx_round_rect_a(wx+ww-84,wy+7,38,30,10,PAL_PANEL_HI,255);
    gfx_text_centered(wx+ww-65,wy+13,wm_max?"o":"[]",PAL_TEXT_DIM);
    gfx_round_rect(wx+ww-44,wy+7,35,30,10,0xFBE6E8u);
    gfx_text_centered(wx+ww-26,wy+14,"x",0xAE3E4Bu);
    gfx_rect_a(wx+13,wy+42,ww-26,1,PAL_HAIRLINE,255);
    a->render(wx,wy+44,ww,wh-44,frame);
    if(!wm_max){
        i32 hx=wx+ww-14,hy=wy+wh-14;
        for(i32 j=0;j<3;j++)gfx_rect(hx-j*4,hy+j*4,3,3,PAL_TEXT_FAINT);
    }
    /* Controls remain in the title bar and Help drawer. Do not paint a
     * generic hint over app-specific footer text (Files/Browser/Codedium). */
}
void apps_render_active(u32 frame) {
    if(wm_slot_count<=0||active_app<0)return;
    wm_store_top();
    if(SET.aero_enabled && SET.theme==THEME_LIQUID)
        gfx_blur_rect(20,40,(i32)FB.width-40,(i32)FB.height-140,2);
    /* Keep background visible for spatial awareness and pointer targeting. */
    gfx_rect_a(0,0,FB.width,FB.height,COL_SHADOW,10);
    const i32 count=wm_slot_count;
    for(i32 j=0;j<count;j++) {
        const wm_slot_t *w=&wm_slots[j];
        if(w->minimized)continue;
        active_app=w->app;wm_dx=w->dx;wm_dy=w->dy;
        wm_dw=w->dw;wm_dh=w->dh;wm_max=w->maximized;
        wm_passive_paint=(j!=count-1);
        wm_paint_window(frame,!wm_passive_paint);
    }
    wm_passive_paint=false;
    wm_load_top();
}
