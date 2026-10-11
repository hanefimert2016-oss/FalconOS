/* =============================================================================
 *  FalconOS — desktop shortcut pinning  (v5)
 * -----------------------------------------------------------------------------
 *  Users can pin any app from Launchpad to the desktop with `P` while the
 *  Launchpad cursor is on the tile.  Pinned shortcuts render as small icon
 *  tiles down the left edge of the desktop, between the menu bar and the
 *  dock, click-to-launch.
 *
 *  State is held in a 16-slot bitmap keyed by app id.  Order on screen is
 *  insertion order (track via an array).
 *
 *  Hit-testing happens in input_click() and is consumed before the menu-bar
 *  / dock click handlers see the event.
 * ============================================================================= */
#include "falcon.h"
#include "shfs.h"

#define MAX_PINS 16

static i32 g_pin_ids[MAX_PINS];
static i32 g_pin_n = 0;

bool desktop_pin_is_pinned(i32 app_id)
{
    for (i32 i = 0; i < g_pin_n; i++) if (g_pin_ids[i] == app_id) return true;
    return false;
}

bool desktop_pin_toggle(i32 app_id)
{
    for (i32 i = 0; i < g_pin_n; i++) {
        if (g_pin_ids[i] == app_id) {
            for (i32 j = i + 1; j < g_pin_n; j++) g_pin_ids[j-1] = g_pin_ids[j];
            g_pin_n--;
            return false;
        }
    }
    if (g_pin_n < MAX_PINS) {
        g_pin_ids[g_pin_n++] = app_id;
        return true;
    }
    return false;
}

/* Files-generated shortcut tiles are NOT a hardcoded app list; each
 * .link is resolved only after user click through apps_launch_package_path.
 * No path in a link can directly invoke host processes or Linux/Windows ELF.
 */
#define MAX_FILE_SHORTCUTS 12
static char file_shortcuts[MAX_FILE_SHORTCUTS][SHFS_PATH];
static i32 file_shortcut_count;
static void collect_shortcut(const char *path,bool dir,u32 len,void *ud){
    (void)ud;
    if(dir||!len||file_shortcut_count>=MAX_FILE_SHORTCUTS)return;
    if(k_strncmp(path,"/home/falcon/Desktop/",21)!=0)return;
    u32 length=(u32)k_strlen(path);
    if(length<6||k_strcmp(path+length-5,".link")!=0)return;
    shfs_ent_t *file=shfs_lookup(path);
    if(!file||file->is_dir||k_strlen(file->data)>=SHFS_PATH ||
       k_strncmp(file->data,"/home/falcon/apps/",18)!=0)return;
    k_strcpy(file_shortcuts[file_shortcut_count++],path);
}
static void collect_all_shortcuts(void){
    file_shortcut_count=0;
    shfs_foreach_path(collect_shortcut,NULL);
}
/* --------------------------------------------------------------------------- */
/* Layout: column on the left, just below the menu bar.                        */
static void pin_geometry(i32 *x, i32 *y0, i32 *tile)
{
    *x    = 22;
    *y0   = 100;
    *tile = 78;
}

void desktop_pins_render(u32 frame)
{
    collect_all_shortcuts();
    if (g_pin_n == 0 && file_shortcut_count==0) return;

    i32 x, y0, tile;
    pin_geometry(&x, &y0, &tile);

    for (i32 i = 0; i < g_pin_n; i++) {
        i32 ty = y0 + i * (tile + 16);
        i32 cx = x + tile / 2;
        i32 cy = ty + tile / 2;

        /* glass tile */
        gfx_round_rect_a(x, ty, tile, tile, 16, PAL_PANEL, 230);
        gfx_round_outline(x, ty, tile, tile, 16, PAL_HAIRLINE);
        /* app icon (uses apps' draw_icon hook) */
        apps_draw_icon(g_pin_ids[i], cx, cy - 8);
        /* label */
        gfx_text_centered(cx, ty + tile - 18, apps_display_name(g_pin_ids[i]), PAL_TEXT);
    }
    /* Real shortcut file tiles beside the native pinned-app column. */
    for(i32 i=0;i<file_shortcut_count;i++){
        i32 tx=x+94,ty=y0+i*(tile+16);
        gfx_round_rect_a(tx,ty,tile,tile,16,PAL_PANEL,230);
        gfx_round_outline(tx,ty,tile,tile,16,PAL_HAIRLINE);
        gfx_round_rect(tx+25,ty+12,29,28,8,0x23A780u);
        gfx_text_centered(tx+39,ty+18,"A",0xFFFFFFu);
        const char *name=file_shortcuts[i]+21;
        char short_name[11];i32 n=0;
        while(name[n]&&name[n]!='.'&&n<10){short_name[n]=name[n];n++;}
        short_name[n]=0;
        gfx_text_centered(tx+tile/2,ty+tile-18,short_name,PAL_TEXT);
    }
}

bool desktop_pins_input_click(i32 mx, i32 my)
{
    collect_all_shortcuts();
    if (g_pin_n == 0 && file_shortcut_count == 0) return false;
    i32 x, y0, tile;
    pin_geometry(&x, &y0, &tile);
    if(mx>=x+94&&mx<x+94+tile){
        for(i32 i=0;i<file_shortcut_count;i++){
            i32 ty=y0+i*(tile+16);
            if(my>=ty&&my<ty+tile){
                apps_launch_package_path(file_shortcuts[i]);
                return true;
            }
        }
    }
    if (mx < x || mx > x + tile) return false;
    for (i32 i = 0; i < g_pin_n; i++) {
        i32 ty = y0 + i * (tile + 16);
        if (my >= ty && my < ty + tile) {
            apps_open(g_pin_ids[i]);
            return true;
        }
    }
    return false;
}
