/* =============================================================================
 *  FalconOS — public kernel header (v4 "Lumen")
 * =============================================================================
 *  All public types, theme colors and module-level entry points live here so
 *  the rest of the kernel can stay surgically small.
 * ============================================================================= */
#ifndef FALCON_H
#define FALCON_H

/* ---- fixed-width primitives (no libc) ------------------------------------- */
typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned int        u32;
typedef unsigned long long  u64;
typedef signed char         i8;
typedef short               i16;
typedef int                 i32;
typedef _Bool               bool;
#define true                1
#define false               0
#define NULL                ((void *)0)

/* ---- framebuffer ---------------------------------------------------------- */
typedef struct {
    u32 *pixels;
    u32  width;
    u32  height;
    u32  pitch;
    u8   bpp;
} fb_t;

extern fb_t FB;
u32  gfx_back_w(void);
u32  gfx_back_h(void);

/* ---- macOS-Big-Sur-inspired LIGHT theme (v4 "Lumen") --------------------- */
#define COL_BG_TOP      0xEAF0F8   /* light wallpaper top    */
#define COL_BG_BOT      0xC8D5E6   /* light wallpaper bottom */
#define COL_BG_HINT     0xA8BBD3   /* deeper accent for vignette */
#define COL_PANEL       0xFFFFFF   /* frosted glass: white   */
#define COL_PANEL_HI    0xD8E1EC   /* frosted glass border   */
#define COL_PANEL_DEEP  0xF1F4F9   /* alternate panel        */
#define COL_TEXT        0x14181F   /* near-black text        */
#define COL_TEXT_DIM    0x6E7884   /* secondary slate text   */
#define COL_TEXT_FAINT  0xA3ACB7   /* tertiary text          */
#define COL_ACCENT      0x3070FF   /* primary blue           */
#define COL_ACCENT_DIM  0xB8CDFF   /* tinted accent          */
#define COL_OK          0x2BB673   /* green                  */
#define COL_WARN        0xF59F1A   /* amber                  */
#define COL_ERR         0xE53935   /* red                    */
#define COL_PURPLE      0xA45EE5
#define COL_TEAL        0x16B5A8
#define COL_GLASS       0xFFFFFF   /* dock & menubar glass tint */
#define COL_HAIRLINE    0xC4CDD9
#define COL_SHADOW      0x000000

/* ---- gfx primitives ------------------------------------------------------- */
void gfx_init(void *p, u32 w, u32 h, u32 pitch, u8 bpp);
void gfx_present(void);
void gfx_clear(u32 c);
void gfx_gradient_v(u32 top, u32 bot);
void gfx_wallpaper(void);
void gfx_pixel(i32 x, i32 y, u32 c);
void gfx_pixel_a(i32 x, i32 y, u32 c, u8 a);
void gfx_rect(i32 x, i32 y, i32 w, i32 h, u32 c);
void gfx_rect_a(i32 x, i32 y, i32 w, i32 h, u32 c, u8 a);
void gfx_round_rect(i32 x, i32 y, i32 w, i32 h, i32 r, u32 c);
void gfx_round_rect_a(i32 x, i32 y, i32 w, i32 h, i32 r, u32 c, u8 a);
void gfx_round_outline(i32 x, i32 y, i32 w, i32 h, i32 r, u32 c);
void gfx_round_glass(i32 x, i32 y, i32 w, i32 h, i32 r);
void gfx_circle(i32 cx, i32 cy, i32 r, u32 c);
void gfx_circle_a(i32 cx, i32 cy, i32 r, u32 c, u8 alpha);
void gfx_circle_outline(i32 cx, i32 cy, i32 r, u32 c);
void gfx_line(i32 x0, i32 y0, i32 x1, i32 y1, u32 c);
void gfx_text(i32 x, i32 y, const char *s, u32 c);
void gfx_text_centered(i32 cx, i32 y, const char *s, u32 c);
i32  gfx_text_width(const char *s);
void gfx_dim(u8 amount);

