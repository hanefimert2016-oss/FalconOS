/* =============================================================================
 *  FalconOS — kernel entry & dual-kernel dispatcher
 * -----------------------------------------------------------------------------
 *  Multiboot2 hands us:
 *    eax = magic (0x36D76289)
 *    ebx = pointer to boot info (sequence of 8-byte aligned tags)
 *  We locate the framebuffer tag (type=8) and feed it to the gfx layer, then
 *  enter a tight render/input loop dispatching to one of the two kernels.
 *
 *  Switching is INSTANT: F1 toggles the active kernel — there is a single
 *  dispatcher, no context switch, just a different render path.
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

volatile u32      g_tick    = 0;
static falcon_mode_t  g_mode = MODE_PERSONAL;

/* --------------------------------------------------------------------------- */
static void parse_multiboot(u32 magic, u32 info_ptr)
{
    if (magic != MB2_MAGIC_BOOT || !info_ptr) return;

    u8 *p = (u8 *)info_ptr;
    u32 total = *(u32 *)p;
    u8 *end = p + total;
    p += 8;                         /* skip total_size + reserved */

    while (p < end) {
        mb2_tag_t *t = (mb2_tag_t *)p;
        if (t->type == MB2_TAG_END) break;
        if (t->type == MB2_TAG_FRAMEBUFFER) {
            mb2_fb_t *f = (mb2_fb_t *)t;
            gfx_init((void *)(u32)f->addr, f->width, f->height, f->pitch, f->bpp);
        }
        p += (t->size + 7) & ~7u;
    }
}

/* --------------------------------------------------------------------------- */
static void draw_status_bar(void)
{
    /* macOS-style top status pill */
    const char *label_p = "  Personal Kernel";
    const char *label_d = "  Developer Kernel";
    const char *label   = (g_mode == MODE_PERSONAL) ? label_p : label_d;
    u32 accent = (g_mode == MODE_PERSONAL) ? COL_ACCENT : COL_OK;

    i32 w = 280, h = 28;
    i32 x = (i32)(FB.width - w) / 2, y = 12;

    gfx_round_rect_a(x, y, w, h, 14, COL_PANEL, 220);
    gfx_round_outline(x, y, w, h, 14, COL_PANEL_HI);

    /* mode indicator dot */
    gfx_circle(x + 18, y + h / 2, 5, accent);

    gfx_text(x + 32, y + 6, label, COL_TEXT);

    /* hint on right */
    const char *hint = "F1 \xC2\xB7 switch";  /* middle-dot in UTF-8, falls back */
    /* ASCII fallback: */
    hint = "F1: switch";
    gfx_text(x + w - gfx_text_width(hint) - 14, y + 6, hint, COL_TEXT_DIM);
}

/* --------------------------------------------------------------------------- */
void kernel_main(u32 magic, u32 info_ptr)
{
    parse_multiboot(magic, info_ptr);

    if (!FB.pixels) {
        /* No framebuffer — halt. (Cannot render anything useful.) */
        for (;;) __asm__ volatile ("hlt");
    }

    for (;;) {
        /* poll keyboard */
        i32 k = kbd_poll();
        if (k == KEY_F1) {
            g_mode = (g_mode == MODE_PERSONAL) ? MODE_DEVELOPER : MODE_PERSONAL;
        } else if (k != -1) {
            if (g_mode == MODE_PERSONAL)  mode_personal_input(k);
            else                          mode_developer_input(k);
        }

        /* draw frame */
        gfx_gradient_v(COL_BG_TOP, COL_BG_BOT);

        if (g_mode == MODE_PERSONAL)  mode_personal_render(g_tick);
        else                          mode_developer_render(g_tick);

        draw_status_bar();

        gfx_present();

        g_tick++;

        /* crude pacing: ~30-60 FPS depending on host */
        for (volatile u32 i = 0; i < 800000; i++) { __asm__ volatile (""); }
    }
}
