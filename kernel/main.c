/* =============================================================================
 *  FalconOS — kernel entry & dispatcher  (FalconOS 1, x86_64 long mode)
 * -----------------------------------------------------------------------------
 *  Boot stub (boot/multiboot2.asm) flips the CPU into long mode and tail-calls
 *  `long_start` with the multiboot magic in EDI and the info-pointer in ESI
 *  (System-V AMD64).  We forward to `kernel_main`.
 *
 *  Boot sequence:
 *      1. parse Multiboot2 (framebuffer + memory map)
 *      2. install GDT/PIC/IDT/PIT, init mouse + Linux compat shim
 *      3. enable interrupts (sti)
 *      4. play boot splash (~700 ms, light/dark-aware fade)
 *      5. if !SET.installed → run installer wizard
 *      6. lock screen — accept SET.password to enter desktop
 *      7. main loop @ 100 Hz pacing two kernels + Launchpad + apps
 *
 *  Hot keys:
 *      F1   toggle Personal ↔ Developer kernel  (locked → ignored)
 *      F2   open / close Launchpad in Personal kernel
 *      Esc  close active app or Launchpad
 * ============================================================================= */
#include "falcon.h"

#define MB2_MAGIC_BOOT      0x36D76289u
#define MB2_TAG_END         0
#define MB2_TAG_FRAMEBUFFER 8

typedef struct __attribute__((packed)) {
    u32 type;
    u32 size;
} mb2_tag_t;

typedef struct __attribute__((packed)) {
    u32 type;
    u32 size;
    u64 addr;
    u32 pitch;
    u32 width;
    u32 height;
    u8  bpp;
    u8  fb_type;
    u16 reserved;
} mb2_fb_t;

volatile u32      g_tick = 0;
static falcon_mode_t g_mode = MODE_PERSONAL;

volatile bool g_panic = false;
char          g_panic_msg[80];

/* --------------------------------------------------------------------------- */
static void parse_multiboot(u64 magic, u64 info_ptr)
{
    if ((u32)magic != MB2_MAGIC_BOOT || !info_ptr) return;

    u8 *p = (u8 *)(uintptr_t)info_ptr;
    u32 total = *(u32 *)p;
    u8 *end = p + total;
    p += 8;

    while (p < end) {
        mb2_tag_t *t = (mb2_tag_t *)p;
        if (t->type == MB2_TAG_END) break;
        if (t->type == MB2_TAG_FRAMEBUFFER) {
            mb2_fb_t *f = (mb2_fb_t *)t;
            gfx_init((void *)(uintptr_t)f->addr, f->width, f->height, f->pitch, f->bpp);
        }
        p += (t->size + 7) & ~7u;
    }

    mmap_parse((uptr)info_ptr);
}