extern const u8 FONT8X16[95][16];

/* ---- keyboard ------------------------------------------------------------- */
i32  kbd_poll(void);
#define KEY_F1         0x101
#define KEY_F2         0x102
#define KEY_F3         0x103
#define KEY_F4         0x104
#define KEY_ESC        0x01B
#define KEY_TAB        0x009
#define KEY_ENTER      0x00A
#define KEY_BACKSPACE  0x008
#define KEY_UP         0x110
#define KEY_DOWN       0x111
#define KEY_LEFT       0x112
#define KEY_RIGHT      0x113

/* ---- mouse ---------------------------------------------------------------- */
void mouse_init(void);
void mouse_get(i32 *x, i32 *y, bool *left);
bool mouse_consume_click(void);

/* ---- timer / clock -------------------------------------------------------- */
void pit_init(void);
u32  pit_ms(void);
void pit_sleep(u32 ms);
void pit_uptime(u32 *h, u32 *m, u32 *s);
extern volatile u32 g_ticks;          /* 100 Hz, set by IRQ0 */

/* ---- gdt / idt / pic ------------------------------------------------------ */
void gdt_install(void);
void idt_install(void);
void pic_remap(void);
void pic_unmask(u8 irq);
void pic_eoi(u32 vec);

/* ---- CPU helpers ---------------------------------------------------------- */
u8   inb(u16 port);
void outb(u16 port, u8 v);
u64  rdtsc(void);

/* ---- utility -------------------------------------------------------------- */
void  k_itoa(u32 v, char *buf, i32 base);
void  k_pad(char *buf, i32 width, char fill);
i32   k_strlen(const char *s);
void  k_memcpy(void *d, const void *s, u32 n);
void  k_memset(void *d, u8 v, u32 n);
char *k_strcat(char *d, const char *s);
char *k_strcpy(char *d, const char *s);
i32   k_strcmp(const char *a, const char *b);
i32   k_strncmp(const char *a, const char *b, i32 n);
u32   k_parse_hex(const char *s);

/* ---- multiboot2 memory map ------------------------------------------------ */
typedef struct { u32 base; u32 length; u32 type; } mmap_entry_t;
#define MMAP_MAX 16
extern mmap_entry_t MMAP[MMAP_MAX];
extern i32          MMAP_N;
extern u32          RAM_TOTAL_KB;
void mmap_parse(u32 info_ptr);
const char *mmap_type_name(u32 t);

/* ---- developer log -------------------------------------------------------- */
void log_push_dev(const char *s);

/* ---- developer REPL ------------------------------------------------------- */
void repl_input(i32 key);
void repl_render(i32 x, i32 y, i32 w, i32 h);

/* ---- panic ---------------------------------------------------------------- */
extern volatile bool g_panic;
extern char          g_panic_msg[80];

/* ---- mode dispatcher ------------------------------------------------------ */
typedef enum { MODE_PERSONAL = 0, MODE_DEVELOPER = 1 } falcon_mode_t;

void mode_personal_render(u32 frame);
void mode_personal_input(i32 key);

void mode_developer_render(u32 frame);
void mode_developer_input(i32 key);

/* ---- application framework (Personal kernel) ------------------------------ */
i32          apps_count(void);
const char  *apps_name(i32 i);
u32          apps_tint(i32 i);
const char  *apps_subtitle(i32 i);
void         apps_draw_icon(i32 i, i32 cx, i32 cy);
void         apps_open(i32 i);
void         apps_close(void);
i32          apps_active(void);
void         apps_render_active(u32 frame);
void         apps_input_active(i32 key);

/* ---- Launchpad (full-screen app grid, F2) --------------------------------- */
void launchpad_open(void);
void launchpad_close(void);
bool launchpad_is_open(void);
void launchpad_render(u32 frame);
void launchpad_input(i32 key);

/* tiny global tick (incremented by main loop, NOT real time — see g_ticks) */
extern volatile u32 g_tick;

#endif /* FALCON_H */
