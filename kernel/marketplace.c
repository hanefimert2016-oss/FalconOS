/* FalconOS Marketplace: COM1 client for an opt-in, host-side HTTPS bridge.
 * Protocol: LIST => CAT|id|version|name (+ ACK each), DONE.
 * GET|id => BEGIN|id|size|sha256 (+ ACK), CHUNK|hex (+ ACK),
 * END (+ ACK). Each incoming line is bounded; no network commands execute
 * on host, and payload is verified and interpreted as safe FAPP/1 script.
 */
#include "falcon.h"
#include "shfs.h"

#define PORT 0x3F8
#define MARKET_MAX 48
#define PKG_MAX 4096
#define LINE_MAX 196
typedef struct {
    char id[33], version[25], name[41];
    char sha256[65], filename[80];
} market_app_t;
static market_app_t APP[MARKET_MAX];
static i32 N_APP, rx_used, rx_expected;
static char rx_line[LINE_MAX];
static char rx_data[PKG_MAX + 1];
static char rx_id[33];
static char rx_digest[65];
static char pending_id[33];
static void market_rebuild_cached(void);
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
    if (k_strncmp(p, "FAPP/1\nid=", 10) != 0) return false;
    const char *beg = p + 10;
    i32 len = k_strlen(id);
    if (k_strncmp(beg, id, len) != 0 || beg[len] != '\n') return false;
    return true;
}
static bool check_package(void) {
    if (rx_used != rx_expected || !app_meta(rx_data, rx_id)) return false;
    for (i32 i = 0; i < rx_used; i++) if (rx_data[i] == 0) return false;
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
            if (!market_line_allowed(line, n)) return false;
            has_cmd = true;
        }
        line = p + 1;
    }
    return has_cmd && line[0] == 0;
}
static void on_line(char *line) {
    if(k_strncmp(line,"PUBOK|",6)==0){
        publish_busy=false;
        publish_status="GitHub Release uploaded. Press F5 in Discover.";
        status_text=publish_status;
        outb(0xE9,'J'); /* QEMU e2e: host acknowledged real publish request */
        return;
    }
    if(k_strncmp(line,"PUBERR|",7)==0){
        publish_busy=false;
        publish_status="GitHub publish failed. See host bridge log.";
        status_text=publish_status;
        outb(0xE9,'j');
        return;
    }
    if (k_strcmp(line, "DONE") == 0) {
        status_text = "Catalog ready. Enter: download/run, R: refresh";
        return;
    }
    if (k_strncmp(line, "ERR|", 4) == 0) {
        status_text = "Bridge error. Cached apps stay available.";
        rx_expected = 0;
        pending_id[0] = 0;
        return;
    }
    if (k_strncmp(line, "CAT|", 4) == 0) {
        char *f[5];
        if (parts(line, f, 5) == 4 && safe_id(f[1])) {
            i32 index = -1;
            for (i32 i = 0; i < N_APP; i++)
                if (k_strcmp(APP[i].id, f[1]) == 0) index = i;
            if (index < 0 && N_APP < MARKET_MAX) index = N_APP++;
            if (index >= 0) {
                market_app_t *app = &APP[index];
                copy_small(app->id, sizeof app->id, f[1]);
                copy_small(app->version, sizeof app->version, f[2]);
                copy_small(app->name, sizeof app->name, f[3]);
                outb(0xE9, 'C'); /* QEMU integration event: catalog accepted */
            }
        }
        ack();
        return;
    }
    if (k_strncmp(line, "BEGIN|", 6) == 0) {
        char *f[5];
        if (parts(line, f, 5) != 4 || !safe_id(f[1]) ||
            !pending_id[0] || k_strcmp(pending_id, f[1]) != 0) return;
        i32 size = 0;
        for (const char *p = f[2]; *p; p++) {
            if (*p < '0' || *p > '9') return;
            size = size * 10 + (*p - '0');
            if (size > PKG_MAX) return;
        }
        if (!size || k_strlen(f[3]) != 64) return;
        for (i32 j = 0; j < 64; j++) if (hexn(f[3][j]) < 0) return;
        bool listed = false;
        for (i32 j = 0; j < N_APP; j++)
            if (k_strcmp(APP[j].id, f[1]) == 0) listed = true;
        if (!listed) { uart_write("ERR\n"); return; }
        copy_small(rx_id, sizeof rx_id, f[1]);
        copy_small(rx_digest, sizeof rx_digest, f[3]);
        rx_used = 0;
        rx_data[0] = 0;
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
        rx_data[rx_used] = 0;
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
                status_text = market_disk_save(rx_id, rx_data, rx_used)
                    ? "Verified app installed to safe disk"
                    : "Verified app installed to RAM (secure session)";
                outb(0xE9, 'I'); /* QEMU integration event: payload validated and installed */
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
        pending_id[0] = 0;
        return;
    }
}