/* --------------------------------------------------------------------------- */
/* Top menu bar (full-width frosted strip with kernel name + clock).           */
static void draw_menu_bar(void)
{
    i32 W = (i32)FB.width;
    i32 H = 30;

    /* Aero glass strip across the top — blur the wallpaper underneath
     * so the menu bar feels lifted off the desktop.  When SET.aero is
     * off, fall back to the cheap flat overlay.                       */
    if (SET.aero_enabled) {
        if (SET.theme == THEME_LIQUID) {
            gfx_blur_rect(0, 0, W, H, 6);
            gfx_rect_a(0, 0, W, H, 0xE8F6FF, 105);
            gfx_rect_a(0, 0, W, 1, 0xFFFFFF, 180);
            gfx_rect_a(0, 1, W, 1, 0xCFF1FF, 120);
        } else {
            gfx_blur_rect(0, 0, W, H, 5);
            gfx_rect_a(0, 0, W, H, PAL_PANEL, 150);
        }
    } else {
        gfx_rect_a(0, 0, W, H, PAL_PANEL, 220);
    }
    gfx_rect_a(0, H, W, 1, PAL_HAIRLINE, 255);

    /* left: brand */
    gfx_circle(20, H / 2, 6, PAL_ACCENT);
    gfx_text(34, 7, "Falcon", PAL_TEXT);

    /* mode label + dot */
    const char *label;
    u32 accent;
    if (g_mode == MODE_PERSONAL) { label = "Personal"; accent = PAL_ACCENT; }
    else                         { label = "Developer"; accent = COL_OK; }

    i32 mx = 110;
    gfx_circle(mx, H / 2, 4, accent);
    gfx_text(mx + 12, 7, label, PAL_TEXT_DIM);

    /* hint pill (centered) */
    {
        const char *hint = T("F2 Launchpad    F12 Power    Esc closes",
                             "F2 Launchpad    F12 Guc    Esc kapatir");
        i32 hw = gfx_text_width(hint) + 28;
        i32 hx = (W - hw) / 2;
        gfx_round_rect_a(hx, 4, hw, H - 8, 11, PAL_PANEL_DEEP, 255);
        gfx_text(hx + 14, 7, hint, PAL_TEXT_DIM);
    }

    /* right: locale-formatted "DD <mon> HH:MM:SS"  +  lang badge      */
    rtc_time_t now; rtc_local(&now);
    char clk[24]; char tmp[8];
    k_strcpy(clk, "");
    /* day + localized month abbrev */
    k_itoa(now.day, tmp, 10);
    if (now.day < 10) k_strcat(clk, "0");
    k_strcat(clk, tmp);
    k_strcat(clk, " ");
    k_strcat(clk, loc_month_short(now.month));
    k_strcat(clk, "  ");
    /* HH:MM:SS */
    k_itoa(now.hour, tmp, 10); if (now.hour < 10) k_strcat(clk, "0"); k_strcat(clk, tmp);
    k_strcat(clk, ":");
    k_itoa(now.min,  tmp, 10); if (now.min  < 10) k_strcat(clk, "0"); k_strcat(clk, tmp);
    k_strcat(clk, ":");
    k_itoa(now.sec,  tmp, 10); if (now.sec  < 10) k_strcat(clk, "0"); k_strcat(clk, tmp);

    i32 cw = gfx_text_width(clk);
    gfx_text(W - cw - 18, 7, clk, PAL_TEXT);

    /* tiny ISO-639 lang badge (TR/EN/DE/FR/ES) */
    const char *codes[] = { "TR", "EN", "DE", "FR", "ES" };
    const char *lang = (SET.lang < LANG_COUNT) ? codes[SET.lang] : "EN";
    gfx_round_rect_a(W - cw - 56, 6, 28, 18, 9, PAL_PANEL_DEEP, 255);
    gfx_text(W - cw - 48, 8, lang, PAL_ACCENT);

    /* power glyph at the far right (click to open Power options).        */
    i32 px = W - cw - 88;
    i32 py = H / 2;
    gfx_circle_outline(px, py, 9, COL_ERR);
    gfx_circle_outline(px, py, 8, COL_ERR);
    gfx_rect(px - 1, py - 11, 3, 7, COL_ERR);
    gfx_rect(px - 1, py - 11, 3, 3, PAL_PANEL); /* knock out the top */

    /* "?" help glyph — sits 26 px left of the power glyph, opens the
     * sliding Help drawer.  Subtle outline so it doesn't compete with
     * the power button visually.                                     */
    i32 hpx = W - cw - 88 - 28;
    gfx_circle_outline(hpx, py, 9, PAL_TEXT_DIM);
    gfx_text_centered(hpx + 1, py - 7, "?", PAL_TEXT_DIM);
}

/* Hit-test for the power glyph in the menu bar; returns true if (mx,my) is
 * within the small clickable circle.                                       */
static bool menu_bar_power_hit(i32 mx, i32 my)
{
    if (my < 0 || my > 30) return false;
    i32 W = (i32)FB.width;
    /* Re-derive the same x used in draw_menu_bar — must stay in sync.    */
    rtc_time_t now; rtc_local(&now);
    char clk[24]; char tmp[8];
    k_strcpy(clk, "");
    k_itoa(now.day, tmp, 10);
    if (now.day < 10) k_strcat(clk, "0");
    k_strcat(clk, tmp);
    k_strcat(clk, " ");
    k_strcat(clk, loc_month_short(now.month));
    k_strcat(clk, "  ");
    k_itoa(now.hour, tmp, 10); if (now.hour < 10) k_strcat(clk, "0"); k_strcat(clk, tmp);
    k_strcat(clk, ":");
    k_itoa(now.min,  tmp, 10); if (now.min  < 10) k_strcat(clk, "0"); k_strcat(clk, tmp);
    k_strcat(clk, ":");
    k_itoa(now.sec,  tmp, 10); if (now.sec  < 10) k_strcat(clk, "0"); k_strcat(clk, tmp);
    i32 cw = gfx_text_width(clk);
    i32 px = W - cw - 88;
    i32 py = 15;
    i32 dx = mx - px, dy = my - py;
    return (dx * dx + dy * dy) <= 16 * 16;
}

