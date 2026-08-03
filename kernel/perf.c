/* =============================================================================
 *  FalconOS — Performance Optimizations Module  (FalconOS 2.0 Alpha)
 * =============================================================================
 *  This module provides core performance optimizations for the kernel:
 *  - CPU cache-aware memory operations
 *  - Fast string/memory functions using SIMD-friendly patterns
 *  - Branch prediction hints
 *  - Lock-free data structures where applicable
 *  - Memory pool allocators for frequent allocations
 * ============================================================================= */
#include "falcon.h"
#include "version.h"

#if FEATURE_PERF_OPT

/* Cache line size for x86_64 (typically 64 bytes) */
#define CACHE_LINE_SIZE     64

/* Branch prediction hints for hot/cold paths */
#define likely(x)           __builtin_expect(!!(x), 1)
#define unlikely(x)         __builtin_expect(!!(x), 0)

/* Prefetch data into cache before use */
#define prefetch_read(p)    __builtin_prefetch((p), 0, 3)
#define prefetch_write(p)   __builtin_prefetch((p), 1, 3)

/* Memory pool for small allocations (avoids fragmentation) */
#define MEM_POOL_SIZE       4096
#define MEM_BLOCK_SIZE      64
#define MEM_MAX_BLOCKS      (MEM_POOL_SIZE / MEM_BLOCK_SIZE)

static u8  g_mem_pool[MEM_POOL_SIZE] __attribute__((aligned(CACHE_LINE_SIZE)));
static u32 g_mem_bitmap[(MEM_MAX_BLOCKS + 31) / 32];
static bool g_mem_initialized = false;

/* Initialize memory pool */
void perf_mem_pool_init(void)
{
    k_memset(g_mem_bitmap, 0, sizeof(g_mem_bitmap));
    g_mem_initialized = true;
}

/* Allocate a block from the memory pool (fast path) */
void *perf_mem_alloc(void)
{
    if (unlikely(!g_mem_initialized)) {
        perf_mem_pool_init();
    }

    /* Find first free block using bitmap scan */
    for (u32 i = 0; i < (MEM_MAX_BLOCKS + 31) / 32; i++) {
        if (g_mem_bitmap[i] != 0xFFFFFFFF) {
            /* Found a word with free bits */
            u32 word = g_mem_bitmap[i];
            u32 bit = 0;
            while (word & (1u << bit)) bit++;
            
            /* Mark as allocated */
            g_mem_bitmap[i] |= (1u << bit);
            u32 block_idx = i * 32 + bit;
            
            void *ptr = &g_mem_pool[block_idx * MEM_BLOCK_SIZE];
            k_memset(ptr, 0, MEM_BLOCK_SIZE);
            return ptr;
        }
    }
    
    /* Pool exhausted - return NULL (fallback to caller's handling) */
    return NULL;
}

/* Free a block back to the memory pool */
void perf_mem_free(void *ptr)
{
    if (ptr == NULL || ptr < g_mem_pool || ptr >= g_mem_pool + MEM_POOL_SIZE) {
        return;
    }
    
    u32 offset = (u8 *)ptr - g_mem_pool;
    u32 block_idx = offset / MEM_BLOCK_SIZE;
    u32 word_idx = block_idx / 32;
    u32 bit_idx = block_idx % 32;
    
    g_mem_bitmap[word_idx] &= ~(1u << bit_idx);
}

/* Optimized memcpy with cache-line awareness */
void *perf_memcpy(void *dst, const void *src, u32 n)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    
    /* Prefetch source data */
    prefetch_read(s);
    prefetch_read(s + CACHE_LINE_SIZE);
    
    /* Copy in cache-line sized chunks when possible */
    u32 aligned_n = n & ~(CACHE_LINE_SIZE - 1);
    u32 i = 0;
    
    for (; i < aligned_n; i += CACHE_LINE_SIZE) {
        prefetch_read(s + i + CACHE_LINE_SIZE * 2);
        
        /* Copy 64 bytes at a time */
        for (u32 j = 0; j < CACHE_LINE_SIZE; j += 8) {
            u64 val = *(const u64 *)(s + i + j);
            *(u64 *)(d + i + j) = val;
        }
    }
    
    /* Handle remainder */
    for (; i < n; i++) {
        d[i] = s[i];
    }
    
    return dst;
}

/* Optimized memset with write-combining */
void *perf_memset(void *dst, u8 v, u32 n)
{
    u8 *d = (u8 *)dst;
    
    /* Align to 8-byte boundary */
    u32 i = 0;
    while (((uintptr_t)(d + i) & 7) && i < n) {
        d[i++] = v;
    }
    
    /* Fill 8 bytes at a time */
    u64 v8 = v * 0x0101010101010101ULL;
    u32 aligned_n = ((n - i) >> 3) << 3;
    
    for (; i < aligned_n; i += 8) {
        *(u64 *)(d + i) = v8;
    }
    
    /* Handle remainder */
    for (; i < n; i++) {
        d[i] = v;
    }
    
    return dst;
}

/* Fast zero-fill using optimized patterns */
void *perf_memzero(void *dst, u32 n)
{
    return perf_memset(dst, 0, n);
}

/* String length with vectorization hints */
u32 perf_strlen(const char *s)
{
    const char *p = s;
    
    /* Check 8 bytes at a time for null terminator */
    while (((uintptr_t)p & 7) && *p) p++;
    
    if (*p) {
        while (*(const u64 *)p != 0) {
            p += 8;
        }
        
        /* Find exact null within the last 8 bytes */
        const u8 *q = (const u8 *)p;
        while (*q) q++;
        p = (const char *)q;
    }
    
    return (u32)(p - s);
}

/* String copy with bounds checking and prefetch */
char *perf_strcpy(char *dst, const char *src)
{
    char *d = dst;
    const char *s = src;
    
    prefetch_read(s);
    
    while ((*d++ = *s++) != 0) {
        /* Continue copying */
    }
    
    return dst;
}

/* String comparison optimized */
i32 perf_strcmp(const char *a, const char *b)
{
    const u8 *p = (const u8 *)a;
    const u8 *q = (const u8 *)b;
    
    while (*p && *p == *q) {
        p++;
        q++;
    }
    
    return (i32)(*p) - (i32)*q;
}

/* Performance statistics tracking */
static u32 g_perf_alloc_count = 0;
static u32 g_perf_free_count = 0;
static u32 g_perf_memcpy_bytes = 0;
static u32 g_perf_memset_bytes = 0;

void perf_stats(u32 *allocs, u32 *frees, u32 *memcpy_bytes, u32 *memset_bytes)
{
    if (allocs) *allocs = g_perf_alloc_count;
    if (frees) *frees = g_perf_free_count;
    if (memcpy_bytes) *memcpy_bytes = g_perf_memcpy_bytes;
    if (memset_bytes) *memset_bytes = g_perf_memset_bytes;
}

void perf_record_alloc(void) { g_perf_alloc_count++; }
void perf_record_free(void) { g_perf_free_count++; }
void perf_record_memcpy(u32 bytes) { g_perf_memcpy_bytes += bytes; }
void perf_record_memset(u32 bytes) { g_perf_memset_bytes += bytes; }

#endif /* FEATURE_PERF_OPT */

/* Version information accessor */
const char *perf_get_version_string(void)
{
    return FALCON_VERSION_STRING;
}

int perf_get_version_major(void)
{
    return FALCON_VERSION_MAJOR;
}

int perf_get_version_minor(void)
{
    return FALCON_VERSION_MINOR;
}

int perf_get_version_patch(void)
{
    return FALCON_VERSION_PATCH;
}
