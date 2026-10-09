/* FalconOS: fail-closed persistence in a dedicated MBR partition.
 * NO DATA IS EVER WRITTEN TO THE DISK'S MBR (LBA0).
 * Only an existing primary partition of type 0xFA may hold settings.
 * GPT protective MBRs, overlapping and malformed layouts are rejected.
 */
#include "falcon.h"

#define SUPER_SECTORS  4u
#define SUPER_BYTES    (SUPER_SECTORS * 512u)
#define MBR_PART_OFF   446u
#define MBR_PART_SIZE  16u
#define MBR_COUNT      4u
#define MIN_START_LBA  2048u
#define MIN_PART_LBA   8u
#define LBA28_LIMIT    0x10000000ull

typedef struct __attribute__((packed)) {
    u32 magic;
    u32 version;
    u32 length;
    u32 checksum;
    u32 reserved[4];
} super_hdr_t;

typedef char superblock_must_fit[
    (sizeof(settings_t) <= SUPER_BYTES - sizeof(super_hdr_t)) ? 1 : -1
];

static bool g_loaded_ok = false;

static u32 read_le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) |
           ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u32 fletcher16(const u8 *data, u32 len)
{
    u32 a = 0, b = 0;
    for (u32 i = 0; i < len; i++) {
        a = (a + data[i]) % 255u;
        b = (b + a) % 255u;
    }
    return (b << 8) | a;
}

/* Find exactly one non-overlapping, explicitly dedicated partition. */
static bool target_partition(i32 disk, u32 *start_lba)
{
    if (disk < 0 || disk >= ata_probe_count() || !start_lba) return false;

    u64 disk_end = ata_sectors(disk);
    if (disk_end > LBA28_LIMIT) disk_end = LBA28_LIMIT;
    if (disk_end <= MIN_START_LBA + MIN_PART_LBA) return false;

    u8 mbr[512];
    if (!ata_read_lba28(disk, 0, mbr, 1)) return false;
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) return false;

    u64 found_start = 0, found_end = 0;
    u32 found = 0;
    for (u32 i = 0; i < MBR_COUNT; i++) {
        const u8 *p = mbr + MBR_PART_OFF + i * MBR_PART_SIZE;
        u8 type = p[4];
        if (type == 0xEE) return false; /* GPT protective MBR */
        if (type != FALCONFS_PARTITION_TYPE) continue;
        u32 lba = read_le32(p + 8);
        u32 sectors = read_le32(p + 12);
        u64 end = (u64)lba + (u64)sectors;
        if (++found != 1 || (p[0] != 0 && p[0] != 0x80) ||
            lba < MIN_START_LBA || sectors < MIN_PART_LBA ||
            end > disk_end || end <= lba) return false;
        found_start = lba;
        found_end = end;
    }
    if (found != 1) return false;

    for (u32 i = 0; i < MBR_COUNT; i++) {
        const u8 *p = mbr + MBR_PART_OFF + i * MBR_PART_SIZE;
        if (p[4] == 0 || p[4] == FALCONFS_PARTITION_TYPE) continue;
        u64 start = read_le32(p + 8);
        u64 n = read_le32(p + 12);
        if (n && start < found_end && found_start < start + n)
            return false;
    }
    *start_lba = (u32)found_start;
    return true;
}

bool diskdb_target_available(i32 disk)
{
    u32 lba;
    return target_partition(disk, &lba);
}

bool diskdb_present(void) { return g_loaded_ok; }

static bool validate_and_import(const u8 *buf)
{
    const super_hdr_t *h = (const super_hdr_t *)buf;
    if (h->magic != FALCONFS_MAGIC || h->version != FALCONFS_VERSION ||
        h->length != sizeof(settings_t)) return false;

    const u8 *payload = buf + sizeof(super_hdr_t);
    if (fletcher16(payload, sizeof(settings_t)) != h->checksum) return false;
    k_memcpy(&SET, payload, sizeof(settings_t));
    return true;
}

void diskdb_load(void)
{
    g_loaded_ok = false;
    static u8 buf[SUPER_BYTES];
    for (i32 disk = 0; disk < ata_probe_count(); disk++) {
        u32 lba;
        if (!target_partition(disk, &lba)) continue;
        if (!ata_read_lba28(disk, lba, buf, SUPER_SECTORS)) continue;
        if (validate_and_import(buf)) {
            SET.install_disk = disk;
            g_loaded_ok = true;
            return;
        }
    }
}

bool diskdb_save(void)
{
    if (SET.install_disk < 0) return false;
    u32 lba;
    if (!target_partition(SET.install_disk, &lba)) return false;

    static u8 buf[SUPER_BYTES];
    k_memset(buf, 0, SUPER_BYTES);
    super_hdr_t *h = (super_hdr_t *)buf;
    h->magic = FALCONFS_MAGIC;
    h->version = FALCONFS_VERSION;
    h->length = sizeof(settings_t);
    u8 *payload = buf + sizeof(super_hdr_t);
    k_memcpy(payload, &SET, sizeof(settings_t));
    h->checksum = fletcher16(payload, sizeof(settings_t));
    return ata_write_lba28(SET.install_disk, lba, buf, SUPER_SECTORS);
}
