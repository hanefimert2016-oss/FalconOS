/* Host-side unit test of the ACTUAL FalconOS COM1 protocol parser and FAPP
 * installer. No QEMU or external HTTP required.
 */
#include "falcon.h"
#include "shfs.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

settings_t SET;
static char sent[65536];
static unsigned nsent;
u8 inb(u16 port) { (void)port; return 0x20; }
void outb(u16 port, u8 value) {
    if (port == 0x3F8 && nsent < sizeof sent-1) sent[nsent++] = (char)value;
}
i32 k_strlen(const char *p) { return (i32)strlen(p); }
i32 k_strcmp(const char *a, const char *b) { return strcmp(a,b); }
i32 k_strncmp(const char *a, const char *b, i32 n) { return strncmp(a,b,(size_t)n); }
char *k_strcpy(char *d,const char *s){return strcpy(d,s);}
char *k_strcat(char *d,const char *s){return strcat(d,s);}
void k_memcpy(void *d,const void *s,u32 n){memcpy(d,s,n);}
void k_memset(void *d,u8 b,u32 n){memset(d,b,n);}
void k_itoa(u32 v,char *b,i32 base){(void)base;sprintf(b,"%u",v);}
void sha256_hash(const u8 *s, u32 n, u8 d[32]) {
    memset(d,0,32);
    for(u32 i=0;i<n;i++) d[i%32]^=(u8)(s[i]+(u8)i);
}
void hex_encode(const u8 *s,u32 n,char *d) {
    static const char *hex="0123456789abcdef";
    for(u32 i=0;i<n;i++){d[i*2]=hex[s[i]>>4];d[i*2+1]=hex[s[i]&15];}
    d[n*2]=0;
}
bool market_disk_save(const char *id,const char *s,u32 n) {
    (void)id;(void)s;(void)n;return false;
}
void market_disk_restore(void) {}
bool market_disk_delete(const char *id) {(void)id;return false;}
extern void market_consume_byte(char c);
static void inject(const char *s){while(*s)market_consume_byte(*s++);}
static void to_hex(const char *s, char *out, unsigned len) {
    static const char *hex="0123456789abcdef";
    for(unsigned i=0;i<len;i++){unsigned v=(unsigned char)s[i];out[i*2]=hex[v>>4];out[i*2+1]=hex[v&15];}
    out[len*2]=0;
}
static void expect(bool yes, int code){if(!yes){fprintf(stderr,"market protocol failed %d\n",code);exit(code);}}
int main(void){
    shfs_init();
    (void)shfs_mkdir_abs("/home/falcon/apps");
    const char *cached =
        "FAPP/1\nid=hello-world\nname=Hello World\nversion=1.0.0\n"
        "summary=Previously downloaded app\n\necho cached\n";
    shfs_ent_t *preloaded = shfs_open_w_abs("/home/falcon/apps/hello-world.pkg", false);
    expect(preloaded != NULL, 20);
    memcpy(preloaded->data, cached, strlen(cached) + 1);
    preloaded->len = (u32)strlen(cached);
    market_init();
    expect(market_count() == 1 && market_installed(0), 21);
    inject("CAT|hello-world|1.0.1|Hello World\n");
    expect(market_count()==1,1);
    expect(market_has_update(0),22);
    expect(!market_line_allowed("echo unsafe & reboot",20),2);
    market_download(0);
    expect(nsent>0,3);
    const char *pkg =
        "FAPP/1\nid=hello-world\nname=Hello World\nversion=1.0.0\n"
        "summary=Testing the sandbox\n\necho hello\n";
    char digest[65], hex[8193], header[180];
    u8 sum[32];
    unsigned len=(unsigned)strlen(pkg);
    sha256_hash((const u8 *)pkg,len,sum);
    hex_encode(sum,32,digest);
    snprintf(header,sizeof header,"BEGIN|hello-world|%u|%s\n",len,digest);
    inject(header);
    to_hex(pkg,hex,len);
    for(unsigned pos=0;pos<len*2;pos+=32){
        unsigned count=(len*2-pos<32)?len*2-pos:32;
        char chunk[48]="CHUNK|";
        memcpy(chunk+6,hex+pos,count);
        chunk[6+count]='\n';chunk[7+count]=0;
        inject(chunk);
    }
    inject("END\n");
    expect(market_installed(0),4);
    const char *installed=market_script(0);
    expect(installed && strcmp(installed,"echo hello\n")==0,5);
    market_uninstall(0);
    expect(!market_installed(0),6);
    /* Corruption cannot be installed: digest no longer matches. */
    inject(header);
    hex[0] = hex[0]=='0'?'1':'0';
    for(unsigned pos=0;pos<len*2;pos+=32){
        unsigned count=(len*2-pos<32)?len*2-pos:32;
        char chunk[48]="CHUNK|";
        memcpy(chunk+6,hex+pos,count);
        chunk[6+count]='\n';chunk[7+count]=0;
        inject(chunk);
    }
    inject("END\n");
    expect(!market_installed(0),7);
    puts("PASS native Marketplace CAT/GET/CHUNK/END, hash and uninstall");
    return 0;
}
