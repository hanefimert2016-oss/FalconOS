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

/* =============================================================================
 *  shfs persistence
 * -----------------------------------------------------------------------------
 *  sh_files[] lives in kernel/apps.c (declared extern in falcon.h).  We
 *  serialise the whole array — every dir entry + every file payload — into
 *  a dedicated disk region right after the settings superblock, with its own
 *  magic + checksum so a freshly formatted disk doesn't get mis-loaded.
 *
 *  Layout:
 *      LBA SHFS_SECTOR + 0     header sector (magic, version, length, csum)
 *      LBA SHFS_SECTOR + 1...   payload (sizeof(sh_files))
 *
 *  Mutating shell commands (mkdir, touch, rm, mv, cp, write) call
 *  shfs_mark_dirty() and the main loop calls shfs_flush_if_dirty() once a
 *  second so the user really does get cold-reboot persistence for any file
 *  or folder they create from Files / Terminal / wget.                   */
typedef struct __attribute__((packed)) {
    u32 magic;       /* SHFS_MAGIC               */
    u32 version;     /* SHFS_VERSION             */
    u32 length;      /* sizeof(sh_files)         */
    u32 checksum;    /* fletcher16 over payload  */
    u32 reserved[4];
} shfs_hdr_t;

#define SHFS_PAYLOAD_BYTES   ((u32)(SH_FILES * sizeof(shfile_t)))
#define SHFS_TOTAL_BYTES     (sizeof(shfs_hdr_t) + SHFS_PAYLOAD_BYTES)
#define SHFS_TOTAL_SECTORS   ((SHFS_TOTAL_BYTES + 511) / 512)

static bool g_shfs_dirty = false;
static u32  g_shfs_last_save_tick = 0;
extern volatile u32 g_ticks;   /* PIT counter, defined in kernel/main.c */

void shfs_mark_dirty(void)
{
    g_shfs_dirty = true;
}

void shfs_load(void)
{
    if (SET.install_disk < 0) return;
    if (SET.install_disk >= ata_probe_count()) return;

    static u8 buf[(SH_FILES * sizeof(shfile_t)) + 512 + 4096];
    k_memset(buf, 0, sizeof buf);

    if (!ata_read_lba28(SET.install_disk, SHFS_SECTOR, buf, SHFS_TOTAL_SECTORS))
        return;

    const shfs_hdr_t *h = (const shfs_hdr_t *)buf;
    if (h->magic   != SHFS_MAGIC)             return;
    if (h->version != SHFS_VERSION)           return;
    if (h->length  != SHFS_PAYLOAD_BYTES)     return;

    const u8 *payload = buf + sizeof(shfs_hdr_t);
    u32 cs = fletcher16(payload, SHFS_PAYLOAD_BYTES);
    if (cs != h->checksum)                    return;

    k_memcpy(sh_files, payload, SHFS_PAYLOAD_BYTES);
    g_shfs_dirty = false;
}

bool shfs_save(void)
{
    if (SET.install_disk < 0) return false;
    if (SET.install_disk >= ata_probe_count()) return false;

    static u8 buf[(SH_FILES * sizeof(shfile_t)) + 512 + 4096];
    k_memset(buf, 0, sizeof buf);

    shfs_hdr_t *h = (shfs_hdr_t *)buf;
    h->magic    = SHFS_MAGIC;
    h->version  = SHFS_VERSION;
    h->length   = SHFS_PAYLOAD_BYTES;

    u8 *payload = buf + sizeof(shfs_hdr_t);
    k_memcpy(payload, sh_files, SHFS_PAYLOAD_BYTES);
    h->checksum = fletcher16(payload, SHFS_PAYLOAD_BYTES);

    bool ok = ata_write_lba28(SET.install_disk, SHFS_SECTOR, buf,
                              SHFS_TOTAL_SECTORS);
    if (ok) {
        g_shfs_dirty = false;
        g_shfs_last_save_tick = g_ticks;
    }
    return ok;
}

void shfs_flush_if_dirty(void)
{
    if (!g_shfs_dirty) return;
    /* Coalesce: defer writes to once a second so a flurry of touch/mkdir
     * commands maps to a single ATA write.                              */
    if ((u32)(g_ticks - g_shfs_last_save_tick) < 100) return;
    (void)shfs_save();
}
