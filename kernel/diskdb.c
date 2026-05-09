/* =============================================================================
 *  FalconOS — disk-backed settings ("FalconFS" superblock, v5+)
 * -----------------------------------------------------------------------------
 *  We persist the entire `settings_t` (including hashed user records) to the
 *  first four 512-byte sectors of the chosen ATA device.
 *
 *      LBA0 .. LBA3   (2048 bytes)   "FalconFS" superblock
 *
 *  load:  scans every probed ATA disk (0..N-1) and imports the first sector
 *         set that validates (magic, version, checksum).
 *  save:  honours SET.install_disk (>=0).  SET.install_disk == -1 means the
 *         user picked “güvenli çalıştırma” — we skip all writes (no host USB
 *         / disk persistence for that session profile).
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

static bool validate_and_import(const u8 *buf)
{
    const super_hdr_t *h = (const super_hdr_t *)buf;
    if (h->magic   != FALCONFS_MAGIC)    return false;
    if (h->version != FALCONFS_VERSION)  return false;
    if (h->length  != sizeof(settings_t)) return false;

    const u8 *payload = buf + sizeof(super_hdr_t);
    u32 cs = fletcher16(payload, sizeof(settings_t));
    if (cs != h->checksum) return false;

    k_memcpy(&SET, payload, sizeof(settings_t));
    return true;
}

void diskdb_load(void)
{
    g_loaded_ok = false;

    i32 nd = ata_probe_count();
    if (nd <= 0) return;

    static u8 buf[SUPER_BYTES];

    for (i32 d = 0; d < nd; d++) {
        if (!ata_read_lba28(d, FALCONFS_SECTOR, buf, SUPER_SECTORS)) continue;
        if (validate_and_import(buf)) {
            g_loaded_ok = true;
            return;
        }
    }
}

bool diskdb_save(void)
{
    /* güvenli çalıştırma — deliberately stateless across cold boots        */
    if (SET.install_disk < 0)
        return false;

    if (SET.install_disk >= ata_probe_count())
        return false;

    static u8 buf[SUPER_BYTES];
    k_memset(buf, 0, SUPER_BYTES);

    super_hdr_t *h = (super_hdr_t *)buf;
    h->magic    = FALCONFS_MAGIC;
    h->version  = FALCONFS_VERSION;
    h->length   = sizeof(settings_t);
    u8 *payload = buf + sizeof(super_hdr_t);
    k_memcpy(payload, &SET, sizeof(settings_t));
    h->checksum = fletcher16(payload, sizeof(settings_t));

    return ata_write_lba28(SET.install_disk, FALCONFS_SECTOR, buf, SUPER_SECTORS);
}
