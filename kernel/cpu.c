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
