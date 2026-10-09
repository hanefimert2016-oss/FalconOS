/* CodeDium FAPP/1 packager: real metadata supplied by editable source comments.
 * No native ELF/privileged execution. All commands require the same allowlist
 * as Marketplace downloads. Output fits an SHFS 4097-byte file slot.
 */
#include "falcon.h"

typedef struct {
    char id[33], name[41], version[25], summary[76];
    u8 found;
} manifest_t;

static bool token(const char *p, u32 n, char *out, u32 cap)
{
    if (!n || n >= cap) return false;
    for (u32 i=0;i<n;i++) {
        if (p[i]<32 || p[i]>126) return false;
        out[i]=p[i];
    }
    out[n]=0;
    return true;
}
static bool match_key(const char *line, u32 n, const char *key, char *dest, u32 cap)
{
    u32 k=(u32)k_strlen(key);
    if (n<k || k_strncmp(line,key,(i32)k)!=0) return false;
    return token(line+k,n-k,dest,cap);
}
static bool valid_id(const char *s)
{
    u32 n=(u32)k_strlen(s);
    if (n<2||n>32||s[0]<'a'||s[0]>'z') return false;
    for (u32 i=1;i<n;i++) {
        char c=s[i];
        if (!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-')) return false;
    }
    return true;
}
static bool valid_label(const char *s, u32 max)
{
    u32 n=(u32)k_strlen(s);
    if (n<2||n>max) return false;
    for (u32 i=0;i<n;i++) {
        char c=s[i];
        if ((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
            (c>='0'&&c<='9')) continue;
        bool ok=false;
        const char *special=" .,:!?+/_()-";
        for (u32 j=0;special[j];j++) if(c==special[j]) ok=true;
        if (!ok) return false;
    }
    return true;
}
static bool write_str(char *out,u32 cap,u32 *at,const char *src)
{
    for (u32 i=0;src[i];i++) {
        if (*at>=4096u || *at>=cap-1) return false;
        out[(*at)++]=src[i];
    }
    out[*at]=0;
    return true;
}
bool codedium_build_pkg(const char *src,u32 length,char *out,u32 capacity,u32 *actual)
{
    if (!src||!out||!actual||!length||length>4095||capacity<4097)
        return false;
    manifest_t m;
    k_memset(&m,0,sizeof m);
    for (u32 start=0;start<length;) {
        u32 end=start;
        while (end<length && src[end]!='\n') {
            char c=src[end];
            if(c<32||c>126) return false;
            if(end-start>=180) return false;
            end++;
        }
        u32 n=end-start;
        const char *line=src+start;
        u8 bit=0;
        bool valid=true;
        if (n>=10 && k_strncmp(line,"# app-id: ",10)==0) {
            bit=1;valid=match_key(line,n,"# app-id: ",m.id,sizeof m.id);
        } else if (n>=12 && k_strncmp(line,"# app-name: ",12)==0) {
            bit=2;valid=match_key(line,n,"# app-name: ",m.name,sizeof m.name);
        } else if (n>=15 && k_strncmp(line,"# app-version: ",15)==0) {
            bit=4;valid=match_key(line,n,"# app-version: ",m.version,sizeof m.version);
        } else if (n>=15 && k_strncmp(line,"# app-summary: ",15)==0) {
            bit=8;valid=match_key(line,n,"# app-summary: ",m.summary,sizeof m.summary);
        } else if (n && line[0]!='#') {
            if (!market_line_allowed(line,(i32)n)) return false;
        }
        if (!valid || (bit && (m.found&bit))) return false;
        m.found |= bit;
        start=end<length?end+1:end;
    }
    if (m.found!=15 || !valid_id(m.id) || !valid_label(m.name,40) ||
        !valid_label(m.summary,75) || !market_version_valid(m.version))
        return false;
    u32 at=0;
    if (!write_str(out,capacity,&at,"FAPP/1\nid=") ||
        !write_str(out,capacity,&at,m.id) ||
        !write_str(out,capacity,&at,"\nname=") ||
        !write_str(out,capacity,&at,m.name) ||
        !write_str(out,capacity,&at,"\nversion=") ||
        !write_str(out,capacity,&at,m.version) ||
        !write_str(out,capacity,&at,"\nsummary=") ||
        !write_str(out,capacity,&at,m.summary) ||
        !write_str(out,capacity,&at,"\n\n"))
        return false;
    for(u32 i=0;i<length;i++) {
        if (at>=4096u || at>=capacity-1) return false;
        out[at++]=src[i];
    }
    if(src[length-1]!='\n') {
        if(at>=4096u || at>=capacity-1) return false;
        out[at++]='\n';
    }
    out[at]=0;
    *actual=at;
    return true;
}
