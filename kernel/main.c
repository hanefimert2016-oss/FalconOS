/* =============================================================================
 *  FalconOS — kernel entry & dispatcher  (v5 "Aurora", x86_64 long mode)
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

    mmap_parse((u32)info_ptr);
}

/* --------------------------------------------------------------------------- */
/* Top menu bar (full-width frosted strip with kernel name + clock).           */
static void draw_menu_bar(void)
{
    i32 W = (i32)FB.width;
    i32 H = 30;

    /* glass strip across the top */
    gfx_rect_a(0, 0, W, H,     PAL_PANEL,    220);
    gfx_rect_a(0, H, W, 1,     PAL_HAIRLINE, 255);

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
        const char *hint = T("F1 kernel    F2 Launchpad    Esc closes",
                             "F1 cekirdek    F2 Launchpad    Esc kapatir");
        i32 hw = gfx_text_width(hint) + 28;
        i32 hx = (W - hw) / 2;
        gfx_round_rect_a(hx, 4, hw, H - 8, 11, PAL_PANEL_DEEP, 255);
        gfx_text(hx + 14, 7, hint, PAL_TEXT_DIM);
    }

    /* right: HH:MM:SS  +  small lang badge */
    u32 H_, M_, S_; pit_uptime(&H_, &M_, &S_);
    char clk[16]; char tmp[8];
    k_strcpy(clk, "");
    k_itoa(H_, tmp, 10); if (H_ < 10) k_strcat(clk, "0"); k_strcat(clk, tmp);
    k_strcat(clk, ":");
    k_itoa(M_, tmp, 10); if (M_ < 10) k_strcat(clk, "0"); k_strcat(clk, tmp);
    k_strcat(clk, ":");
    k_itoa(S_, tmp, 10); if (S_ < 10) k_strcat(clk, "0"); k_strcat(clk, tmp);

    i32 cw = gfx_text_width(clk);
    gfx_text(W - cw - 18, 7, clk, PAL_TEXT);

    const char *lang = SET.lang == LANG_TR ? "TR" : "EN";
    gfx_round_rect_a(W - cw - 56, 6, 28, 18, 9, PAL_PANEL_DEEP, 255);
    gfx_text(W - cw - 48, 8, lang, PAL_ACCENT);
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

/* --------------------------------------------------------------------------- */
static void boot_splash(void)
{
    /* run for ~70 ticks (700 ms at 100 Hz) — palette-aware fade.            */
    u32 start = g_ticks;
    while (g_ticks - start < 70) {
        u32 dt = g_ticks - start;
        gfx_gradient_v(PAL_BG_TOP, PAL_BG_BOT);

        i32 cx = (i32)FB.width  / 2;
        i32 cy = (i32)FB.height / 2;

        u8  alpha = dt < 10 ? (u8)(dt * 25) : 255;
        i32 r     = 60 + (i32)dt;

        gfx_circle_a(cx, cy, r,      PAL_ACCENT,    alpha);
        gfx_circle_a(cx, cy, r - 18, PAL_PANEL,     alpha);
        gfx_circle_a(cx, cy, r - 36, PAL_ACCENT,    alpha);
        gfx_text_centered(cx, cy + r + 22, "FalconOS",         PAL_TEXT);
        gfx_text_centered(cx, cy + r + 44,
            T("starting Aurora", "Aurora baslatiliyor"),       PAL_TEXT_DIM);
        gfx_text_centered(cx, cy + r + 64, "x86_64 long mode", PAL_TEXT_FAINT);

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

    pic_unmask(0);   /* PIT      */
    pic_unmask(1);   /* keyboard */
    pic_unmask(2);   /* cascade  */
    pic_unmask(12);  /* mouse    */
    __asm__ volatile ("sti");

    boot_splash();

    /* installer: only on the very first boot                                */
    if (!SET.installed) {
        modal_loop(installer_is_done, installer_render, installer_input);
    }

    /* lock screen — must enter the password to reach the desktop           */
    modal_loop(lockscreen_is_unlocked, lockscreen_render, lockscreen_input);

    /* main loop: paced to PIT — wait for at least 1 tick before next frame */
    u32 last = g_ticks;
    for (;;) {
        if (g_panic) { render_panic_overlay(); gfx_present();
                       for (;;) __asm__ volatile ("hlt"); }

        i32 k;
        while ((k = kbd_poll()) != -1) {
            if (k == KEY_F1) {
                g_mode = (g_mode == MODE_PERSONAL) ? MODE_DEVELOPER : MODE_PERSONAL;
                if (launchpad_is_open()) launchpad_close();
                continue;
            }
            if (k == KEY_F2 && g_mode == MODE_PERSONAL) {
                if (launchpad_is_open()) launchpad_close();
                else                     launchpad_open();
                continue;
            }
            if (launchpad_is_open()) {
                launchpad_input(k);
                continue;
            }
            if (g_mode == MODE_PERSONAL)  mode_personal_input(k);
            else                          mode_developer_input(k);
        }

        gfx_wallpaper();

        if (g_mode == MODE_PERSONAL)  mode_personal_render(g_tick);
        else                          mode_developer_render(g_tick);

        if (launchpad_is_open()) launchpad_render(g_tick);

        draw_menu_bar();
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
