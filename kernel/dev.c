/* =============================================================================
 *  FalconOS — Developer Kernel UI (v4 "Lumen")
 * -----------------------------------------------------------------------------
 *  A "mission-control" view for kernel hackers.  Everything is rendered every
 *  frame, so anything you snapshot is live:
 *
 *    [ CPU  ]     eax/ebx/ecx/edx + rdtsc                  (top-left)
 *    [ MEM  ]     16 × 8 byte hex dump from cursor address (top-mid/right)
 *    [ MMAP ]     multiboot2 BIOS memory regions           (top-right)
 *    [ LOG  ]     scrolling kernel log                     (mid-bottom)
 *    [ REPL ]     interactive command prompt               (bottom)
 *
 *  Keys:  Up/Down  page memory dump,    L  push log,
 *         everything else is forwarded to the REPL.
 *
 *  Layout scales to FB.width / FB.height — looks identical at 1024×768,
 *  1920×1080 or 2560×1440.
 * ============================================================================= */
#include "falcon.h"

#define LOG_LINES   8
#define LOG_COLS    96

static char  log_buf[LOG_LINES][LOG_COLS];
static i32   log_head;
static u32   mem_addr = 0x100000;

void log_push_dev(const char *s)
{
    char *dst = log_buf[log_head];
    i32 n = 0;
    while (s[n] && n < LOG_COLS - 1) { dst[n] = s[n]; n++; }
    dst[n] = 0;
    log_head = (log_head + 1) % LOG_LINES;
}

void log_clear_dev(void)
{
    for (i32 i = 0; i < LOG_LINES; i++) log_buf[i][0] = 0;
    log_head = 0;
    log_push_dev("log cleared");
}

void mem_set_addr(u32 a) { mem_addr = a; }

void mode_developer_input(i32 key)
{
    if (key == KEY_UP)    { mem_addr -= 0x80; return; }
    if (key == KEY_DOWN)  { mem_addr += 0x80; return; }
    repl_input(key);
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
    static bool greeted = false;
    if (!greeted) {
        log_push_dev("FalconOS developer kernel online");
        log_push_dev("IDT installed - 32 exceptions, 16 IRQs (PIT/KBD/MOUSE live)");
        log_push_dev("type `help` to list REPL commands - F1 returns to Personal");
        greeted = true;
    }

    i32 W = (i32)FB.width;
    i32 H = (i32)FB.height;

    /* layout — 3 cards across the top, log + REPL stacked below */
    i32 m = 24;                                /* margin           */
    i32 top_y = 50;
    i32 top_h = 196;                           /* card height      */
    i32 cpu_w  = (W - 4 * m) * 28 / 100;       /* 28% width        */
    i32 mem_w  = (W - 4 * m) * 42 / 100;       /* 42% width        */
    i32 mmap_w = (W - 4 * m) - cpu_w - mem_w;  /* remaining        */
    i32 cpu_x  = m;
    i32 mem_x  = cpu_x + cpu_w + m;
    i32 mmap_x = mem_x + mem_w + m;

    i32 log_y  = top_y + top_h + 14;
    i32 log_h  = (H - log_y - 56) * 50 / 100;   /* half lower area */
    i32 log_w  = W - 2 * m;
    i32 repl_y = log_y + log_h + 14;
    i32 repl_h = H - repl_y - 28;

    /* ---- CPU card --------------------------------------------------- */
    {
        gfx_round_glass(cpu_x, top_y, cpu_w, top_h, 14);
        gfx_text(cpu_x + 16, top_y + 12, "CPU", PAL_ACCENT);
        gfx_text(cpu_x + cpu_w - 64, top_y + 12, "live", COL_OK);
        gfx_circle(cpu_x + cpu_w - 76, top_y + 19, 4, COL_OK);

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
            gfx_text(cpu_x + 18, top_y + 40 + i * 18, line, PAL_TEXT);
        }

        line[0] = 0;
        k_strcat(line, "tsc  0x"); hex32(hex, t_hi); k_strcat(line, hex);
        hex32(hex, t_lo); k_strcat(line, hex);
        gfx_text(cpu_x + 18, top_y + 40 + 4 * 18 + 4, line, COL_WARN);
    }

    /* ---- memory inspector card -------------------------------------- */
    {
        gfx_round_glass(mem_x, top_y, mem_w, top_h, 14);
        char hex[16], head[40] = "MEM @ 0x";
        hex32(hex, mem_addr); k_strcat(head, hex);
        gfx_text(mem_x + 16, top_y + 12, head, PAL_ACCENT);
        gfx_text(mem_x + mem_w - 110, top_y + 12, "Up/Down", PAL_TEXT_DIM);

        u8 *p = (u8 *)mem_addr;
        for (i32 r = 0; r < 8; r++) {
            char row[80] = ""; char b[4];
            hex32(hex, mem_addr + r * 16); k_strcat(row, hex);
            k_strcat(row, "  ");
            for (i32 c = 0; c < 16; c++) {
                hexbyte(b, p[r * 16 + c]); k_strcat(row, b);
                k_strcat(row, c == 7 ? "  " : " ");
            }
            gfx_text(mem_x + 16, top_y + 38 + r * 18, row, PAL_TEXT_DIM);
        }
    }

    /* ---- mmap card -------------------------------------------------- */
    {
        gfx_round_glass(mmap_x, top_y, mmap_w, top_h, 14);
        gfx_text(mmap_x + 16, top_y + 12, "MMAP", PAL_ACCENT);

        char total[40] = "RAM ";
        char hex[16];
        k_itoa(RAM_TOTAL_KB / 1024, hex, 10);
        k_strcat(total, hex); k_strcat(total, " MB");
        gfx_text(mmap_x + 16, top_y + 38, total, PAL_TEXT);

        i32 maxn = MMAP_N < 6 ? MMAP_N : 6;
        for (i32 i = 0; i < maxn; i++) {
            char ln[64];
            k_strcpy(ln, "0x");
            k_itoa(MMAP[i].base, hex, 16); k_strcat(ln, hex);
            k_strcat(ln, " ");
            k_strcat(ln, mmap_type_name(MMAP[i].type));
            u32 col = MMAP[i].type == 1 ? COL_OK : PAL_TEXT_DIM;
            gfx_text(mmap_x + 16, top_y + 60 + i * 18, ln, col);
        }
    }

    /* ---- log card --------------------------------------------------- */
    {
        gfx_round_glass(m, log_y, log_w, log_h, 14);
        gfx_text(m + 16, log_y + 12, "LOG", PAL_ACCENT);

        char tag[16] = "tick ";
        char hex[16]; k_itoa(g_ticks, hex, 10);
        k_strcat(tag, hex);
        gfx_text(m + log_w - gfx_text_width(tag) - 16, log_y + 12, tag, PAL_TEXT_DIM);

        i32 line_h = 22;
        i32 max_lines = (log_h - 40) / line_h;
        if (max_lines > LOG_LINES) max_lines = LOG_LINES;
        for (i32 i = 0; i < max_lines; i++) {
            i32 idx = (log_head + LOG_LINES - max_lines + i) % LOG_LINES;
            const char *line = log_buf[idx];
            if (!line[0]) continue;
            u32 col = (i == max_lines - 1) ? PAL_TEXT : PAL_TEXT_DIM;
            gfx_text(m + 16, log_y + 40 + i * line_h, line, col);
        }
    }
    (void)frame;

    /* ---- REPL card -------------------------------------------------- */
    repl_render(m, repl_y, log_w, repl_h);
}
