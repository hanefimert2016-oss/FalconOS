/* =============================================================================
 *  FalconOS — Multiboot2 memory-map parser
 * -----------------------------------------------------------------------------
 *  Captures up to MMAP_MAX entries from the boot info so the Developer
 *  Kernel can render the BIOS-reported physical memory regions.
 * ============================================================================= */
#include "falcon.h"

#define MB2_TAG_MMAP 6

mmap_entry_t MMAP[MMAP_MAX];
i32          MMAP_N;
u32          RAM_TOTAL_KB;

typedef struct __attribute__((packed)) {
    u32 type;
    u32 size;
    u32 entry_size;
    u32 entry_version;
} mb2_mmap_hdr_t;

typedef struct __attribute__((packed)) {
    u64 base;
    u64 length;
    u32 type;
    u32 reserved;
} mb2_mmap_e_t;

void mmap_parse(u32 info_ptr)
{
    if (!info_ptr) return;
    u8 *p = (u8 *)info_ptr;
    u32 total = *(u32 *)p;
    u8 *end = p + total;
    p += 8;

    while (p < end) {
        u32 type = *(u32 *)p;
        u32 size = *(u32 *)(p + 4);
        if (type == 0) break;
        if (type == MB2_TAG_MMAP) {
            mb2_mmap_hdr_t *h = (mb2_mmap_hdr_t *)p;
            u8 *e = (u8 *)h + sizeof(*h);
            u8 *eend = (u8 *)h + h->size;
            while (e < eend && MMAP_N < MMAP_MAX) {
                mb2_mmap_e_t *m = (mb2_mmap_e_t *)e;
                MMAP[MMAP_N].base   = (u32)m->base;
                MMAP[MMAP_N].length = (u32)m->length;
                MMAP[MMAP_N].type   = m->type;
                if (m->type == 1)
                    RAM_TOTAL_KB += (u32)(m->length / 1024);
                MMAP_N++;
                e += h->entry_size;
            }
        }
        p += (size + 7) & ~7u;
    }
}

const char *mmap_type_name(u32 t)
{
    switch (t) {
        case 1: return "available";
        case 2: return "reserved";
        case 3: return "ACPI rec";
        case 4: return "ACPI nvs";
        case 5: return "bad ram";
        default: return "unknown";
    }
}
