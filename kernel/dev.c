/* =============================================================================
 *  FalconOS — Developer Kernel UI
 * -----------------------------------------------------------------------------
 *  A lightweight "mission-control" view for kernel hackers.  Everything is
 *  rendered every frame, so anything you snapshot is live:
 *
 *    [ CPU ]   eax,ebx,ecx,edx + rdtsc                (top-left card)
 *    [ MEM ]   16 × 16 byte hex dump from cursor addr (centre card)
 *    [ LOG ]   scrolling kernel log                   (bottom card)
 *
 *  Keys:  Up/Down  page memory dump,    L  push a log line.
 * ============================================================================= */
#include "falcon.h"

#define LOG_LINES   8
#define LOG_COLS    72

static char  log_buf[LOG_LINES][LOG_COLS];
static i32   log_head;          /* next slot to write */
static u32   mem_addr = 0x100000;

static void log_push(const char *s)
{
    char *dst = log_buf[log_head];
    i32 n = 0;
    while (s[n] && n < LOG_COLS - 1) { dst[n] = s[n]; n++; }
    dst[n] = 0;
    log_head = (log_head + 1) % LOG_LINES;
}

void mode_developer_input(i32 key)
{
    if (key == KEY_UP)    mem_addr -= 0x80;
    if (key == KEY_DOWN)  mem_addr += 0x80;
    if (key == 'l')       log_push("[user] manual log entry");
}

static void hexbyte(char *out, u8 v)
{
    static const char *D = "0123456789ABCDEF";
    out[0] = D[(v >> 4) & 0xF];
    out[1] = D[v & 0xF];
    out[2] = 0;
}

static void hex32(char *out, u32 v)
{
    static const char *D = "0123456789ABCDEF";
    for (i32 i = 0; i < 8; i++) out[i] = D[(v >> ((7 - i) * 4)) & 0xF];
    out[8] = 0;
}

/* read CPU regs (best effort — at this point we've been in kernel for a while
 * so the values just snapshot the C calling convention; useful for liveness) */
static void snapshot_regs(u32 r[4])
{
    __asm__ volatile (
        "movl %%eax, %0\n\t"
        "movl %%ebx, %1\n\t"
        "movl %%ecx, %2\n\t"
        "movl %%edx, %3"
        : "=m"(r[0]), "=m"(r[1]), "=m"(r[2]), "=m"(r[3])
    );
}

void mode_developer_render(u32 frame)
{
    /* one-time greeter */
    static bool greeted = false;
    if (!greeted) {
        log_push("FalconOS developer kernel online");
        log_push("framebuffer ready / IDT bypassed / polled keyboard");
        log_push("press F1 to flip back to personal kernel");
        greeted = true;
    }

    i32 W = (i32)FB.width;

    /* ---- CPU card (top-left) ------------------------------------------- */
    {
        i32 x = 24, y = 56, w = 360, h = 168;
        gfx_round_rect_a(x, y, w, h, 12, COL_PANEL, 230);
        gfx_round_outline(x, y, w, h, 12, COL_PANEL_HI);
        gfx_text(x + 16, y + 12, "CPU", COL_ACCENT);
        gfx_text(x + w - 64, y + 12, "live", COL_OK);
        gfx_circle(x + w - 76, y + 19, 4, COL_OK);

        u32 regs[4]; snapshot_regs(regs);
        u64 t = rdtsc();
        u32 t_lo = (u32)t;
        u32 t_hi = (u32)(t >> 32);

        char hex[16], line[64];
        const char *names[] = { "eax", "ebx", "ecx", "edx" };
        for (i32 i = 0; i < 4; i++) {
            line[0] = 0;
            k_strcat(line, names[i]); k_strcat(line, "  0x");
            hex32(hex, regs[i]); k_strcat(line, hex);
            gfx_text(x + 18, y + 40 + i * 18, line, COL_TEXT);
        }

        line[0] = 0;
        k_strcat(line, "tsc  0x"); hex32(hex, t_hi); k_strcat(line, hex);
        hex32(hex, t_lo); k_strcat(line, hex);
        gfx_text(x + 18, y + 40 + 4 * 18 + 4, line, COL_WARN);
    }

    /* ---- memory inspector card (top-right) ----------------------------- */
    {
        i32 x = 408, y = 56, w = W - x - 24, h = 168;
        gfx_round_rect_a(x, y, w, h, 12, COL_PANEL, 230);
        gfx_round_outline(x, y, w, h, 12, COL_PANEL_HI);

        char hex[16], head[40] = "MEM @ 0x";
        hex32(hex, mem_addr); k_strcat(head, hex);
        gfx_text(x + 16, y + 12, head, COL_ACCENT);
        gfx_text(x + w - 110, y + 12, "Up/Down", COL_TEXT_DIM);

        u8 *p = (u8 *)mem_addr;
        for (i32 r = 0; r < 8; r++) {
            char row[80] = ""; char b[4];
            hex32(hex, mem_addr + r * 16); k_strcat(row, hex);
            k_strcat(row, "  ");
            for (i32 c = 0; c < 16; c++) {
                hexbyte(b, p[r * 16 + c]); k_strcat(row, b);
                k_strcat(row, c == 7 ? "  " : " ");
            }
            gfx_text(x + 16, y + 38 + r * 16, row, COL_TEXT_DIM);
        }
    }

    /* ---- log card (bottom) --------------------------------------------- */
    {
        i32 x = 24, y = 240, w = W - 48, h = (i32)FB.height - y - 24;
        gfx_round_rect_a(x, y, w, h, 12, COL_PANEL, 230);
        gfx_round_outline(x, y, w, h, 12, COL_PANEL_HI);
        gfx_text(x + 16, y + 12, "LOG", COL_ACCENT);

        char tag[16] = "frame ";
        char hex[16]; k_itoa(frame, hex, 10);
        k_strcat(tag, hex);
        gfx_text(x + w - gfx_text_width(tag) - 16, y + 12, tag, COL_TEXT_DIM);

        for (i32 i = 0; i < LOG_LINES; i++) {
            i32 idx = (log_head + i) % LOG_LINES;
            const char *line = log_buf[idx];
            if (!line[0]) continue;
            u32 col = (i == LOG_LINES - 1) ? COL_TEXT : COL_TEXT_DIM;
            gfx_text(x + 16, y + 40 + i * 18, line, col);
        }

        /* live cursor blink */
        if ((frame >> 4) & 1)
            gfx_rect(x + 16, y + 40 + LOG_LINES * 18 + 4, 8, 14, COL_ACCENT);
    }
}
