/* =============================================================================
 *  FalconOS — disk-backed settings ("FalconFS" superblock, v5+)
 * -----------------------------------------------------------------------------
 *  We persist the entire `settings_t` (including hashed user records) to the
 *  first 4 sectors of ATA device 0.  No real filesystem — just a single
 *  superblock with a magic + version + length-prefixed copy of SET, padded
 *  to 2048 bytes and sealed with a Fletcher-16 checksum.
 *
 *      LBA0 .. LBA3   (2048 bytes)   "FalconFS" superblock
 *
 *  Every installer wizard completion, every Settings ▸ Users mutation and
 *  every Settings change calls diskdb_save() so the next cold boot resumes
 *  from disk instead of dropping into the wizard again.
 *
 *  When no disk is attached (QEMU launched without -hda) the loader simply
 *  returns "no superblock found" and the wizard runs as before — i.e. the
 *  multi-user system gracefully degrades to in-memory mode.
 * ============================================================================= */
#include "falcon.h"

#define SUPER_SECTORS  4
#define SUPER_BYTES    (SUPER_SECTORS * 512)

typedef struct __attribute__((packed)) {
    u32 magic;          /* FALCONFS_MAGIC                                */
    u32 version;        /* FALCONFS_VERSION                              */
    u32 length;         /* sizeof(settings_t)                            */
    u32 checksum;       /* fletcher-16 over SET payload, zero-extended    */
    u32 reserved[4];
} super_hdr_t;

static bool g_loaded_ok = false;

static u32 fletcher16(const u8 *data, u32 len)
{
    u32 a = 0, b = 0;
    for (u32 i = 0; i < len; i++) {
        a = (a + data[i]) % 255;
        b = (b + a)       % 255;
    }
    return (b << 8) | a;
}

bool diskdb_present(void) { return g_loaded_ok; }

/* --------------------------------------------------------------------------- */
void diskdb_load(void)
{
    g_loaded_ok = false;

    if (ata_probe_count() == 0) return;          /* nothing attached      */

    static u8 buf[SUPER_BYTES];
    if (!ata_read_lba28(0, FALCONFS_SECTOR, buf, SUPER_SECTORS)) return;

    super_hdr_t *h = (super_hdr_t *)buf;
    if (h->magic   != FALCONFS_MAGIC)    return;
    if (h->version != FALCONFS_VERSION)  return;
    if (h->length  != sizeof(settings_t)) return;

    u8 *payload = buf + sizeof(super_hdr_t);
    u32 cs = fletcher16(payload, sizeof(settings_t));
    if (cs != h->checksum) return;

    /* commit */
    k_memcpy(&SET, payload, sizeof(settings_t));
    g_loaded_ok = true;
}

bool diskdb_save(void)
{
    if (ata_probe_count() == 0) return false;

    static u8 buf[SUPER_BYTES];
    k_memset(buf, 0, SUPER_BYTES);

    super_hdr_t *h = (super_hdr_t *)buf;
    h->magic    = FALCONFS_MAGIC;
    h->version  = FALCONFS_VERSION;
    h->length   = sizeof(settings_t);
    u8 *payload = buf + sizeof(super_hdr_t);
    k_memcpy(payload, &SET, sizeof(settings_t));
    h->checksum = fletcher16(payload, sizeof(settings_t));

    return ata_write_lba28(0, FALCONFS_SECTOR, buf, SUPER_SECTORS);
}
