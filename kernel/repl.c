/* =============================================================================
 *  FalconOS — Developer Kernel REPL
 * -----------------------------------------------------------------------------
 *  A line-based interpreter usable from the Developer Kernel.  Every key
 *  pressed in dev mode is forwarded to repl_input(); Enter parses a line.
 *
 *  Commands:
 *    help                        list available verbs
 *    clear                       wipe the kernel log
 *    time                        show uptime HH:MM:SS
 *    regs                        snapshot eax/ebx/ecx/edx + tsc into log
 *    peek <hex_addr> [N]         dump N bytes (default 16) at addr
 *    poke <hex_addr> <hex_byte>  write one byte
 *    mem  <hex_addr>             move memory inspector to addr
 *    apps                        list registered Personal-mode apps
 *    panic                       deliberately raise INT3 (test exception path)
 *    echo <text...>              echo arguments to log
 * ============================================================================= */
#include "falcon.h"

#define REPL_MAX 64

static char  buf[REPL_MAX];
static i32   blen;
static char  history[5][REPL_MAX];
static i32   hist_n;

extern void log_push_dev(const char *s);
extern void log_clear_dev(void);
extern void mem_set_addr(u32 a);

static void prompt_save(const char *s)
{
    if (hist_n < 5) { k_strcpy(history[hist_n++], s); return; }
    for (i32 i = 0; i < 4; i++) k_strcpy(history[i], history[i + 1]);
    k_strcpy(history[4], s);
}

static const char *skip_word(const char *s)
{
    while (*s && *s != ' ') s++;
    while (*s == ' ')       s++;
    return s;
}

static u32 parse_hex_arg(const char *s, u32 fallback)
{
    if (!*s) return fallback;
    return k_parse_hex(s);
}

static u32 parse_dec_arg(const char *s, u32 fallback)
{
    if (!*s) return fallback;
    u32 v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    if (v == 0) return fallback;
    return v;
}

static void echo_back(const char *prefix, const char *line)
{
    char out[REPL_MAX + 8];
    k_strcpy(out, prefix);
    k_strcat(out, line);
    log_push_dev(out);
}

static void repl_pop_utf8(void)
{
    if (blen <= 0) return;
    i32 i = blen - 1;
    while (i > 0 && (((u8)buf[i] & 0xC0u) == 0x80u)) i--;
    buf[i] = 0;
    blen = i;
}

static void repl_append_key(i32 key)
{
    char utf[4];
    i32 n = key_to_utf8(key, utf);
    if (n <= 0) return;
    if (blen + n >= REPL_MAX) return;
    for (i32 i = 0; i < n; i++) buf[blen + i] = utf[i];
    blen += n;
    buf[blen] = 0;
}

static void cmd_help(void)
{
    log_push_dev("commands: help clear time date uptime regs cpu mem peek poke");
    log_push_dev("          apps ls cat touch mkdir rm pwd cd echo version panic");
}

static void cmd_date(void)
{
    rtc_time_t t; rtc_local(&t);
    char out[64], tmp[8];
    k_strcpy(out, "date ");
    k_itoa(t.day, tmp, 10); if (t.day < 10) k_strcat(out, "0"); k_strcat(out, tmp);
    k_strcat(out, "/");
    k_itoa(t.month, tmp, 10); if (t.month < 10) k_strcat(out, "0"); k_strcat(out, tmp);
    k_strcat(out, "/");
    k_itoa(t.year, tmp, 10); k_strcat(out, tmp);
    log_push_dev(out);
}

static void cmd_uptime(void)
{
    u32 H, M, S; pit_uptime(&H, &M, &S);
    char out[64], tmp[8];
    k_strcpy(out, "up ");
    k_itoa(H, tmp, 10); k_strcat(out, tmp); k_strcat(out, "h ");
    k_itoa(M, tmp, 10); k_strcat(out, tmp); k_strcat(out, "m ");
    k_itoa(S, tmp, 10); k_strcat(out, tmp); k_strcat(out, "s");
    log_push_dev(out);
}

static void cmd_cpu(void)
{
    char out[64];
    k_strcpy(out, "cpu: x86_64 long-mode, tsc ");
    char hex[20];
    u64 t = rdtsc();
    k_itoa((u32)(t >> 32), hex, 16); k_strcat(out, hex);
    k_itoa((u32)t, hex, 16); k_strcat(out, hex);
    log_push_dev(out);
}

static void cmd_version(void)
{
    log_push_dev("FalconOS 1.1.0 (x86_64 long-mode)");
    log_push_dev("  kernel: falcon-kernel 1.0.0");
    log_push_dev("  shell:  falcon-shell 1.0.0");
}