/* Same shape, 28 px to the left — the menu-bar "?" help glyph.        */
static bool main_help_glyph_hit(i32 mx, i32 my)
{
    if (my < 0 || my > 30) return false;
    i32 W = (i32)FB.width;
    rtc_time_t now; rtc_local(&now);
    char clk[24]; char tmp[8];
    k_strcpy(clk, "");
    k_itoa(now.day, tmp, 10);
    if (now.day < 10) k_strcat(clk, "0");
    k_strcat(clk, tmp);
    k_strcat(clk, " ");
    k_strcat(clk, loc_month_short(now.month));
    k_strcat(clk, "  ");
    k_itoa(now.hour, tmp, 10); if (now.hour < 10) k_strcat(clk, "0"); k_strcat(clk, tmp);
    k_strcat(clk, ":");
    k_itoa(now.min,  tmp, 10); if (now.min  < 10) k_strcat(clk, "0"); k_strcat(clk, tmp);
    k_strcat(clk, ":");
    k_itoa(now.sec,  tmp, 10); if (now.sec  < 10) k_strcat(clk, "0"); k_strcat(clk, tmp);
    i32 cw = gfx_text_width(clk);
    i32 hpx = W - cw - 88 - 28;
    i32 hpy = 15;
    i32 dx = mx - hpx, dy = my - hpy;
    return (dx * dx + dy * dy) <= 14 * 14;
}

/* --------------------------------------------------------------------------- */
static void draw_cursor(void)
{
    i32 mx, my; bool ml; mouse_get(&mx, &my, &ml);
    /* drop shadow */
    gfx_circle_a(mx + 2, my + 3, 9, COL_SHADOW, 90);
    /* outer ring */
    gfx_circle(mx, my, 9, PAL_TEXT);
    /* inner — change color on click */
    gfx_circle(mx, my, 5, ml ? PAL_ACCENT : PAL_PANEL);
}

