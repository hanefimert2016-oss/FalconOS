/* =============================================================================
 *  vendor/bearssl-shim/string.h — minimal freestanding <string.h>
 * -----------------------------------------------------------------------------
 *  BearSSL #includes <string.h> for memcpy/memset/memmove/memcmp/strlen.
 *  The FalconOS kernel builds with -nostdinc so we don't have a real libc.
 *  This shim declares the exact subset BearSSL uses; the symbols are
 *  defined once in vendor/bearssl-shim/strops.c against the kernel's
 *  k_memcpy / k_memset / etc.
 * ============================================================================= */
#ifndef FALCON_BEARSSL_SHIM_STRING_H
#define FALCON_BEARSSL_SHIM_STRING_H

#include <stddef.h>

void  *memcpy (void *dst, const void *src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
void  *memset (void *dst, int c, size_t n);
int    memcmp (const void *a, const void *b, size_t n);
size_t strlen (const char *s);
char  *strcpy (char *dst, const char *src);
int    strcmp (const char *a, const char *b);
char  *strchr (const char *s, int c);

#endif
