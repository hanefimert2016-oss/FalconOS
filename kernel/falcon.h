/* =============================================================================
 *  FalconOS — public kernel header
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
    u32 *pixels;       /* base of linear ARGB framebuffer (XRGB8888)          */
    u32  width;
    u32  height;
    u32  pitch;        /* row stride in bytes                                 */
    u8   bpp;          /* always 32 for FalconOS                              */
} fb_t;

extern fb_t FB;

/* ---- macOS-inspired theme palette (0x00RRGGBB) ---------------------------- */
#define COL_BG_TOP      0x0F1216
#define COL_BG_BOT      0x222A33
#define COL_PANEL       0x1B2028
#define COL_PANEL_HI    0x2B313B
#define COL_TEXT        0xEEF0F5
#define COL_TEXT_DIM    0x8A93A1
#define COL_ACCENT      0x4F9EFF
#define COL_OK          0x46D160
#define COL_WARN        0xFFB547
#define COL_ERR         0xFF5E57
#define COL_GLASS       0x2A323C   /* dock glass tint */

/* ---- gfx primitives ------------------------------------------------------- */
void gfx_init(void *p, u32 w, u32 h, u32 pitch, u8 bpp);
void gfx_present(void);
void gfx_clear(u32 c);
void gfx_gradient_v(u32 top, u32 bot);
void gfx_pixel(i32 x, i32 y, u32 c);
void gfx_pixel_a(i32 x, i32 y, u32 c, u8 a);
void gfx_rect(i32 x, i32 y, i32 w, i32 h, u32 c);
void gfx_rect_a(i32 x, i32 y, i32 w, i32 h, u32 c, u8 a);
void gfx_round_rect(i32 x, i32 y, i32 w, i32 h, i32 r, u32 c);
void gfx_round_rect_a(i32 x, i32 y, i32 w, i32 h, i32 r, u32 c, u8 a);
void gfx_round_outline(i32 x, i32 y, i32 w, i32 h, i32 r, u32 c);
void gfx_circle(i32 cx, i32 cy, i32 r, u32 c);
void gfx_circle_outline(i32 cx, i32 cy, i32 r, u32 c);
void gfx_line(i32 x0, i32 y0, i32 x1, i32 y1, u32 c);
void gfx_text(i32 x, i32 y, const char *s, u32 c);
void gfx_text_centered(i32 cx, i32 y, const char *s, u32 c);
i32  gfx_text_width(const char *s);

/* font (8 x 16, printable ASCII 0x20-0x7E) — generated at build time */
extern const u8 FONT8X16[95][16];

/* ---- keyboard ------------------------------------------------------------- */
i32  kbd_poll(void);            /* -1 if no key, else key code */
#define KEY_F1     0x101
#define KEY_F2     0x102
#define KEY_ESC    0x01B
#define KEY_TAB    0x009
#define KEY_ENTER  0x00A
#define KEY_UP     0x110
#define KEY_DOWN   0x111
#define KEY_LEFT   0x112
#define KEY_RIGHT  0x113

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

/* ---- mode dispatcher (the two "kernels") ---------------------------------- */
typedef enum { MODE_PERSONAL = 0, MODE_DEVELOPER = 1 } falcon_mode_t;

void mode_personal_render(u32 frame);
void mode_personal_input(i32 key);

void mode_developer_render(u32 frame);
void mode_developer_input(i32 key);

/* tiny global tick (incremented by main loop) */
extern volatile u32 g_tick;

#endif /* FALCON_H */