static void draw_blue_dragon(i32 cx, i32 cy, u8 alpha)
{
    /* Professional blue dragon emblem - detailed and realistic */
    u32 deep_blue  = 0x0A1628;
    u32 body_blue  = 0x1E4A8C;
    u32 mid_blue   = 0x2A66F5;
    u32 bright_blue= 0x4A88FF;
    u32 glow_blue  = 0x5588FF;
    u32 ice_blue   = 0xBDE2FF;
    u32 white      = 0xFFFFFF;
    u32 gold       = 0xFFD700;
    u32 dark       = 0x050A14;

    /* Outer glow layers - atmosphere around dragon */
    gfx_circle_a(cx, cy, 90, dark, (u8)(alpha / 4));
    gfx_circle_a(cx, cy, 80, deep_blue, (u8)(alpha / 3));
    gfx_circle_a(cx, cy, 70, body_blue, (u8)(alpha / 2));

    /* Main body - sinuous serpentine body */
    gfx_circle_a(cx - 25, cy + 35, 28, body_blue, alpha);
    gfx_circle_a(cx - 15, cy + 25, 30, body_blue, alpha);
    gfx_circle_a(cx - 5, cy + 15, 32, mid_blue, alpha);
    gfx_circle_a(cx + 5, cy + 5, 34, mid_blue, alpha);
    gfx_circle_a(cx + 15, cy - 5, 32, mid_blue, alpha);
    gfx_circle_a(cx + 20, cy - 18, 28, bright_blue, alpha);

    /* Body scales pattern - subtle scale effect */
    gfx_circle_a(cx - 20, cy + 30, 12, body_blue, (u8)(alpha * 3 / 4));
    gfx_circle_a(cx - 10, cy + 20, 14, body_blue, (u8)(alpha * 3 / 4));
    gfx_circle_a(cx, cy + 10, 16, mid_blue, (u8)(alpha * 3 / 4));
    gfx_circle_a(cx + 10, cy, 14, mid_blue, (u8)(alpha * 3 / 4));

    /* Dragon neck and head - elegant curve */
    gfx_circle_a(cx + 25, cy - 25, 22, bright_blue, alpha);
    gfx_circle_a(cx + 30, cy - 32, 18, bright_blue, alpha);
    gfx_circle_a(cx + 35, cy - 38, 14, mid_blue, alpha);

    /* Dragon snout */
    gfx_circle_a(cx + 42, cy - 40, 10, body_blue, alpha);
    gfx_circle_a(cx + 48, cy - 42, 6, body_blue, alpha);

    /* Dragon eye socket */
    gfx_circle_a(cx + 32, cy - 36, 8, dark, alpha);
    /* Glowing eye */
    gfx_circle_a(cx + 33, cy - 37, 5, gold, alpha);
    gfx_circle_a(cx + 33, cy - 37, 3, white, alpha);
    gfx_circle_a(cx + 34, cy - 38, 1, white, alpha);

    /* Dragon horns - sharp and angular */
    gfx_line(cx + 28, cy - 42, cx + 18, cy - 60, ice_blue);
    gfx_line(cx + 30, cy - 44, cx + 22, cy - 62, ice_blue);
    gfx_line(cx + 32, cy - 45, cx + 26, cy - 64, white);
    /* Horn tips - glowing */
    gfx_circle_a(cx + 18, cy - 60, 3, white, alpha);
    gfx_circle_a(cx + 22, cy - 62, 3, white, alpha);
    gfx_circle_a(cx + 26, cy - 64, 2, white, alpha);

    /* Dragon ears/frills */
    gfx_line(cx + 24, cy - 35, cx + 14, cy - 48, bright_blue);
    gfx_line(cx + 22, cy - 34, cx + 12, cy - 46, bright_blue);
    gfx_circle_a(cx + 14, cy - 48, 4, ice_blue, alpha);
    gfx_circle_a(cx + 12, cy - 46, 3, ice_blue, alpha);

    /* Dragon wings - large and impressive */
    /* Left wing (viewer's left, dragon's right) */
    gfx_line(cx + 10, cy - 5, cx - 15, cy - 35, glow_blue);
    gfx_line(cx + 8, cy - 3, cx - 20, cy - 38, glow_blue);
    gfx_line(cx + 5, cy, cx - 28, cy - 40, bright_blue);
    gfx_line(cx + 2, cy + 2, cx - 35, cy - 38, bright_blue);
    gfx_line(cx - 2, cy + 4, cx - 42, cy - 32, mid_blue);
    /* Wing membrane details */
    gfx_circle_a(cx - 15, cy - 35, 8, bright_blue, (u8)(alpha * 2 / 3));
    gfx_circle_a(cx - 28, cy - 40, 6, bright_blue, (u8)(alpha * 2 / 3));
    gfx_circle_a(cx - 40, cy - 35, 5, mid_blue, (u8)(alpha * 2 / 3));
    /* Wing tip feathers */
    gfx_circle_a(cx - 15, cy - 35, 4, ice_blue, alpha);
    gfx_circle_a(cx - 28, cy - 40, 3, ice_blue, alpha);
    gfx_circle_a(cx - 40, cy - 35, 2, ice_blue, alpha);

    /* Dragon tail - long and flowing */
    gfx_circle_a(cx - 35, cy + 45, 20, body_blue, alpha);
    gfx_circle_a(cx - 45, cy + 55, 14, body_blue, alpha);
    gfx_circle_a(cx - 55, cy + 62, 10, deep_blue, alpha);
    gfx_circle_a(cx - 62, cy + 68, 6, deep_blue, alpha);
    gfx_circle_a(cx - 68, cy + 72, 3, mid_blue, alpha);
    /* Tail tuft */
    gfx_line(cx - 68, cy + 72, cx - 75, cy + 78, bright_blue);
    gfx_line(cx - 68, cy + 72, cx - 78, cy + 75, bright_blue);
    gfx_line(cx - 68, cy + 72, cx - 76, cy + 80, ice_blue);

    /* Dragon claws - sharp and dangerous */
    gfx_line(cx + 20, cy + 15, cx + 25, cy + 35, body_blue);
    gfx_line(cx + 18, cy + 18, cx + 22, cy + 38, body_blue);
    gfx_line(cx + 22, cy + 20, cx + 30, cy + 36, body_blue);
    gfx_line(cx + 25, cy + 22, cx + 35, cy + 34, body_blue);
    /* Claw tips */
    gfx_circle_a(cx + 25, cy + 35, 2, white, alpha);
    gfx_circle_a(cx + 22, cy + 38, 2, white, alpha);
    gfx_circle_a(cx + 30, cy + 36, 2, white, alpha);
    gfx_circle_a(cx + 35, cy + 34, 2, white, alpha);

    /* Belly scales - lighter underbelly */
    gfx_circle_a(cx - 5, cy + 18, 10, ice_blue, (u8)(alpha / 2));
    gfx_circle_a(cx + 5, cy + 8, 12, ice_blue, (u8)(alpha / 2));
    gfx_circle_a(cx + 15, cy - 2, 10, ice_blue, (u8)(alpha / 2));

    /* Nostril smoke/breath effect */
    gfx_circle_a(cx + 50, cy - 41, 3, (u8)(alpha / 2), (u8)(alpha / 2));
    gfx_circle_a(cx + 52, cy - 40, 2, (u8)(alpha / 3), (u8)(alpha / 3));

    /* Final glow ring */
    gfx_circle_outline(cx, cy, 65, glow_blue);
    gfx_circle_outline(cx, cy, 68, (u8)(alpha / 2));
}

