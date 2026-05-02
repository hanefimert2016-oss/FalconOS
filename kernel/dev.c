/* =============================================================================
 *  FalconOS — Developer Kernel UI (FalconOS 1, IDE-style)
 * -----------------------------------------------------------------------------
 *  Where the Personal kernel paints a desktop with widgets and a dock, the
 *  Developer kernel paints an IDE.  Layout:
 *
 *    [-- TOOLBAR -----------------------------------------------------]
 *    [ EXPLORER ] [ EDITOR / REPL ]                  [ INSPECTOR     ]
 *    [          ] [               ]                  [ CPU registers ]
 *    [          ] [               ]                  [ MEM dump      ]
 *    [          ] [               ]                  [ MMAP regions  ]
 *    [-- STATUS BAR --------------------------------------------------]
 *
 *  Keys:  Up/Down  page memory dump,    L  push log,
 *         everything else is forwarded to the REPL.
 *
 *  Same scaling story as before — every dimension is computed from the
 *  framebuffer, so 1024x768 / 1080p / 2K all look the same.
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

/* ----- explorer pane (file tree mock + symbol list) ---------------------- */
static const char *EXPLORER_TREE[] = {
    "FalconOS-1/",
    "  boot/",
    "    multiboot2.asm",
    "    isr.asm",
    "  kernel/",
    "    main.c",
    "    apps.c",
    "    dev.c",
    "    repl.c",
    "    settings.c",
    "    auth.c",
    "    diskdb.c",
    "    rtc.c",
    "    locale.c",
    "  linux/",
    "    ata_pio.c",
    "    hid_keymap.c",
    "  Makefile",
};
#define EXPLORER_N (i32)(sizeof EXPLORER_TREE / sizeof EXPLORER_TREE[0])

