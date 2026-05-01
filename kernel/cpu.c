/* =============================================================================
 *  FalconOS — CPU port I/O & micro-utilities (no libc)
 * ============================================================================= */
#include "falcon.h"

u8 inb(u16 port)
{
    u8 v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

void outb(u16 port, u8 v)
{
    __asm__ volatile ("outb %0, %1" :: "a"(v), "Nd"(port));
}

u16 inw(u16 port)
{
    u16 v;
    __asm__ volatile ("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

void outw(u16 port, u16 v)
{
    __asm__ volatile ("outw %0, %1" :: "a"(v), "Nd"(port));
}

void insw(u16 port, void *buf, u32 count)
{
    /* repeat in word — port -> [rdi/edi] */
    __asm__ volatile ("cld; rep insw"
                      : "+D"(buf), "+c"(count)
                      : "d"(port)
                      : "memory");
}

void outsw(u16 port, const void *buf, u32 count)
{
    __asm__ volatile ("cld; rep outsw"
                      : "+S"(buf), "+c"(count)
                      : "d"(port)
                      : "memory");
}

u64 rdtsc(void)
{
    u32 lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | lo;
}

/* ---- mini libc ------------------------------------------------------------ */
i32 k_strlen(const char *s) { i32 n = 0; while (s[n]) n++; return n; }

void k_memset(void *d, u8 v, u32 n)
{
    u8 *p = d;
    while (n--) *p++ = v;
}

/* Volatile pointer + memory clobber stop the optimiser from folding
 * away a zeroing pass on a buffer that's about to go out of scope.   */
void k_explicit_bzero(void *p, u32 n)
{
    volatile u8 *vp = p;
    while (n--) *vp++ = 0;
    __asm__ __volatile__("" ::: "memory");
}

void k_memcpy(void *d, const void *s, u32 n)
{
    u8 *dp = d; const u8 *sp = s;
    while (n--) *dp++ = *sp++;
}

char *k_strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++)) {}
    return r;
}

char *k_strcat(char *d, const char *s)
{
    char *r = d;
    while (*d) d++;
    while ((*d++ = *s++)) {}
    return r;
}

void k_itoa(u32 v, char *buf, i32 base)
{
    static const char *digits = "0123456789ABCDEF";
    char tmp[16];
    i32  i = 0;
    if (v == 0) { buf[0] = '0'; buf[1] = 0; return; }
    while (v && i < 16) { tmp[i++] = digits[v % base]; v /= base; }
    i32 j = 0;
    while (i--) buf[j++] = tmp[i];
    buf[j] = 0;
}

void k_pad(char *buf, i32 width, char fill)
{
    i32 n = k_strlen(buf);
    if (n >= width) return;
    i32 pad = width - n;
    for (i32 i = n; i >= 0; i--) buf[i + pad] = buf[i];
    for (i32 i = 0; i < pad; i++) buf[i] = fill;
}

i32 k_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (u8)*a - (u8)*b;
}

i32 k_strncmp(const char *a, const char *b, i32 n)
{
    while (n-- > 0 && *a && *a == *b) { a++; b++; }
    if (n < 0) return 0;
    return (u8)*a - (u8)*b;
}

u32 k_parse_hex(const char *s)
{
    u32 v = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    while (*s) {
        u32 d = 0;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        else break;
        v = (v << 4) | d;
        s++;
    }
    return v;
}
