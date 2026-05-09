/* =============================================================================
 *  FalconOS — firmware / VM hardware probes (CPU / framebuffer summary)
 * -----------------------------------------------------------------------------
 *  Fills a concise text block for Terminal `hwinfo` and tooling.
 * ============================================================================= */
#include "falcon.h"

static void cpuid_r(u32 leaf, u32 sub, u32 r[4])
{
    __asm__ volatile ("cpuid"
                      : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3])
                      : "a"(leaf), "c"(sub));
}

void hw_probe_summary(char *dst, i32 cap)
{
    if (!dst || cap < 24) return;
    dst[0] = 0;

    u32 r[4], maxLeaf;
    cpuid_r(0, 0, r);
    maxLeaf = r[0];

    char vend[13];
    *(u32 *)(vend + 0) = r[1];
    *(u32 *)(vend + 4) = r[3];
    *(u32 *)(vend + 8) = r[2];
    vend[12] = 0;

    char brand[64];
    k_memset(brand, 0, sizeof brand);

    u32 eax28;
    cpuid_r(0x80000000, 0, r);
    eax28 = r[0];
    if (eax28 >= 0x80000004) {
        i32 off = 0;
        for (u32 L = 0x80000002; L <= 0x80000004 && off + 15 < (i32)sizeof brand; L++) {
            cpuid_r(L, 0, r);
            k_memcpy(brand + off +  0, r + 0, 4);
            k_memcpy(brand + off +  4, r + 1, 4);
            k_memcpy(brand + off +  8, r + 2, 4);
            k_memcpy(brand + off + 12, r + 3, 4);
            off += 16;
        }
        for (i32 i = (i32)k_strlen(brand) - 1; i >= 0; i--) {
            if (brand[i] == ' ') brand[i] = 0;
            else if (brand[i]) break;
        }
    }

    /* Copy leading line ------------------------------------------------ */
    const char *src = (brand[0] ? brand : vend);
    i32 i = 0;
    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;

    /* Second line ------------------------------------------------------ */
    char ramt[24];
    if (RAM_TOTAL_BYTES >= ((u64)1024 * 1024))
        k_u64_to_dec(RAM_TOTAL_BYTES / (u64)(1024 * 1024), ramt);
    else
        k_strcpy(ramt, "0");

    char tail[128];
    tail[0] = 0;
    k_strcat(tail, "RAM(mmap)="); k_strcat(tail, ramt); k_strcat(tail, "MiB FB=");
    char w[12], h[12], bp[12];
    k_itoa(FB.width, w, 10);  k_strcat(tail, w); k_strcat(tail, "x");
    k_itoa(FB.height, h, 10); k_strcat(tail, h); k_strcat(tail, "@");
    k_itoa(FB.bpp, bp, 10);   k_strcat(tail, bp); k_strcat(tail, " CPUID ");

    cpuid_r(1, 0, r);
    char fam[16];
    k_itoa(r[0], fam, 16);
    k_strcat(tail, "1:"); k_strcat(tail, fam);

    if (maxLeaf >= 7) {
        cpuid_r(7, 0, r);
        k_strcat(tail, " ext7:");
        char ex[16];
        k_itoa(r[0], ex, 16);
        k_strcat(tail, ex);
    }

    i32 u = k_strlen(dst);
    if (u + k_strlen(tail) + 2 >= cap)
        return;
    dst[u++] = '\n';
    k_memcpy(dst + u, tail, (u32)k_strlen(tail) + 1u);
}