static void cmd_ls(void)
{
    log_push_dev("drwx  /home");
    log_push_dev("drwx  /system");
    log_push_dev("drwx  /apps");
    log_push_dev("-rw-  /config.ini");
}

static void cmd_pwd(void)
{
    log_push_dev("/home/user");
}

static void cmd_cat(const char *args)
{
    if (!args || !*args) {
        log_push_dev("usage: cat <file>");
        return;
    }
    char out[64];
    k_strcpy(out, "cat: ");
    k_strcat(out, args);
    k_strcat(out, " (file system demo)");
    log_push_dev(out);
}

static void cmd_touch(const char *args)
{
    if (!args || !*args) {
        log_push_dev("usage: touch <filename>");
        return;
    }
    char out[64];
    k_strcpy(out, "created: ");
    k_strcat(out, args);
    log_push_dev(out);
}

static void cmd_mkdir(const char *args)
{
    if (!args || !*args) {
        log_push_dev("usage: mkdir <dirname>");
        return;
    }
    char out[64];
    k_strcpy(out, "directory created: ");
    k_strcat(out, args);
    log_push_dev(out);
}

static void cmd_rm(const char *args)
{
    if (!args || !*args) {
        log_push_dev("usage: rm <file>");
        return;
    }
    char out[64];
    k_strcpy(out, "removed: ");
    k_strcat(out, args);
    log_push_dev(out);
}

static void cmd_cd(const char *args)
{
    if (!args || !*args) {
        log_push_dev("cd -> /home/user");
        return;
    }
    char out[64];
    k_strcpy(out, "cd -> ");
    k_strcat(out, args);
    log_push_dev(out);
}

static void cmd_time(void)
{
    u32 H, M, S; pit_uptime(&H, &M, &S);
    char out[64], tmp[8];
    k_strcpy(out, "uptime ");
    k_itoa(H, tmp, 10); if (H < 10) k_strcat(out, "0"); k_strcat(out, tmp);
    k_strcat(out, ":");
    k_itoa(M, tmp, 10); if (M < 10) k_strcat(out, "0"); k_strcat(out, tmp);
    k_strcat(out, ":");
    k_itoa(S, tmp, 10); if (S < 10) k_strcat(out, "0"); k_strcat(out, tmp);
    log_push_dev(out);
}

static void cmd_regs(void)
{
    u32 a, b, c, d;
    __asm__ volatile ("movl %%eax,%0; movl %%ebx,%1; movl %%ecx,%2; movl %%edx,%3"
                      : "=m"(a), "=m"(b), "=m"(c), "=m"(d));
    char out[80], tmp[16];
    k_strcpy(out, "eax="); k_itoa(a, tmp, 16); k_strcat(out, tmp);
    k_strcat(out, " ebx="); k_itoa(b, tmp, 16); k_strcat(out, tmp);
    k_strcat(out, " ecx="); k_itoa(c, tmp, 16); k_strcat(out, tmp);
    k_strcat(out, " edx="); k_itoa(d, tmp, 16); k_strcat(out, tmp);
    log_push_dev(out);

    u64 t = rdtsc();
    char hex[16], line[40] = "tsc=0x";
    k_itoa((u32)(t >> 32), hex, 16); k_strcat(line, hex);
    k_itoa((u32)t,         hex, 16); k_strcat(line, hex);
    log_push_dev(line);
}

static void cmd_peek(const char *args)
{
    u32 addr = parse_hex_arg(args, 0x100000);
    const char *next = skip_word(args);
    u32 n = parse_dec_arg(next, 16);
    if (n > 32) n = 32;

    u8 *p = (u8 *)(uptr)addr;
    char out[80], hex[16];
    k_strcpy(out, "peek 0x");
    k_itoa(addr, hex, 16); k_strcat(out, hex); k_strcat(out, " :");
    for (u32 i = 0; i < n; i++) {
        char b[5]; b[0] = ' ';
        u8 v = p[i];
        const char *D = "0123456789ABCDEF";
        b[1] = D[(v >> 4) & 0xF];
        b[2] = D[v & 0xF];
        b[3] = 0;
        k_strcat(out, b);
    }
    log_push_dev(out);
    mem_set_addr(addr);
}

static void cmd_poke(const char *args)
{
    u32 addr = parse_hex_arg(args, 0);
    const char *next = skip_word(args);
    u32 val  = parse_hex_arg(next, 0);
    if (!addr) { log_push_dev("poke: addr required"); return; }
    *(u8 *)(uptr)addr = (u8)val;
    char out[64], hex[16];
    k_strcpy(out, "poke 0x");
    k_itoa(addr, hex, 16); k_strcat(out, hex);
    k_strcat(out, " <- 0x");
    k_itoa(val & 0xFF, hex, 16); k_strcat(out, hex);
    log_push_dev(out);
}