void mode_developer_render(u32 frame)
{
    static bool greeted = false;
    if (!greeted) {
        log_push_dev("FalconOS 1 developer kernel online");
        log_push_dev("IDT installed - 32 exceptions, 16 IRQs (PIT/KBD/MOUSE live)");
        log_push_dev("type `help` to list REPL commands - F1 returns to Personal");
        greeted = true;
    }

    i32 W = (i32)FB.width;
    i32 H = (i32)FB.height;

    i32 m         = 12;
    i32 toolbar_y = 32;
    i32 toolbar_h = 36;
    i32 status_h  = 26;
    i32 body_y    = toolbar_y + toolbar_h + m;
    i32 body_h    = H - body_y - status_h - m * 2;

    i32 explorer_w  = 240;
    i32 inspector_w = (W - explorer_w) * 38 / 100;
    i32 editor_w    = W - 2 * m - explorer_w - m - inspector_w - m;
    i32 explorer_x  = m;
    i32 editor_x    = explorer_x + explorer_w + m;
    i32 inspector_x = editor_x + editor_w + m;

    /* ===== TOOLBAR ======================================================= */
    gfx_round_glass(m, toolbar_y, W - 2 * m, toolbar_h, 10);
    {
        const char *items[] = {
            "Build",  "Run",   "Stop",  "Clear log", "Page mem (Up/Down)"
        };
        u32 cols[]            = {
            COL_OK,   COL_OK,  COL_ERR, COL_WARN,    PAL_ACCENT
        };
        i32 tx = m + 14;
        gfx_text(tx, toolbar_y + 11, "FalconOS-1 :: developer", PAL_ACCENT);
        tx += gfx_text_width("FalconOS-1 :: developer") + 22;
        for (i32 i = 0; i < (i32)(sizeof items / sizeof items[0]); i++) {
            i32 bw = gfx_text_width(items[i]) + 18;
            gfx_round_outline(tx, toolbar_y + 6, bw, toolbar_h - 12, 8, PAL_HAIRLINE);
            gfx_text(tx + 9, toolbar_y + 11, items[i], cols[i]);
            tx += bw + 8;
        }
        /* live tick + frame counter on the right */
        char rt[40] = "tick ";
        char num[16];
        k_itoa(g_ticks, num, 10); k_strcat(rt, num);
        k_strcat(rt, "  frame ");
        k_itoa(frame, num, 10); k_strcat(rt, num);
        i32 rw = gfx_text_width(rt);
        gfx_text(W - m - 14 - rw, toolbar_y + 11, rt, PAL_TEXT_DIM);
    }

    /* ===== EXPLORER ====================================================== */
    gfx_round_glass(explorer_x, body_y, explorer_w, body_h, 14);
    gfx_text(explorer_x + 14, body_y + 10, "EXPLORER", PAL_ACCENT);
    {
        i32 line_h = 17;
        i32 max_lines = (body_h - 36) / line_h;
        if (max_lines > EXPLORER_N) max_lines = EXPLORER_N;
        for (i32 i = 0; i < max_lines; i++) {
            const char *s = EXPLORER_TREE[i];
            u32 col = (s[k_strlen(s) - 1] == '/') ? PAL_ACCENT : PAL_TEXT;
            gfx_text(explorer_x + 14, body_y + 32 + i * line_h, s, col);
        }
    }

    /* ===== EDITOR / LOG / REPL =========================================== */
    gfx_round_glass(editor_x, body_y, editor_w, body_h, 14);
    gfx_text(editor_x + 14, body_y + 10, "EDITOR  ::  kernel.log", PAL_ACCENT);

    /* split editor into LOG (top 60%) + REPL (bottom 40%) */
    i32 inner_y = body_y + 32;
    i32 inner_h = body_h - 36;
    i32 log_h2  = inner_h * 60 / 100;
    i32 repl_y2 = inner_y + log_h2 + 6;
    i32 repl_h2 = inner_h - log_h2 - 6;

    /* log */
    {
        i32 line_h = 18;
        i32 max_lines = (log_h2 - 8) / line_h;
        if (max_lines > LOG_LINES) max_lines = LOG_LINES;
        for (i32 i = 0; i < max_lines; i++) {
            i32 idx = (log_head + LOG_LINES - max_lines + i) % LOG_LINES;
            const char *line = log_buf[idx];
            if (!line[0]) continue;
            char num[8];
            i32 ln = i + 1;
            k_itoa(ln, num, 10);
            char prefix[8] = "  ";
            if (ln < 10) prefix[1] = num[0];
            else        { prefix[0] = num[0]; prefix[1] = num[1]; }
            prefix[2] = '|'; prefix[3] = ' '; prefix[4] = 0;
            char wline[LOG_COLS + 8];
            k_strcpy(wline, prefix);
            k_strcat(wline, line);
            u32 col = (i == max_lines - 1) ? PAL_TEXT : PAL_TEXT_DIM;
            gfx_text(editor_x + 14, inner_y + i * line_h, wline, col);
        }
    }

    /* divider */
    gfx_rect(editor_x + 8, repl_y2 - 4, editor_w - 16, 1, PAL_HAIRLINE);

    /* repl */
    repl_render(editor_x, repl_y2, editor_w, repl_h2);

    /* ===== INSPECTOR (CPU + MEM + MMAP) ================================== */
    gfx_round_glass(inspector_x, body_y, inspector_w, body_h, 14);
    gfx_text(inspector_x + 14, body_y + 10, "INSPECTOR", PAL_ACCENT);
    gfx_circle(inspector_x + inspector_w - 26, body_y + 14, 4, COL_OK);

    /* CPU panel */
    i32 ip_y = body_y + 32;
    {
        gfx_text(inspector_x + 14, ip_y, "CPU", COL_OK);
        u32 regs[4]; snapshot_regs(regs);
        u64 t = rdtsc();
        u32 t_lo = (u32)t, t_hi = (u32)(t >> 32);
        const char *names[] = { "eax", "ebx", "ecx", "edx" };
        char hex[16], line[64];
        for (i32 i = 0; i < 4; i++) {
            line[0] = 0;
            k_strcat(line, names[i]); k_strcat(line, " 0x");
            hex32(hex, regs[i]); k_strcat(line, hex);
            gfx_text(inspector_x + 14, ip_y + 20 + i * 18, line, PAL_TEXT);
        }
        line[0] = 0;
        k_strcat(line, "tsc 0x"); hex32(hex, t_hi); k_strcat(line, hex);
        hex32(hex, t_lo); k_strcat(line, hex);
        gfx_text(inspector_x + 14, ip_y + 20 + 4 * 18 + 2, line, COL_WARN);
    }

    /* MEM panel */
    i32 mem_y = ip_y + 116;
    {
        char hex[16], head[40] = "MEM 0x";
        hex32(hex, mem_addr); k_strcat(head, hex);
        gfx_text(inspector_x + 14, mem_y, head, COL_OK);

        u8 *p = (u8 *)(uptr)mem_addr;
        i32 cols = inspector_w / 22;          /* roughly 8-byte rows */
        if (cols > 16) cols = 16;
        if (cols < 8)  cols = 8;
        i32 rows = 6;
        for (i32 r = 0; r < rows; r++) {
            char row[80] = "";
            char b[4];
            hex32(hex, mem_addr + (u32)(r * cols)); k_strcat(row, hex);
            k_strcat(row, " ");
            for (i32 c = 0; c < cols; c++) {
                hexbyte(b, p[r * cols + c]); k_strcat(row, b);
                if (c == cols / 2 - 1) k_strcat(row, " ");
            }
            gfx_text(inspector_x + 14, mem_y + 18 + r * 14, row, PAL_TEXT_DIM);
        }
    }

    /* MMAP panel */
    i32 mmap_y = mem_y + 18 + 6 * 14 + 14;
    {
        char total[40] = "MMAP / RAM ";
        char hex[16];
        k_itoa(RAM_TOTAL_KB / 1024, hex, 10);
        k_strcat(total, hex); k_strcat(total, " MB");
        gfx_text(inspector_x + 14, mmap_y, total, COL_OK);

        i32 maxn = MMAP_N < 4 ? MMAP_N : 4;
        for (i32 i = 0; i < maxn; i++) {
            char ln[64];
            k_strcpy(ln, "0x");
            k_itoa(MMAP[i].base, hex, 16); k_strcat(ln, hex);
            k_strcat(ln, "  ");
            k_strcat(ln, mmap_type_name(MMAP[i].type));
            u32 col = MMAP[i].type == 1 ? COL_OK : PAL_TEXT_DIM;
            gfx_text(inspector_x + 14, mmap_y + 18 + i * 16, ln, col);
        }
    }

    /* ===== STATUS BAR ==================================================== */
    {
        i32 sy = H - status_h - m;
        gfx_round_glass(m, sy, W - 2 * m, status_h, 8);
        char left[80] = "FalconOS 1  developer  |  ";
        const falcon_user_t *u = users_at(SET.active_user);
        if (u) k_strcat(left, u->name);
        k_strcat(left, "  |  press F1 to switch to Personal");
        gfx_text(m + 14, sy + 5, left, PAL_TEXT_DIM);

        rtc_time_t t; rtc_local(&t);
        char r[32]; r[0] = 0;
        char num[8];
        if (t.hour < 10) k_strcat(r, "0");
        k_itoa(t.hour, num, 10); k_strcat(r, num); k_strcat(r, ":");
        if (t.min  < 10) k_strcat(r, "0");
        k_itoa(t.min,  num, 10); k_strcat(r, num); k_strcat(r, ":");
        if (t.sec  < 10) k_strcat(r, "0");
        k_itoa(t.sec,  num, 10); k_strcat(r, num);
        i32 rw = gfx_text_width(r);
        gfx_text(W - m - 14 - rw, sy + 5, r, PAL_ACCENT);
    }

    (void)frame;
}
