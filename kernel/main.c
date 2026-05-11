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

    /* === workspace dots (centre-left) — 4 little pips, click to switch
     * the active workspace. The current workspace is shown filled with
     * the accent colour, others are outline-only. Hovering shows a
     * subtle highlight. */
    {
        i32 wc = SET.workspace_count > 0 ? SET.workspace_count : 4;
        if (wc > 8) wc = 8;
        i32 dot_size = 14;
        i32 dot_gap  = 8;
        i32 dx_start = (W - (wc * dot_size + (wc - 1) * dot_gap)) / 2 - 80;
        i32 mx_, my_; bool ml_; mouse_get(&mx_, &my_, &ml_); (void)ml_;
        for (i32 i = 0; i < wc; i++) {
            i32 dx = dx_start + i * (dot_size + dot_gap);
            i32 dy = (H - dot_size) / 2;
            bool active = (i == SET.active_workspace);
            bool hover  = (mx_ >= dx && mx_ < dx + dot_size &&
                           my_ >= dy && my_ < dy + dot_size);
            if (active) {
                gfx_round_rect_a(dx, dy, dot_size, dot_size, 4, PAL_ACCENT, 255);
            } else if (hover) {
                gfx_round_rect_a(dx, dy, dot_size, dot_size, 4, PAL_ACCENT, 90);
            } else {
                gfx_round_outline(dx, dy, dot_size, dot_size, 4, PAL_TEXT_DIM);
            }
            if (hover && mouse_peek_click()) {
                (void)mouse_consume_click();
                SET.active_workspace = i;
                /* Switching workspace closes any centred app window so
                 * the new workspace starts clean. Apps are still
                 * available; they just aren't re-opened across spaces. */
                apps_close();
            }
        }
    }

    /* === Jarvis quick-search pill (centre, just right of the dots) ====== */
    {
        const char *placeholder =
            T("Search apps, settings, or ask Jarvis...",
              "Uygulama ara veya Jarvis'e sor...");
        i32 jw = gfx_text_width(placeholder) + 36;
        i32 jx = (W - jw) / 2 + 80;
        i32 jy = 4;
        gfx_round_rect_a(jx, jy, jw, H - 8, 11, PAL_PANEL_DEEP, 255);
        /* magnifier glyph */
        gfx_circle_outline(jx + 12, H / 2, 5, PAL_TEXT_DIM);
        gfx_line(jx + 16, H / 2 + 4, jx + 21, H / 2 + 9, PAL_TEXT_DIM);
        gfx_text(jx + 28, 7, placeholder, PAL_TEXT_FAINT);
        /* clicking opens Jarvis app */
        i32 mxp, myp; bool mlp; mouse_get(&mxp, &myp, &mlp); (void)mlp;
        if (mxp >= jx && mxp < jx + jw && myp >= 0 && myp < H &&
            mouse_peek_click()) {
            (void)mouse_consume_click();
            for (i32 i = 0; i < apps_count(); i++) {
                if (k_strcmp(apps_name(i), "Jarvis") == 0 ||
                    k_strcmp(apps_name(i), "jarvis") == 0) {
                    apps_open(i); break;
                }
            }
        }
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

/* ----------------------------------------------------------------------------
 *  draw_3d_wordmark — perspective-projected "FalconOS 1" mark
 * ----------------------------------------------------------------------------
 *  We treat each letter as a row of vertical "bars" sampled from a 5x7 glyph
 *  matrix.  Every bar is then rendered twice: once for the front face and
 *  once for the back face (offset by a 3D depth vector) and the two faces
 *  are joined by side struts so the mark reads as an extruded slab.
 *
 *  A subtle Y-axis rotation (sweep ±18°) is driven by the splash tick so the
 *  mark looks alive without burning a real 3D pipeline.                    */

/* 5 wide x 7 tall bitmap font, 1 = stroke, 0 = empty.                     */
typedef struct { char ch; const char *bits; } glyph5x7_t;
static const glyph5x7_t G3D[] = {
    { 'F', "11111""10000""10000""11110""10000""10000""10000" },
    { 'a', "00000""00000""01110""00001""01111""10001""01111" },
    { 'l', "01100""00100""00100""00100""00100""00100""01110" },
    { 'c', "00000""00000""01110""10001""10000""10001""01110" },
    { 'o', "00000""00000""01110""10001""10001""10001""01110" },
    { 'n', "00000""00000""10110""11001""10001""10001""10001" },
    { 'O', "01110""10001""10001""10001""10001""10001""01110" },
    { 'S', "01111""10000""10000""01110""00001""00001""11110" },
    { 'B', "11110""10001""10001""11110""10001""10001""11110" },
    { 'r', "00000""00000""10110""11001""10000""10000""10000" },
    { 'T', "11111""00100""00100""00100""00100""00100""00100" },
    { 'F', "11111""10000""10000""11110""10000""10000""10000" },
    { 'y', "00000""00000""10001""10001""01111""00001""11110" },
    { '1', "00100""01100""00100""00100""00100""00100""01110" },
    { ' ', "00000""00000""00000""00000""00000""00000""00000" },
};
static const char *glyph_bits(char c)
{
    for (u32 i = 0; i < sizeof G3D / sizeof G3D[0]; i++)
        if (G3D[i].ch == c) return G3D[i].bits;
    return G3D[14].bits;  /* space */
}

/* Project (x,y,z) using a soft Y-axis rotation by angle (8-bit cos/sin
 * approximated from a 256-entry table-less reduction) and a pinhole
 * perspective at z = 1024.                                              */
static void p3d(i32 px, i32 py, i32 pz, i32 ang_sx256,
                i32 ox, i32 oy, i32 *outx, i32 *outy)
{
    /* small-angle sin/cos: |ang| <= 32 so radians ~ ang/100; use cubic. */
    i32 s = ang_sx256;                      /* sin*256 approx           */
    i32 c = 256 - (s * s) / 512;            /* cos*256 ~ 1 - s^2/2      */
    /* rotate around Y axis: x' = x*c + z*s ; z' = -x*s + z*c           */
    i32 xr = (px * c + pz * s) >> 8;
    i32 zr = (-px * s + pz * c) >> 8;
    /* pinhole: x'' = x' * f / (f + z) ; f chosen so depth ~ 0.85       */
    i32 f = 800;
    i32 d = f + zr;
    if (d < 50) d = 50;
    *outx = ox + (xr * f) / d;
    *outy = oy + (py * f) / d;
}

static void draw_3d_wordmark(i32 cx, i32 cy, u8 alpha, i32 tick)
{
    /* Two-line layout:
     *      FalconOS 1
     *      Born To Fly
     * Render each character as a 5x7 grid of dots, projected through p3d().
     * The "depth" is achieved by drawing the same character at z = +18 (back)
     * with a dim colour, then at z = -18 (front) with a bright colour, plus
     * 1 px struts between the two layers at glyph corners.                */
    const char *L1 = "FalconOS 1";
    const char *L2 = "Born To Fly";

    /* gentle ±15-unit Y-axis sweep over ~150-tick splash               */
    i32 ang = ((tick % 150) - 75) / 5;   /* range -15..+15              */
    if (ang < -15) ang = -15;
    if (ang >  15) ang =  15;

    const i32 cell = 12;       /* dot pitch in source units            */
    const i32 row_h = 9 * cell;
    const i32 z_back  = +24;
    const i32 z_front = -24;

    u32 col_front = 0x9FCBFF;
    u32 col_back  = 0x2A66F5;
    u32 col_strut = 0x4A88FF;

    const char *lines[2] = { L1, L2 };
    i32 line_y_off[2] = { -row_h / 2 - 18, +row_h / 2 + 18 };

    for (i32 li = 0; li < 2; li++) {
        const char *s = lines[li];
        i32 n = 0; while (s[n]) n++;
        i32 width = n * 6 * cell;
        i32 x0 = -width / 2;
        i32 y0 = line_y_off[li];
        for (i32 ci = 0; ci < n; ci++) {
            const char *gb = glyph_bits(s[ci]);
            i32 gx0 = x0 + ci * 6 * cell;
            for (i32 r = 0; r < 7; r++) {
                for (i32 c = 0; c < 5; c++) {
                    if (gb[r * 5 + c] != '1') continue;
                    i32 px = gx0 + c * cell;
                    i32 py = y0 + r * cell;
                    /* back face dot                                  */
                    i32 ax, ay; p3d(px, py, z_back, ang, cx, cy, &ax, &ay);
                    gfx_circle_a(ax, ay, 3, col_back, alpha);
                    /* front face dot                                 */
                    i32 bx, by; p3d(px, py, z_front, ang, cx, cy, &bx, &by);
                    gfx_circle_a(bx, by, 4, col_front, alpha);
                    /* strut connecting the two faces                 */
                    gfx_line(ax, ay, bx, by, col_strut);
                }
            }
        }
    }
}


/* --------------------------------------------------------------------------- */
static void boot_splash(void)
{
    /* run for ~150 ticks (1500 ms at 100 Hz) — clean simple boot.
     * The previous splash centred a hand-pixelled blue-dragon mascot
     * inside concentric rings, but the user asked for a 3D 'FalconOS 1'
     * wordmark with 'Born To Fly' tagline instead.  The dragon function
     * is preserved one screen below (as dead code) for the desktop
     * pin until a vector replacement is generated, but it no longer
     * runs at boot.                                                      */
    u32 start = g_ticks;
    while (g_ticks - start < 150) {
        u32 dt = g_ticks - start;

        /* Deep-night gradient: top is near-black, bottom is rich royal blue
         * so the 3D mark sits on a calm, theatrical stage.                  */
        gfx_gradient_v(0x05080F, 0x0F1A33);

        i32 cx = (i32)FB.width  / 2;
        i32 cy = (i32)FB.height / 2;

        u8  alpha = dt < 20 ? (u8)(dt * 12) : 240;

        /* Three concentric faint horizon arcs behind the mark to keep
         * the screen from looking empty without any focal mascot.          */
        gfx_circle_a(cx, cy + 8, 240, 0x152A55, (u8)(alpha / 3));
        gfx_circle_a(cx, cy + 8, 180, 0x1F3A75, (u8)(alpha / 4));
        gfx_circle_a(cx, cy + 8, 120, 0x2A66F5, (u8)(alpha / 6));

        /* 3-D extruded 'FalconOS 1' / 'Born To Fly' wordmark.              */
        draw_3d_wordmark(cx, cy, alpha, (i32)dt);

        /* Subtitle: short, dim, one line — directly under the mark.        */
        gfx_text_centered(cx, cy + 160,
            T("Starting...", "Baslatiliyor..."), 0x88AACC);

        /* Architecture badge.                                              */
        gfx_text_centered(cx, cy + 184, "x86_64", 0x446688);

        /* Progress bar pinned to the bottom edge so the layout no longer
         * collides with the wordmark when the user runs at HD.             */
        i32 bar_w = 320;
        i32 bar_h = 4;
        i32 bar_x = cx - bar_w / 2;
        i32 bar_y = cy + 212;
        i32 progress = (i32)(dt * bar_w / 150);
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