/* --------------------------------------------------------------------------- */
static void boot_splash(void)
{
    /* run for ~200 ticks (2000 ms at 100 Hz) — show logo while loading */
    u32 start = g_ticks;
    while (g_ticks - start < 200) {
        u32 dt = g_ticks - start;

        /* Dark blue gradient background for dragon theme */
        gfx_gradient_v(0x0A1628, 0x152238);

        i32 cx = (i32)FB.width  / 2;
        i32 cy = (i32)FB.height / 2 - 30;

        u8  alpha = dt < 15 ? (u8)(dt * 17) : 255;
        i32 r     = 70 + (i32)(dt / 2);

        /* Outer glow rings */
        gfx_circle_a(cx, cy, r + 20, 0x1A3A6A, (u8)(alpha / 3));
        gfx_circle_a(cx, cy, r + 10, 0x2A5A8A, (u8)(alpha / 2));

        /* Main circle with gradient effect */
        gfx_circle_a(cx, cy, r,      0x2A66F5, alpha);
        gfx_circle_a(cx, cy, r - 16, 0x0A1628, alpha);
        gfx_circle_a(cx, cy, r - 32, 0x2A66F5, (u8)(alpha * 3 / 4));
        gfx_circle_a(cx, cy, r - 48, 0x0A1628, alpha);

        /* Draw enhanced dragon */
        draw_blue_dragon(cx, cy, alpha);

        /* FalconOS 1 text - large and prominent */
        gfx_text_lg(cx - 80, cy + r + 30, "FalconOS", 0xFFFFFF);
        gfx_text_lg(cx + 56, cy + r + 30, "1", 0x5588FF);

        /* Subtitle with version */
        gfx_text_centered(cx, cy + r + 70,
            T("Blue Dragon Edition", "Mavi Ejderha Surumu"), 0xBDE2FF);

        /* Starting message */
        gfx_text_centered(cx, cy + r + 92,
            T("Starting kernel...", "Cekirdek baslatiliyor..."), 0x6688AA);

        /* Architecture badge */
        gfx_text_centered(cx, cy + r + 114, "x86_64 Long Mode | 64-bit", 0x446688);

        /* Progress bar effect */
        i32 bar_w = 200;
        i32 bar_h = 4;
        i32 bar_x = cx - bar_w / 2;
        i32 bar_y = cy + r + 135;
        i32 progress = (i32)(dt * bar_w / 200);
        gfx_rect(bar_x, bar_y, bar_w, bar_h, 0x1A3A6A);
        gfx_rect(bar_x, bar_y, progress, bar_h, 0x2A66F5);

        gfx_present();
        __asm__ volatile ("hlt");
    }
}

