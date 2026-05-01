/* =============================================================================
 *  FalconOS — kernel entry & dual-kernel dispatcher
 * -----------------------------------------------------------------------------
 *  GRUB / Multiboot2 hands us:
 *      eax = magic 0x36D76289
 *      ebx = pointer to boot info (sequence of 8-byte aligned tags)
 *
 *  Boot sequence:
 *      1. parse Multiboot2 (framebuffer + memory map)
 *      2. install GDT, PIC, IDT, PIT, mouse
 *      3. enable interrupts (sti)
 *      4. play boot splash (~700 ms)
 *      5. enter render/input loop @ 100 Hz, dispatching to one of the kernels
 *
 *  Switching kernels is INSTANT — F1 just toggles `g_mode`.
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
static void parse_multiboot(u32 magic, u32 info_ptr)
{
    if (magic != MB2_MAGIC_BOOT || !info_ptr) return;

    u8 *p = (u8 *)info_ptr;
    u32 total = *(u32 *)p;
    u8 *end = p + total;
    p += 8;

    while (p < end) {
        mb2_tag_t *t = (mb2_tag_t *)p;
        if (t->type == MB2_TAG_END) break;
        if (t->type == MB2_TAG_FRAMEBUFFER) {
            mb2_fb_t *f = (mb2_fb_t *)t;
            gfx_init((void *)(u32)f->addr, f->width, f->height, f->pitch, f->bpp);
        }
        p += (t->size + 7) & ~7u;
    }

    mmap_parse(info_ptr);
}

/* --------------------------------------------------------------------------- */
static void draw_status_bar(void)
{
    const char *label = (g_mode == MODE_PERSONAL) ? "  Personal Kernel"
                                                   : "  Developer Kernel";
    u32 accent = (g_mode == MODE_PERSONAL) ? COL_ACCENT : COL_OK;

    i32 w = 280, h = 28;
    i32 x = (i32)(FB.width - w) / 2, y = 12;

    gfx_round_rect_a(x, y, w, h, 14, COL_PANEL, 220);
    gfx_round_outline(x, y, w, h, 14, COL_PANEL_HI);

    gfx_circle(x + 18, y + h / 2, 5, accent);
    gfx_text(x + 32, y + 6, label, COL_TEXT);

    /* hint */
    const char *hint = "F1: switch";
    gfx_text(x + w - gfx_text_width(hint) - 14, y + 6, hint, COL_TEXT_DIM);

    /* top-right uptime clock */
    u32 H, M, S; pit_uptime(&H, &M, &S);
    char clk[16]; char tmp[8];
    k_strcpy(clk, "");
    k_itoa(H, tmp, 10); if (H < 10) k_strcat(clk, "0"); k_strcat(clk, tmp);
    k_strcat(clk, ":");
    k_itoa(M, tmp, 10); if (M < 10) k_strcat(clk, "0"); k_strcat(clk, tmp);
    k_strcat(clk, ":");
    k_itoa(S, tmp, 10); if (S < 10) k_strcat(clk, "0"); k_strcat(clk, tmp);

    i32 cw = 110, cx = (i32)FB.width - cw - 18;
    gfx_round_rect_a(cx, y, cw, h, 14, COL_PANEL, 220);
    gfx_round_outline(cx, y, cw, h, 14, COL_PANEL_HI);
    gfx_text_centered(cx + cw / 2, y + 6, clk, COL_TEXT);
}

/* --------------------------------------------------------------------------- */
static void draw_cursor(void)
{
    i32 mx, my; bool ml; mouse_get(&mx, &my, &ml);
    /* drop shadow */
    gfx_circle_a(mx + 2, my + 3, 9, 0x000000, 160);
    /* outer ring */
    gfx_circle(mx, my, 9, 0xFFFFFF);
    /* inner — change color on click */
    gfx_circle(mx, my, 5, ml ? COL_ACCENT : 0x202830);
}

/* --------------------------------------------------------------------------- */
static void boot_splash(void)
{
    /* run for ~70 ticks (700 ms at 100 Hz) */
    u32 start = g_ticks;
    while (g_ticks - start < 70) {
        u32 dt = g_ticks - start;
        gfx_gradient_v(COL_BG_TOP, COL_BG_BOT);

        i32 cx = (i32)FB.width  / 2;
        i32 cy = (i32)FB.height / 2;

        u8  alpha = dt < 10 ? (u8)(dt * 25) : 255;
        i32 r     = 60 + (i32)dt;

        gfx_circle_a(cx, cy, r,      COL_ACCENT, alpha);
        gfx_circle_a(cx, cy, r - 18, COL_BG_TOP, alpha);
        gfx_circle_a(cx, cy, r - 36, COL_ACCENT, alpha);
        gfx_text_centered(cx, cy + r + 22, "FalconOS", COL_TEXT);
        gfx_text_centered(cx, cy + r + 44, "starting", COL_TEXT_DIM);

        gfx_present();
        __asm__ volatile ("hlt");
    }
}

/* --------------------------------------------------------------------------- */
static void render_panic_overlay(void)
{
    gfx_dim(120);
    i32 w = 480, h = 100;
    i32 x = ((i32)FB.width - w) / 2;
    i32 y = ((i32)FB.height - h) / 2;
    gfx_round_rect_a(x, y, w, h, 14, COL_PANEL, 240);
    gfx_round_outline(x, y, w, h, 14, COL_ERR);
    gfx_text_centered(x + w / 2, y + 18, "kernel halted", COL_ERR);
    gfx_text_centered(x + w / 2, y + 50, g_panic_msg,      COL_TEXT);
    gfx_text_centered(x + w / 2, y + 76, "reset to recover",  COL_TEXT_DIM);
}

/* --------------------------------------------------------------------------- */
void kernel_main(u32 magic, u32 info_ptr)
{
    parse_multiboot(magic, info_ptr);

    if (!FB.pixels) for (;;) __asm__ volatile ("hlt");

    /* set up real interrupts so we can have a 100 Hz clock + async I/O */
    gdt_install();
    pic_remap();
    idt_install();
    pit_init();
    mouse_init();

    pic_unmask(0);   /* PIT     */
    pic_unmask(1);   /* keyboard */
    pic_unmask(2);   /* cascade  */
    pic_unmask(12);  /* mouse    */
    __asm__ volatile ("sti");

    boot_splash();

    /* main loop: paced to PIT — wait for at least 1 tick before next frame */
    u32 last = g_ticks;
    for (;;) {
        if (g_panic) { render_panic_overlay(); gfx_present();
                       for (;;) __asm__ volatile ("hlt"); }

        i32 k;
        while ((k = kbd_poll()) != -1) {
            if (k == KEY_F1) {
                g_mode = (g_mode == MODE_PERSONAL) ? MODE_DEVELOPER : MODE_PERSONAL;
                continue;
            }
            if (g_mode == MODE_PERSONAL)  mode_personal_input(k);
            else                          mode_developer_input(k);
        }

        gfx_gradient_v(COL_BG_TOP, COL_BG_BOT);

        if (g_mode == MODE_PERSONAL)  mode_personal_render(g_tick);
        else                          mode_developer_render(g_tick);

        draw_status_bar();
        draw_cursor();

        gfx_present();
        g_tick++;

        /* pace at ~50 FPS (every 2 PIT ticks) — halt CPU between frames */
        while (g_ticks - last < 2) __asm__ volatile ("hlt");
        last = g_ticks;
    }
}