/* Explicit, bounded guest->host publishing channel (COM1).
 * The GH login always lives on the host and is never sent to the VM.
 * Nothing is published merely by opening a file or launching CodeDium.
 */
static bool publish_busy;
static const char *publish_status="P: prepare upload, F10: confirm";
const char *market_publish_status(void) { return publish_status; }

static bool publish_header(const char *p,u32 bytes,char *id,char *version)
{
    if(!p||bytes<70||bytes>PKG_MAX||p[bytes]!=0 ||
       k_strncmp(p,"FAPP/1\nid=",10)!=0)return false;
    const char *at=p+10;
    u32 k=0;
    while(*at&&*at!='\n'&&k<32)id[k++]=*at++;
    id[k]=0;
    if(*at!='\n'||!safe_id(id))return false;
    const char *v=NULL;
    for(u32 i=0;i+9<bytes;i++){
        if((i==0||p[i-1]=='\n')&&k_strncmp(p+i,"version=",8)==0){
            v=p+i+8;break;
        }
    }
    if(!v)return false;
    k=0;
    while(v[k]&&v[k]!='\n'&&k<24){version[k]=v[k];k++;}
    version[k]=0;
    if(v[k]!='\n'||!market_version_valid(version))return false;
    return true;
}
bool market_publish_package(const char *payload,u32 len)
{
    if(publish_busy){publish_status="Upload already in progress";return false;}
    char id[33],version[25];
    if(!publish_header(payload,len,id,version)){
        publish_status="Invalid FAPP/1 package or metadata";return false;
    }
    if(len>PKG_MAX){publish_status="4 KiB package limit";return false;}
    /* Every package is validated by the host before its authenticated API call. */
    u8 digest[32];char hash[65],number[16];
    sha256_hash((const u8 *)payload,len,digest);
    hex_encode(digest,32,hash);
    k_itoa((i32)len,number,10);
    publish_busy=true;
    publish_status="Uploading verified package to host publisher...";
    uart_write("UP|");uart_write(id);uart_write("|");
    uart_write(version);uart_write("|");uart_write(number);
    uart_write("|");uart_write(hash);uart_write("\n");
    static const char hexchars[]="0123456789abcdef";
    for(u32 start=0;start<len;start+=16){
        u32 end=start+16;if(end>len)end=len;
        char line[38];u32 j=0;
        line[j++]='D';line[j++]='A';line[j++]='T';line[j++]='|';
        for(u32 at=start;at<end;at++){
            u8 c=(u8)payload[at];
            line[j++]=hexchars[c>>4];line[j++]=hexchars[c&15u];
        }
        line[j++]='\n';line[j]=0;
        uart_write(line);
    }
    uart_write("UPEND\n");
    return true;
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
    market_disk_restore();
    market_rebuild_cached();
}
void market_consume_byte(char c);
void market_poll(void) {
    i32 budget = 256;
    while (budget-- > 0 && (inb(PORT + 5) & 1)) {
        char c = (char)inb(PORT);
        if (c == '\r') continue;
        /* Input line cursor is independent from package byte cursor. */
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

/* Experimental direct-HTTPS Marketplace: no shell bridge, no insecure HTTP.
 * Requires an explicit TLS-enabled build and a reviewed certificate root.
 * All packages are fetched from authenticated static raw.githubusercontent.com.
 * This path stays opt-in until real public HTTPS E2E is green.
 */
#ifdef FALCON_NATIVE_MARKET
#if !defined(FALCON_BEARSSL)
#error Native Marketplace requires certificate-verified BearSSL
#endif
#define DIRECT_HOST "raw.githubusercontent.com"
#define DIRECT_ROOT "/hanefimert2016-oss/FalconOS-Marketplace/main/site/native/"
static char direct_http[4096];
static const char *direct_http_body(char *data){
    if(!data || k_strncmp(data,"HTTP/1.",7)!=0 || data[9]!='2' ||
       data[10]!='0' || data[11]!='0') return NULL;
    for(char *p=data;*p;p++)if(p[0]=='\r'&&p[1]=='\n'&&
                            p[2]=='\r'&&p[3]=='\n')return p+4;
    return NULL;
}
static bool direct_filename(const char *id,const char *version,const char *name){
    if(!safe_id(id)||!market_version_valid(version)||!name)return false;
    char expected[80];
    k_strcpy(expected,id);k_strcat(expected,"-v");
    k_strcat(expected,version);k_strcat(expected,".app.pkg");
    return k_strcmp(name,expected)==0;
}
static bool direct_https_refresh(void){
    if(!native_https_get(DIRECT_HOST,DIRECT_ROOT "catalog.fcat",
                         direct_http,sizeof direct_http))return false;
    const char *start=direct_http_body(direct_http);
    if(!start || k_strncmp(start,"FCAT/1\n",7)!=0)return false;
    char *line=(char *)start+7;
    i32 accepted=0;
    while(*line){
        char *end=line;
        while(*end && *end!='\n')end++;
        if(!*end)return false;
        *end=0;
        if(k_strncmp(line,"CAT|",4)!=0)return false;
        char *fields[7];
        if(parts(line,fields,7)!=6 || !direct_filename(fields[1],fields[2],fields[5]) ||
           k_strlen(fields[3])<2 || k_strlen(fields[3])>40 ||
           k_strlen(fields[4])!=64)return false;
        for(i32 k=0;k<64;k++)if(hexn(fields[4][k])<0)return false;
        i32 idx=-1;
        for(i32 i=0;i<N_APP;i++)if(k_strcmp(APP[i].id,fields[1])==0){idx=i;break;}
        if(idx<0 && N_APP<MARKET_MAX)idx=N_APP++;
        if(idx<0)return false;
        market_app_t *app=&APP[idx];
        copy_small(app->id,sizeof app->id,fields[1]);
        copy_small(app->name,sizeof app->name,fields[3]);
        copy_small(app->version,sizeof app->version,fields[2]);
        copy_small(app->sha256,sizeof app->sha256,fields[4]);
        copy_small(app->filename,sizeof app->filename,fields[5]);
        accepted++;
        line=end+1;
    }
    return accepted>0;
}
static bool direct_https_download(i32 i){
    market_app_t *app=&APP[i];
    if(!direct_filename(app->id,app->version,app->filename) ||
       k_strlen(app->sha256)!=64)return false;
    char url[256];
    k_strcpy(url,DIRECT_ROOT);k_strcat(url,app->filename);
    if(!native_https_get(DIRECT_HOST,url,direct_http,sizeof direct_http))return false;
    const char *payload=direct_http_body(direct_http);
    if(!payload)return false;
    u32 n=(u32)k_strlen(payload);
    if(n<70 || n>=PKG_MAX || n>=SHFS_FBYTES)return false;
    copy_small(rx_id,sizeof rx_id,app->id);
    copy_small(rx_digest,sizeof rx_digest,app->sha256);
    k_memcpy(rx_data,payload,n);rx_data[n]=0;
    rx_used=rx_expected=(i32)n;
    if(!check_package())return false;
    char path[SHFS_PATH];
    k_strcpy(path,"/home/falcon/apps/");k_strcat(path,app->id);
    k_strcat(path,".pkg");
    (void)shfs_mkdir_abs("/home/falcon/apps");
    shfs_ent_t *file=shfs_open_w_abs(path,false);
    if(!file)return false;
    k_memcpy(file->data,rx_data,n);file->data[n]=0;file->len=n;
    bool saved=market_disk_save(app->id,rx_data,n);
    status_text=saved ? "HTTPS verified: app saved to disk" :
                        "HTTPS verified: installed to RAM only";
    return true;
}
#endif

void market_refresh(void) {
#ifdef FALCON_NATIVE_MARKET
    status_text=direct_https_refresh() ?
        "Native HTTPS catalog verified" :
        "Native HTTPS catalog unavailable/invalid (cached apps intact)";
    return;
#endif
    pending_id[0] = 0;
    status_text = "Refreshing releases; cached apps remain available...";
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
#ifdef FALCON_NATIVE_MARKET
    if(!direct_https_download(i))status_text =
        "HTTPS app download or SHA-256 verification failed (no HTTP downgrade)";
    return;
#endif
    copy_small(pending_id, sizeof pending_id, APP[i].id);
    uart_write("GET|");
    uart_write(APP[i].id);
    uart_write("\n");
    status_text = "Fetching .app.pkg from GitHub Releases...";
}

void market_uninstall(i32 i) {
    if (i < 0 || i >= N_APP) return;
    char path[SHFS_PATH];
    k_strcpy(path, "/home/falcon/apps/");
    k_strcat(path, APP[i].id);
    k_strcat(path, ".pkg");
    bool ram = shfs_rm_abs(path);
    bool disk = market_disk_delete(APP[i].id);
    status_text = ram || disk ? "Application uninstalled" : "App not installed";
}


/* Offline-first catalog: persisted apps remain runnable when HTTPS is down.
 * The next LIST merges remote versions with these cached entries.
 */
static bool manifest_field(const char *pkg, const char *field,
                           char *out, i32 cap)
{
    const char *p = pkg;
    i32 nfield = k_strlen(field);
    while (*p && *p != '\n') p++;
    if (*p == '\n') p++;
    for (i32 row = 0; row < 4 && *p; row++) {
        if (k_strncmp(p, field, nfield) == 0 && p[nfield] == '=') {
            p += nfield + 1;
            i32 k = 0;
            while (p[k] && p[k] != '\n' && k < cap - 1) {
                if ((u8)p[k] < 32 || (u8)p[k] > 126) return false;
                out[k] = p[k]; k++;
            }
            if (p[k] != '\n') return false;
            out[k] = 0;
            return k > 0;
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return false;
}
static void cached_package(const char *path, bool is_dir, u32 length, void *ignored)
{
    (void)ignored;
    if (is_dir || length < 75u || N_APP >= MARKET_MAX ||
        k_strncmp(path, "/home/falcon/apps/", 18) != 0) return;
    shfs_ent_t *f = shfs_lookup(path);
    if (!f || !f->data[0]) return;
    char id[33], name[41], version[25];
    if (!manifest_field(f->data, "id", id, sizeof id) ||
        !manifest_field(f->data, "name", name, sizeof name) ||
        !manifest_field(f->data, "version", version, sizeof version) ||
        !safe_id(id) || !app_meta(f->data, id)) return;
    for (i32 i = 0; i < N_APP; i++)
        if (k_strcmp(APP[i].id, id) == 0) return;
    market_app_t *app = &APP[N_APP++];
    copy_small(app->id, sizeof app->id, id);
    copy_small(app->version, sizeof app->version, version);
    copy_small(app->name, sizeof app->name, name);
}
static void market_rebuild_cached(void)
{
    shfs_foreach_path(cached_package, NULL);
}
bool market_has_update(i32 i)
{
    shfs_ent_t *f = package_file(i);
    if (!f) return false;
    char installed_version[25];
    return manifest_field(f->data, "version", installed_version, sizeof installed_version) &&
           market_version_compare(APP[i].version, installed_version) > 0;
}
