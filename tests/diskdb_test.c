/* Host-side FalconOS storage tests; all ATA operations are in-memory mocks. */
#include "falcon.h"
#define DISK_SECTORS 4096u
static u8 disk[DISK_SECTORS * 512u];
static u32 writes, last_lba;
settings_t SET;

void k_memcpy(void *dst, const void *src, u32 n) {
    u8 *d = dst; const u8 *s = src;
    for (u32 i = 0; i < n; i++) d[i] = s[i];
}
void k_memset(void *dst, u8 val, u32 n) {
    u8 *p = dst; for (u32 i = 0; i < n; i++) p[i] = val;
}
i32 ata_probe_count(void) { return 1; }
u64 ata_sectors(i32 d) { return d == 0 ? DISK_SECTORS : 0; }
bool ata_read_lba28(i32 d, u32 lba, u8 *out, u32 count) {
    if (d != 0 || (u64)lba + count > DISK_SECTORS) return false;
    k_memcpy(out, disk + lba * 512u, count * 512u); return true;
}
bool ata_write_lba28(i32 d, u32 lba, const u8 *in, u32 count) {
    if (d != 0 || (u64)lba + count > DISK_SECTORS) return false;
    k_memcpy(disk + lba * 512u, in, count * 512u);
    last_lba = lba; writes++; return true;
}
static void put32(u8 *p, u32 x) {
    for (i32 j = 0; j < 4; j++) p[j] = (u8)(x >> (8 * j));
}
static void reset(void) {
    k_memset(disk, 0, sizeof(disk));
    k_memset(&SET, 0, sizeof(SET));
    SET.install_disk = 0; writes = last_lba = 0;
}
static void part(u32 idx, u8 kind, u32 start, u32 length) {
    u8 *p = disk + 446u + idx * 16u;
    p[4] = kind;
    put32(p + 8, start);
    put32(p + 12, length);
    disk[510] = 0x55; disk[511] = 0xAA;
}
int main(void) {
    reset();
    if (diskdb_save() || writes) return 1;
    part(0, 0xEE, 2048, 1000);
    if (diskdb_target_available(0) || diskdb_save()) return 2;
    reset(); part(0, 0xFA, 1, 16);
    if (diskdb_save()) return 3;
    reset(); part(0, 0xFA, 2048, 8); part(1, 0x83, 2050, 200);
    if (diskdb_save()) return 4;
    reset(); part(0, 0xFA, 2048, 5000);
    if (diskdb_save()) return 5;
    reset(); part(0, 0xFA, 2048, 100); part(1, 0xFA, 3072, 16);
    if (diskdb_save()) return 6;
    reset(); part(0, 0xFA, 2048, 100);
    if (!diskdb_target_available(0)) return 7;
    SET.install_disk = -1;
    if (diskdb_save() || writes) return 8;
    SET.install_disk = 0;
    SET.installed = true;
    SET.theme = THEME_LIQUID;
    if (!diskdb_save() || writes != 1 || last_lba != 2048) return 9;
    if (disk[510] != 0x55 || disk[511] != 0xAA || disk[450] != 0xFA) return 10;
    SET.installed = false;
    SET.theme = THEME_DARK;
    diskdb_load();
    if (!diskdb_present() || !SET.installed ||
        SET.theme != THEME_LIQUID || SET.install_disk != 0) return 11;
    disk[2048 * 512u] ^= 0x10;
    SET.installed = false;
    diskdb_load();
    if (diskdb_present() || SET.installed) return 12;
    reset();
    part(0, 0xFA, 2048, 100);
    u8 blob[5120];
    k_memset(blob, 0xBA, sizeof(blob));
    if (!diskdb_store_io(16, blob, 10, true) || last_lba != 2064) return 13;
    if (disk[510] != 0x55 || disk[450] != 0xFA) return 14;
    part(0, 0xFA, 2048, 20);
    if (diskdb_store_io(16, blob, 10, true)) return 15; /* crosses partition */
    SET.install_disk = -1;
    if (diskdb_store_io(16, blob, 1, true)) return 16;
    return 0;
}
