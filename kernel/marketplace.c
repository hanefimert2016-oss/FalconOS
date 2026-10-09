/* FalconOS Marketplace: COM1 client for an opt-in, host-side HTTPS bridge.
 * Protocol: LIST => CAT|id|version|name (+ ACK each), DONE.
 * GET|id => BEGIN|id|size|sha256 (+ ACK), CHUNK|hex (+ ACK),
 * END (+ ACK). Each incoming line is bounded; no network commands execute
 * on host, and payload is verified and interpreted as safe FAPP/1 script.
 */
#include "falcon.h"
#include "shfs.h"

#define PORT 0x3F8
#define MARKET_MAX 12
#define PKG_MAX 4096
#define LINE_MAX 196
typedef struct {
    char id[33], version[25], name[41];
} market_app_t;
static market_app_t APP[MARKET_MAX];
static i32 N_APP, rx_used, rx_expected;
static char rx_line[LINE_MAX];
static char rx_data[PKG_MAX + 1];
static char rx_id[33];
static char rx_digest[65];
static const char *status_text = "Bridge offline. Press R to refresh.";

static bool safe_id(const char *s) {
    i32 n = 0;
    for (; s[n]; n++) {
        char c = s[n];
        if (n >= 32 || !((c >= 'a' && c <= 'z') || (n > 0 && c >= '0' && c <= '9') ||
                           (n > 0 && c == '-'))) return false;
    }
    return n >= 2;
}
static void copy_small(char *dst, i32 cap, const char *src) {
    i32 i = 0;
    while (src[i] && i < cap - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}
static i32 hexn(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
static i32 parts(char *line, char *out[], i32 max) {
    i32 count = 0;
    out[count++] = line;
    for (i32 j = 0; line[j] && count < max; j++) {
        if (line[j] == '|') { line[j] = 0; out[count++] = line + j + 1; }
    }
    return count;
}
static void uart_write(const char *s) {
    while (*s) {
        i32 watchdog = 100000;
        while (!(inb(PORT + 5) & 0x20) && --watchdog) {}
        if (!watchdog) { status_text = "COM1 transmission timeout"; return; }
        outb(PORT, (u8)*s++);
    }
}
static void ack(void) { uart_write("ACK\n"); }
static bool app_meta(const char *p, const char *id) {
    if (k_strncmp(p, "FAPP/1\nid=", 14) != 0) return false;
    const char *beg = p + 14;
    i32 len = k_strlen(id);
    if (k_strncmp(beg, id, len) != 0 || beg[len] != '\n') return false;
    return true;
}
static bool command_allowed(const char *line, i32 size) {
    static const char *allowed[] = {
        "echo", "date", "uname", "uptime", "whoami", "pwd", "ls",
        "help", "cal", "hwinfo", "free", "df", "clear", NULL
    };
    if (size <= 0 || size > 180) return false;
    for (i32 i = 0; i < size; i++) {
        char c = line[i];
        if (c < 32 || c > 126 || c == ';' || c == '|' || c == '>' ||
            c == '<' || c == '$' || c == '\\' || c == 96) return false;
    }
    for (i32 j = 0; allowed[j]; j++) {
        i32 n = k_strlen(allowed[j]);
        if (size >= n && k_strncmp(line, allowed[j], n) == 0 &&
            (size == n || line[n] == ' ')) return true;
    }
    return false;
}
static bool check_package(void) {
    if (rx_used != rx_expected || !app_meta(rx_data, rx_id)) return false;
    u8 digest[32];
    char hex[65];
    sha256_hash((const u8 *)rx_data, rx_used, digest);
    hex_encode(digest, 32, hex);
    if (k_strcmp(hex, rx_digest) != 0) return false;
    /* Strict metadata delimiter and bounded source commands. */
    const char *body = NULL;
    for (i32 i = 0; i + 1 < rx_used; i++) {
        if (rx_data[i] == '\n' && rx_data[i+1] == '\n') {
            body = rx_data + i + 2;
            break;
        }
    }
    if (!body) return false;
    const char *line = body;
    bool has_cmd = false;
    for (const char *p = body; *p; p++) {
        if (*p == '\r' || (u8)*p > 127) return false;
        if (*p != '\n') continue;
        i32 n = (i32)(p - line);
        if (n > 0 && line[0] != '#') {
            if (!command_allowed(line, n)) return false;
            has_cmd = true;
        }
        line = p + 1;
    }
    return has_cmd && line[0] == 0;
}
static void on_line(char *line) {
    if (k_strcmp(line, "DONE") == 0) {
        status_text = "Catalog ready. Enter: download/run, R: refresh";
        return;
    }
    if (k_strncmp(line, "ERR|", 4) == 0) {
        status_text = "Bridge error. Check host connection.";
        rx_expected = 0;
        return;
    }
    if (k_strncmp(line, "CAT|", 4) == 0) {
        char *f[5];
        if (parts(line, f, 5) == 4 && N_APP < MARKET_MAX && safe_id(f[1])) {
            market_app_t *app = &APP[N_APP++];
            copy_small(app->id, sizeof app->id, f[1]);
            copy_small(app->version, sizeof app->version, f[2]);
            copy_small(app->name, sizeof app->name, f[3]);
        }
        ack();
        return;
    }
    if (k_strncmp(line, "BEGIN|", 6) == 0) {
        char *f[5];
        if (parts(line, f, 5) != 4 || !safe_id(f[1])) return;
        i32 size = 0;
        for (const char *p = f[2]; *p; p++) {
            if (*p < '0' || *p > '9') return;
            size = size * 10 + (*p - '0');
            if (size > PKG_MAX) return;
        }
        if (!size || k_strlen(f[3]) != 64) return;
        for (i32 j = 0; j < 64; j++) if (hexn(f[3][j]) < 0) return;
        copy_small(rx_id, sizeof rx_id, f[1]);
        copy_small(rx_digest, sizeof rx_digest, f[3]);
        rx_used = 0;
        rx_expected = size;
        status_text = "Downloading verified package...";
        ack();
        return;
    }
    if (k_strncmp(line, "CHUNK|", 6) == 0) {
        if (!rx_expected) return;
        const char *p = line + 6;
        i32 len = k_strlen(p);
        if (!len || (len & 1) || len > 32 || rx_used + len / 2 > rx_expected) return;
        for (i32 i = 0; i < len; i+=2) {
            i32 a = hexn(p[i]), b = hexn(p[i+1]);
            if (a < 0 || b < 0) return;
            rx_data[rx_used++] = (char)((a << 4) | b);
        }
        ack();
        return;
    }
    if (k_strcmp(line, "END") == 0) {
        if (rx_expected && check_package()) {
            char path[SHFS_PATH];
            k_strcpy(path, "/home/falcon/apps/");
            k_strcat(path, rx_id);
            k_strcat(path, ".pkg");
            (void)shfs_mkdir_abs("/home/falcon/apps");
            shfs_ent_t *file = shfs_open_w_abs(path, false);
            if (file && rx_used < SHFS_FBYTES) {
                k_memcpy(file->data, rx_data, rx_used);
                file->data[rx_used] = 0;
                file->len = rx_used;
                status_text = "Verified, installed to guest RAM";
                ack();
            } else {
                status_text = "Not enough guest file storage";
                uart_write("ERR\n");
            }
        } else {
            status_text = "Package signature/format rejected";
            uart_write("ERR\n");
        }
        rx_expected = 0;
        return;
    }
}
void market_init(void) {
    /* COM1 16550, 115200 baud, 8N1, polled mode (no UART IRQ). */
    outb(PORT + 1, 0);
    outb(PORT + 3, 0x80);
    outb(PORT + 0, 1);
    outb(PORT + 1, 0);
    outb(PORT + 3, 3);
    outb(PORT + 2, 0xC7);
    outb(PORT + 4, 3);
}
void market_poll(void) {
    i32 budget = 256;
    while (budget-- > 0 && (inb(PORT + 5) & 1)) {
        char c = (char)inb(PORT);
        if (c == '\r') continue;
        if (c == '\n') {
            rx_line[rx_used < 0 ? 0 : 0] = rx_line[0]; /* no-op; protocol state independent */
        }
        /* Input line cursor is independent from package byte cursor. */
        extern void market_consume_byte(char c);
        market_consume_byte(c);
    }
}
static i32 line_used;
void market_consume_byte(char c) {
    if (c == '\n') {
        rx_line[line_used] = 0;
        on_line(rx_line);
        line_used = 0;
    } else if (c >= 32 && c <= 126) {
        if (line_used < LINE_MAX - 1) rx_line[line_used++] = c;
        else { line_used = 0; status_text = "Protocol line too long"; }
    }
}
void market_refresh(void) {
    N_APP = 0;
    status_text = "Refreshing GitHub release catalog...";
    uart_write("LIST\n");
}
i32 market_count(void) { return N_APP; }
const char *market_name(i32 i) { return i>=0 && i<N_APP ? APP[i].name : ""; }
const char *market_version(i32 i) { return i>=0 && i<N_APP ? APP[i].version : ""; }
const char *market_status(void) { return status_text; }
static shfs_ent_t *package_file(i32 i) {
    if (i < 0 || i >= N_APP) return NULL;
    char p[SHFS_PATH];
    k_strcpy(p, "/home/falcon/apps/");
    k_strcat(p, APP[i].id);
    k_strcat(p, ".pkg");
    return shfs_lookup(p);
}
bool market_installed(i32 i) { return package_file(i) != NULL; }
const char *market_script(i32 i) {
    shfs_ent_t *f = package_file(i);
    if (!f || f->is_dir || !app_meta(f->data, APP[i].id)) return NULL;
    for (i32 j = 0; j + 1 < (i32)f->len; j++)
        if (f->data[j] == '\n' && f->data[j + 1] == '\n')
            return f->data + j + 2;
    return NULL;
}
void market_download(i32 i) {
    if (i < 0 || i >= N_APP) return;
    uart_write("GET|");
    uart_write(APP[i].id);
    uart_write("\n");
    status_text = "Fetching .app.pkg from GitHub Releases...";
}
