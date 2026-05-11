/* =============================================================================
 *  vendor/bearssl-shim/stdlib.h — freestanding stub
 * -----------------------------------------------------------------------------
 *  BearSSL itself never calls into <stdlib.h>; the only thing dragging it in
 *  is GCC's own <mm_malloc.h> (pulled by <x86intrin.h>, which in turn is
 *  pulled by vendor/bearssl/src/inner.h on any x86 GCC build), and that
 *  header just wants size_t plus posix_memalign/aligned_alloc prototypes.
 *
 *  We provide the bare minimum so the include chain resolves under
 *  -nostdinc.  None of these symbols are actually referenced by any
 *  *.c file that the kernel links — if they were the linker would
 *  yell about unresolved externals, which is exactly what we want.
 * ============================================================================= */
#ifndef BEARSSL_SHIM_STDLIB_H
#define BEARSSL_SHIM_STDLIB_H

#include <stddef.h>   /* size_t */

/* mm_malloc.h declares these; we only need the prototypes to exist. */
int  posix_memalign(void **memptr, size_t alignment, size_t size);
void *aligned_alloc(size_t alignment, size_t size);
void *malloc(size_t size);
void  free(void *ptr);

#endif