/* --------------------------------------------------------------------------- */
static void render_panic_overlay(void)
{
    gfx_dim(80);
    i32 w = 540, h = 120;
    i32 x = ((i32)FB.width - w) / 2;
    i32 y = ((i32)FB.height - h) / 2;
    gfx_round_glass(x, y, w, h, 18);
    gfx_round_outline(x, y, w, h, 18, COL_ERR);
    gfx_circle(x + 28, y + h / 2, 12, COL_ERR);
    gfx_text(x + 56, y + 24, "kernel halted", COL_ERR);
    gfx_text(x + 56, y + 48, g_panic_msg,     PAL_TEXT);
    gfx_text(x + 56, y + 80, "reset to recover",  PAL_TEXT_DIM);
}

/* --------------------------------------------------------------------------- */
/* Spin a pre-desktop modal (installer or lockscreen). The provided render
 * + input callbacks are called every frame; the predicate decides when to
 * exit the loop.                                                              */
typedef bool (*pred_fn)(void);
typedef void (*render_fn)(u32);
typedef void (*key_fn)(i32);

static void modal_loop(pred_fn done, render_fn ren, key_fn ki)
{
    u32 frame = 0;
    u32 last  = g_ticks;
    while (!done()) {
        if (g_panic) { render_panic_overlay(); gfx_present();
                       for (;;) __asm__ volatile ("hlt"); }

        i32 k;
        while ((k = kbd_poll()) != -1) ki(k);

        gfx_wallpaper();
        ren(frame);
        draw_cursor();
        gfx_present();
        frame++;

        while (g_ticks - last < 2) __asm__ volatile ("hlt");
        last = g_ticks;
    }
}

/* --------------------------------------------------------------------------- */
/* Long-mode entry point — called from boot/multiboot2.asm with arguments
 * already in RDI / RSI per System-V.  We promote to u64 for clarity.        */