static void cmd_mem(const char *args)
{
    u32 addr = parse_hex_arg(args, 0x100000);
    mem_set_addr(addr);
    char out[40], hex[16];
    k_strcpy(out, "mem -> 0x");
    k_itoa(addr, hex, 16); k_strcat(out, hex);
    log_push_dev(out);
}

static void cmd_apps(void)
{
    char out[80] = "apps:";
    for (i32 i = 0; i < apps_count(); i++) {
        k_strcat(out, " ");
        k_strcat(out, apps_display_name(i));
    }
    log_push_dev(out);
}

static void cmd_panic(void)
{
    log_push_dev("triggering INT3 (debug breakpoint) ...");
    __asm__ volatile ("int $3");
}

static void cmd_echo(const char *args)
{
    char out[REPL_MAX + 8];
    k_strcpy(out, "echo: ");
    k_strcat(out, args);
    log_push_dev(out);
}

static void execute(const char *line)
{
    if (!*line) return;
    echo_back("> ", line);
    prompt_save(line);

    if (k_strcmp(line, "help")    == 0) { cmd_help(); return; }
    if (k_strcmp(line, "clear")   == 0) { log_clear_dev(); return; }
    if (k_strcmp(line, "time")    == 0) { cmd_time(); return; }
    if (k_strcmp(line, "date")    == 0) { cmd_date(); return; }
    if (k_strcmp(line, "uptime")  == 0) { cmd_uptime(); return; }
    if (k_strcmp(line, "regs")    == 0) { cmd_regs(); return; }
    if (k_strcmp(line, "cpu")     == 0) { cmd_cpu(); return; }
    if (k_strcmp(line, "apps")    == 0) { cmd_apps(); return; }
    if (k_strcmp(line, "panic")   == 0) { cmd_panic(); return; }
    if (k_strcmp(line, "version") == 0) { cmd_version(); return; }
    if (k_strcmp(line, "ls")      == 0) { cmd_ls(); return; }
    if (k_strcmp(line, "pwd")     == 0) { cmd_pwd(); return; }

    if (k_strncmp(line, "peek", 4) == 0 && (line[4] == 0 || line[4] == ' '))
        { cmd_peek(skip_word(line)); return; }
    if (k_strncmp(line, "poke", 4) == 0 && (line[4] == 0 || line[4] == ' '))
        { cmd_poke(skip_word(line)); return; }
    if (k_strncmp(line, "mem",  3) == 0 && (line[3] == 0 || line[3] == ' '))
        { cmd_mem(skip_word(line)); return; }
    if (k_strncmp(line, "echo", 4) == 0 && (line[4] == 0 || line[4] == ' '))
        { cmd_echo(skip_word(line)); return; }
    if (k_strncmp(line, "cat", 3) == 0 && (line[3] == 0 || line[3] == ' '))
        { cmd_cat(skip_word(line)); return; }
    if (k_strncmp(line, "touch", 5) == 0 && (line[5] == 0 || line[5] == ' '))
        { cmd_touch(skip_word(line)); return; }
    if (k_strncmp(line, "mkdir", 5) == 0 && (line[5] == 0 || line[5] == ' '))
        { cmd_mkdir(skip_word(line)); return; }
    if (k_strncmp(line, "rm", 2) == 0 && (line[2] == 0 || line[2] == ' '))
        { cmd_rm(skip_word(line)); return; }
    if (k_strncmp(line, "cd", 2) == 0 && (line[2] == 0 || line[2] == ' '))
        { cmd_cd(skip_word(line)); return; }

    log_push_dev("unknown command - try `help`");
}

void repl_input(i32 key)
{
    if (key == KEY_ENTER) {
        buf[blen] = 0;
        execute(buf);
        blen = 0;
        buf[0] = 0;
        return;
    }
    if (key == KEY_BACKSPACE) {
        repl_pop_utf8();
        return;
    }
    repl_append_key(key);
}

void repl_render(i32 x, i32 y, i32 w, i32 h)
{
    gfx_round_glass(x, y, w, h, 14);
    gfx_text(x + 16, y + 12, "REPL", PAL_ACCENT);
    gfx_text(x + w - 110, y + 12, "type `help`", PAL_TEXT_DIM);

    /* recent history (above the prompt) */
    i32 prompt_y = y + h - 32;
    /* prompt line */
    char line[REPL_MAX + 8];
    k_strcpy(line, "> ");
    k_strcat(line, buf);
    gfx_text(x + 16, prompt_y, line, PAL_TEXT);
    /* blink caret */
    if ((g_ticks >> 4) & 1) {
        i32 cx = x + 16 + gfx_text_width(line);
        gfx_rect(cx, prompt_y, 8, 14, PAL_ACCENT);
    }
}
