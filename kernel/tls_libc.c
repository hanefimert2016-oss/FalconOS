/* C library primitives required by a freestanding BearSSL TLS build.
 * x86_64 -fno-builtin ensures these do not recursively call themselves.
 * Only exported when FALCON_BEARSSL is enabled.
 */
#include "falcon.h"
#ifdef FALCON_BEARSSL
typedef unsigned long size_t;
void *memcpy(void *dst,const void *src,size_t n){
    u8 *d=(u8 *)dst;const u8 *s=(const u8 *)src;
    for(size_t i=0;i<n;i++)d[i]=s[i];return dst;
}
void *memmove(void *dst,const void *src,size_t n){
    u8 *d=(u8 *)dst;const u8 *s=(const u8 *)src;
    if(d<s)for(size_t i=0;i<n;i++)d[i]=s[i];
    else if(d>s)while(n--)d[n]=s[n];
    return dst;
}
void *memset(void *dst,int val,size_t n){
    u8 *d=(u8 *)dst;for(size_t i=0;i<n;i++)d[i]=(u8)val;
    return dst;
}
int memcmp(const void *a,const void *b,size_t n){
    const u8 *x=(const u8 *)a,*y=(const u8 *)b;
    for(size_t i=0;i<n;i++)if(x[i]!=y[i])return (int)x[i]-(int)y[i];
    return 0;
}
size_t strlen(const char *s){size_t n=0;while(s[n])n++;return n;}
/* Glibc FORTIFY entry points referenced by upstream BearSSL despite a
 * freestanding build; enforce destination capacity rather than bypassing it.
 */
static void bound_failure(void){
    for(;;)__asm__ volatile("cli; hlt");
}
void *__memcpy_chk(void *dst,const void *src,size_t n,size_t bound){
    if(n>bound)bound_failure();
    return memcpy(dst,src,n);
}
void *__memset_chk(void *dst,int val,size_t n,size_t bound){
    if(n>bound)bound_failure();
    return memset(dst,val,n);
}
void *__memmove_chk(void *dst,const void *src,size_t n,size_t bound){
    if(n>bound)bound_failure();
    return memmove(dst,src,n);
}
/* This is only the BearSSL fallback; caller must explicitly set X509 time.
 * 1970 cannot silently validate modern certificates. */
long time(long *timestamp){
    if(timestamp)*timestamp=0;
    return 0;
}
#endif
