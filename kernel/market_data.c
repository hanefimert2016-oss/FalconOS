/* FalconOS Marketplace: persistent, integrity-checked 0xFA partition slots.
 * This is NOT a general disk filesystem. It is 12 fixed-size, safely
 * bounded records for <=4095-byte interpreted FAPP/1 packages.
 */
#include "falcon.h"
#include "shfs.h"

#define MARKET_SLOTS 12
#define SLOT_SECTORS 10u
#define SLOT_BASE 16u
#define RECORD_BYTES (SLOT_SECTORS * 512u)
#define PACKAGE_OFFSET 80u

static u8 REC[RECORD_BYTES];
static bool id_ok(const char *s)
{
    i32 n = 0;
    if (s[0] < 'a' || s[0] > 'z') return false;
    for (; s[n]; n++) {
        char c = s[n];
        if (n >= 32 || !((c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-')) return false;
    }
    return n >= 2;
}
static u32 len_from_record(const u8 *p)
{
    return (u32)p[4] | ((u32)p[5] << 8) |
           ((u32)p[6] << 16) | ((u32)p[7] << 24);
}
static bool valid_record(void)
{
    if (REC[0] != 'F' || REC[1] != 'P' || REC[2] != 'K' ||
        REC[3] != '1') return false;
    u32 n = len_from_record(REC);
    if (n < 70 || n >= SHFS_FBYTES || n > RECORD_BYTES - PACKAGE_OFFSET)
        return false;
    if (REC[8] < 'a' || REC[8] > 'z') return false;
    /* Reserved byte at index 40 terminates the stored identifier. */
    if (REC[40] != 0) return false;
    for (u32 i = 0; i < n; i++) {
        u8 b = REC[PACKAGE_OFFSET + i];
        if (b == 0 || b == '\r' || b > 127) return false;
    }
    char id[33];
    for (i32 i = 0; i < 33; i++) id[i] = (char)REC[8 + i];
    id[32] = 0;
    if (!id_ok(id)) return false;
    u8 hash[32];
    sha256_hash(REC + PACKAGE_OFFSET, n, hash);
    u8 different = 0;
    for (i32 i = 0; i < 32; i++) different |= (hash[i] ^ REC[41 + i]);
    if (different) return false;

    /* Verify the package ID in the internal FAPP/1 header. */
    const char *src = (const char *)(REC + PACKAGE_OFFSET);
    if (k_strncmp(src, "FAPP/1\nid=", 10) != 0) return false;
    i32 idlen = k_strlen(id);
    if (k_strncmp(src + 10, id, idlen) != 0 ||
        src[10 + idlen] != '\n') return false;
    if (REC[PACKAGE_OFFSET + n - 1] != '\n') return false;
    /* Package should contain bounded, allowlisted script lines. */
    i32 body = -1;
    for (u32 i = 0; i + 1 < n; i++) {
        if (src[i] == '\n' && src[i+1] == '\n') {
            body = (i32)i + 2; break;
        }
    }
    if (body < 0) return false;
    for (u32 start = body; start < n;) {
        u32 end = start;
        while (end < n && src[end] != '\n') end++;
        if (end == n) return false;
        if (end > start && src[start] != '#' &&
            !market_line_allowed(src + start, (i32)(end - start)))
            return false;
        start = end + 1;
    }
    return true;
}
static void path_for(char out[SHFS_PATH], const char *id)
{
    k_strcpy(out, "/home/falcon/apps/");
    k_strcat(out, id);
    k_strcat(out, ".pkg");
}
void market_disk_restore(void)
{
    if (SET.install_disk < 0) return;
    shfs_init();
    (void)shfs_mkdir_abs("/home/falcon/apps");
    for (u32 slot = 0; slot < MARKET_SLOTS; slot++) {
        if (!diskdb_store_io(SLOT_BASE + slot * SLOT_SECTORS,
                             REC, SLOT_SECTORS, false)) continue;
        if (!valid_record()) continue;
        char id[33];
        k_memcpy(id, REC + 8, 33); id[32] = 0;
        char path[SHFS_PATH]; path_for(path, id);
        shfs_ent_t *file = shfs_open_w_abs(path, false);
        if (!file) continue;
        u32 n = len_from_record(REC);
        k_memcpy(file->data, REC + PACKAGE_OFFSET, n);
        file->data[n] = 0;
        file->len = n;
    }
}
bool market_disk_save(const char *id, const char *pkg, u32 length)
{
    if (!id || !id_ok(id) || !pkg || length < 70 || length >= SHFS_FBYTES ||
        length > RECORD_BYTES - PACKAGE_OFFSET || SET.install_disk < 0)
        return false;
    u32 free_slot = MARKET_SLOTS;
    for (u32 i = 0; i < MARKET_SLOTS; i++) {
        if (!diskdb_store_io(SLOT_BASE + i * SLOT_SECTORS, REC,
                             SLOT_SECTORS, false)) return false;
        if (REC[0] == 'F' && REC[1] == 'P' && REC[2] == 'K' &&
            REC[3] == '1') {
            char saved[33];
            k_memcpy(saved, REC + 8, 32);
            saved[32] = 0; /* bounded comparison even on corrupt disk */
            if (id_ok(saved) && k_strcmp(saved, id) == 0) {
                free_slot = i; break;
            }
        } else if (free_slot == MARKET_SLOTS) {
            free_slot = i;
        }
    }
    if (free_slot == MARKET_SLOTS) return false;
    k_memset(REC, 0, sizeof REC);
    REC[0] = 'F'; REC[1] = 'P'; REC[2] = 'K'; REC[3] = '1';
    for (i32 i = 0; i < 4; i++) REC[4+i] = (u8)(length >> (i*8));
    k_memcpy(REC + 8, id, k_strlen(id));
    sha256_hash((const u8 *)pkg, length, REC + 41);
    k_memcpy(REC + PACKAGE_OFFSET, pkg, length);
    return diskdb_store_io(SLOT_BASE + free_slot * SLOT_SECTORS,
                           REC, SLOT_SECTORS, true);
}

/* Remove only the named record in the validated dedicated 0xFA partition. */
bool market_disk_delete(const char *id)
{
    if (!id || !id_ok(id) || SET.install_disk < 0) return false;
    for (u32 slot = 0; slot < MARKET_SLOTS; slot++) {
        if (!diskdb_store_io(SLOT_BASE + slot * SLOT_SECTORS,
                             REC, SLOT_SECTORS, false)) return false;
        if (REC[0] != 'F' || REC[1] != 'P' || REC[2] != 'K' ||
            REC[3] != '1') continue;
        char saved[33];
        k_memcpy(saved, REC + 8, 32);
        saved[32] = 0;
        if (!id_ok(saved) || k_strcmp(saved, id) != 0) continue;
        k_memset(REC, 0, sizeof REC);
        return diskdb_store_io(SLOT_BASE + slot * SLOT_SECTORS,
                               REC, SLOT_SECTORS, true);
    }
    return false;
}
