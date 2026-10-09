/* Strict SemVer precedence for Marketplace releases (SemVer 2.0 subset).
 * Comparisons are independent of lexicographic file names and release dates.
 * Invalid versions are never ordered above valid versions.
 */
#include "falcon.h"

static bool parse_component(const char **cursor, u32 *out, char end1, char end2)
{
    const char *p = *cursor;
    if (*p < '0' || *p > '9') return false;
    if (*p == '0' && p[1] >= '0' && p[1] <= '9') return false;
    u32 value = 0;
    while (*p >= '0' && *p <= '9') {
        u32 digit = (u32)(*p - '0');
        if (value > (0xFFFFFFFFu - digit) / 10u) return false;
        value = value * 10u + digit;
        p++;
    }
    if (*p != end1 && *p != end2) return false;
    *out = value; *cursor = p;
    return true;
}
static bool parse_semver(const char *s, u32 parts[3], const char **pre)
{
    if (!s) return false;
    const char *p = s;
    for (u32 i=0;i<3;i++) {
        if (!parse_component(&p, &parts[i], i==2?'-':'.', i==2?0:'.'))
            return false;
        if (i<2) p++;
    }
    if (!*p) { *pre = NULL; return true; }
    if (*p++ != '-' || !*p) return false;
    *pre = p;
    const char *begin = p;
    bool numeric = true;
    while (*p) {
        char c = *p;
        if (c == '.') {
            if (p == begin || (numeric && p - begin > 1 && begin[0] == '0'))
                return false;
            p++; begin = p; numeric = true;
            if (!*p) return false;
            continue;
        }
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
            return false;
        if (c < '0' || c > '9') numeric = false;
        p++;
    }
    return p > begin && !(numeric && p - begin > 1 && begin[0] == '0');
}
static i32 pre_ident_compare(const char *a, u32 al, const char *b, u32 bl)
{
    bool a_num=true,b_num=true;
    for (u32 i=0;i<al;i++) if (a[i]<'0'||a[i]>'9') a_num=false;
    for (u32 i=0;i<bl;i++) if (b[i]<'0'||b[i]>'9') b_num=false;
    if (a_num != b_num) return a_num?-1:1;
    if (a_num) {
        while (al>1 && *a=='0') {a++;al--;}
        while (bl>1 && *b=='0') {b++;bl--;}
        if (al != bl) return al>bl?1:-1;
    }
    u32 n=al<bl?al:bl;
    for (u32 i=0;i<n;i++) if (a[i]!=b[i]) return a[i]>b[i]?1:-1;
    return al==bl?0:(al>bl?1:-1);
}
i32 market_version_compare(const char *a, const char *b)
{
    u32 ap[3],bp[3];
    const char *apr=NULL,*bpr=NULL;
    bool av=parse_semver(a,ap,&apr), bv=parse_semver(b,bp,&bpr);
    if (!av || !bv) return av==bv?0:(av?1:-1);
    for (u32 i=0;i<3;i++) if(ap[i]!=bp[i]) return ap[i]>bp[i]?1:-1;
    if (!apr || !bpr) return apr==bpr?0:(apr?-1:1);
    while (*apr || *bpr) {
        if (!*apr || !*bpr) return *apr?1:-1;
        const char *an=apr, *bn=bpr;
        while (*apr && *apr!='.') apr++;
        while (*bpr && *bpr!='.') bpr++;
        i32 result=pre_ident_compare(an,(u32)(apr-an),bn,(u32)(bpr-bn));
        if (result) return result;
        if (*apr=='.') apr++;
        if (*bpr=='.') bpr++;
    }
    return 0;
}


bool market_version_valid(const char *s)
{
    if(!s) return false;
    u32 parts[3];
    const char *pre;
    u32 len=0;
    while(s[len]) { if(len>=24) return false; len++; }
    return parse_semver(s,parts,&pre);
}