void long_start(u64 magic, u64 info_ptr)
{
    parse_multiboot(magic, info_ptr);

    if (!FB.pixels) for (;;) __asm__ volatile ("hlt");

    /* set up real interrupts so we get a 100 Hz clock + async I/O          */
    gdt_install();
    pic_remap();
    idt_install();
    pit_init();
    mouse_init();
    /* Probe ATA controller BEFORE settings_init so its diskdb_load() can
     * see attached disks and try to restore SET from LBA0 superblock.     */
    linux_compat_init();
    settings_init();
    apps_pkg_sync_receipts_from_state();   /* mirror prg install flags → shfs     */

    pic_unmask(0);   /* PIT      */
    pic_unmask(1);   /* keyboard */
    pic_unmask(2);   /* cascade  */
    pic_unmask(12);  /* mouse    */
    __asm__ volatile ("sti");

    boot_splash();

    /* installer: only on the very first boot                                */
    if (!SET.installed) {
        kbd_drain(); mouse_drain();
        modal_loop(installer_is_done, installer_render, installer_input);
    }

    /* Drain any keys/clicks queued during the installer so the lockscreen
     * does not see a stale Enter from the final wizard step.            */
    kbd_drain(); mouse_drain();

    /* lock screen — must enter the password to reach the desktop           */
    modal_loop(lockscreen_is_unlocked, lockscreen_render, lockscreen_input);

    /* Same for the lockscreen → desktop handoff.                          */
    kbd_drain(); mouse_drain();

    /* First time the user reaches the desktop, slide the Help drawer
     * open automatically so they discover the F1/F2/F12 shortcuts and
     * mouse / window gestures immediately.  diskdb_save() in
     * helppanel_handle_*() flips SET.help_seen so it stays dismissed.  */
    if (!SET.help_seen) helppanel_open();

    /* main loop: paced to PIT — wait for at least 1 tick before next frame */
    u32 last = g_ticks;
    for (;;) {
        if (g_panic) { render_panic_overlay(); gfx_present();
                       for (;;) __asm__ volatile ("hlt"); }

        /* Sign out / Sleep from the power menu set lockscreen back to
         * locked — re-enter the lockscreen modal until unlocked again.   */
        if (!lockscreen_is_unlocked()) {
            if (apps_active() >= 0) apps_close();
            if (launchpad_is_open()) launchpad_close();
            power_menu_close();
            kbd_drain(); mouse_drain();
            modal_loop(lockscreen_is_unlocked, lockscreen_render, lockscreen_input);
            kbd_drain(); mouse_drain();
            last = g_ticks;
        }

        i32 k;
        while ((k = kbd_poll()) != -1) {
            /* Power menu absorbs all keys while open (Esc / arrows / Enter). */
            if (power_menu_is_open()) { power_menu_handle_key(k); continue; }
            /* Help drawer absorbs everything except F-keys so the user
             * can still reach Power / Launchpad / kernel toggle from it. */
            if (helppanel_is_open() && k != KEY_F1 && k != KEY_F2 && k != KEY_F12) {
                helppanel_handle_key(k); continue;
            }
            /* F1 kernel switching disabled in FalconOS 1.1 release */
            if (k == KEY_F1) {
                /* Kernel mode switching is now disabled for stability.
                 * Users stay in Personal mode for the best experience.
                 * Developer mode can be enabled via Settings app.       */
                continue;
            }
            if (k == KEY_F2 && g_mode == MODE_PERSONAL) {
                if (launchpad_is_open()) launchpad_close();
                else                     launchpad_open();
                continue;
            }
            if (k == KEY_F12) {
                if (launchpad_is_open()) launchpad_close();
                power_menu_open();
                continue;
            }
            if (launchpad_is_open()) {
                launchpad_input(k);
                continue;
            }
            if (g_mode == MODE_PERSONAL)  mode_personal_input(k);
            else                          mode_developer_input(k);
        }

        /* Mouse: power menu > help drawer > menubar glyphs > desktop.
         *
         * Important: the help drawer must NOT swallow clicks that fall
         * outside its 420-px right-edge panel.  Earlier builds consumed
         * any click while the drawer was up, which silently ate the
         * very first traffic-light / dock click new users tried.  Now
         * an outside click closes the drawer AND falls through to the
         * normal WM / dock / desktop pipeline so the same click also
         * does what the user expected (open Calculator, click a traffic
         * light, etc.).                                                  */
        {
            i32 mx, my; bool ml; mouse_get(&mx, &my, &ml);
            (void)ml;
            if (power_menu_is_open()) {
                bool edge = mouse_consume_click();
                (void)power_menu_handle_mouse(mx, my, edge);
            } else if (mouse_peek_click() && menu_bar_power_hit(mx, my)) {
                (void)mouse_consume_click();   /* swallow */
                power_menu_open();
            } else if (mouse_peek_click() && main_help_glyph_hit(mx, my)) {
                (void)mouse_consume_click();
                if (helppanel_is_open()) helppanel_close();
                else                     helppanel_open();
            } else if (helppanel_is_open()) {
                /* peek (don't consume) so an outside click can still
                 * reach the WM / dock / desktop after dismissing the
                 * drawer.                                                */
                i32 panel_x = (i32)FB.width - 420;
                bool inside = (mx >= panel_x);
                bool edge   = mouse_peek_click();
                if (inside && edge) {
                    (void)mouse_consume_click();
                    (void)helppanel_handle_mouse(mx, my, true);
                } else if (edge) {
                    /* outside click: dismiss drawer but DO NOT consume
                     * the click — let the WM / dock see it normally.   */
                    helppanel_close();
                }
            }
            /* else: leave the click in the queue; personal/dev paths will
             * consume it themselves (desktop pins, WM, etc.).             */
        }

        gfx_wallpaper();

        if (g_mode == MODE_PERSONAL)  mode_personal_render(g_tick);
        else                          mode_developer_render(g_tick);

        if (launchpad_is_open()) launchpad_render(g_tick);

        draw_menu_bar();
        helppanel_render(g_tick);                /* slides in/out      */
        if (power_menu_is_open()) power_menu_render(g_tick);
        gfx_apply_viewport();
        draw_cursor();

        gfx_present();
        g_tick++;

        /* pace at ~50 FPS (every 2 PIT ticks) — halt CPU between frames     */
        while (g_ticks - last < 2) __asm__ volatile ("hlt");
        last = g_ticks;
    }
}

/* Compatibility shim: old 32-bit entry expected `kernel_main(magic, info)`
 * with u32 args.  Some tooling may still reference it; forward to long_start. */
void kernel_main(u64 magic, u64 info_ptr) { long_start(magic, info_ptr); }
