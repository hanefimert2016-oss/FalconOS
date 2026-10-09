/* Tests persisted FAPP/1 records using fake ATA partition storage. */
#include "falcon.h"
#include "shfs.h"
#include <stdio.h>
#include <string.h>
#define DISK_SECTORS 160u
static u8 disk[DISK_SECTORS * 512u];
settings_t SET;
i32 k_strlen(const char *s) { return (i32)strlen(s); }
i32 k_strcmp(const char *a, const char *b) { return strcmp(a, b); }
i32 k_strncmp(const char *a, const char *b, i32 n) { return strncmp(a, b, (size_t)n); }
char *k_strcpy(char *a, const char *b) { return strcpy(a, b); }
char *k_strcat(char *a, const char *b) { return strcat(a, b); }
void k_memcpy(void *a, const void *b, u32 n) { memcpy(a, b, n); }
void k_memset(void *a, u8 v, u32 n) { memset(a, v, n); }
void k_itoa(u32 value, char *buf, i32 base) {
    (void)base;
    snprintf(buf, 16, "%u", value);
}
bool diskdb_store_io(u32 offset, u8 *buffer, u32 sectors, bool write) {
    if (SET.install_disk < 0 || offset < 16 || sectors == 0 ||
        offset + sectors > DISK_SECTORS) return false;
    if (write) memcpy(disk + offset * 512u, buffer, sectors * 512u);
    else memcpy(buffer, disk + offset * 512u, sectors * 512u);
    return true;
}
void sha256_hash(const u8 *data, u32 len, u8 out[32]) {
    memset(out, 0, 32);
    for (u32 i = 0; i < len; i++) out[i % 32] ^= (u8)(data[i] + (u8)i);
}
bool market_line_allowed(const char *line, i32 size) {
    return size >= 5 && strncmp(line, "echo ", 5) == 0;
}
int main(void) {
    const char *package =
        "FAPP/1\nid=hello-world\nname=Hello World\nversion=1.0.0\n"
        "summary=Test application\n\necho hello\n";
    SET.install_disk = 0;
    if (!market_disk_save("hello-world", package, (u32)strlen(package))) return 1;
    if (disk[16 * 512u] != 'F') return 2;
    market_disk_restore();
    shfs_ent_t *f = shfs_lookup("/home/falcon/apps/hello-world.pkg");
    if (!f || f->len != strlen(package) || strcmp(f->data, package)) return 3;
    if (!shfs_rm_abs("/home/falcon/apps/hello-world.pkg")) return 4;
    if (!market_disk_delete("hello-world")) return 7;
    if (disk[16 * 512u] != 0) return 8;
    if (!market_disk_save("hello-world", package, (u32)strlen(package))) return 9;
    disk[16 * 512u + 80] ^= 1; /* tampered SHA-256 input */
    market_disk_restore();
    if (shfs_lookup("/home/falcon/apps/hello-world.pkg")) return 5;
    SET.install_disk = -1;
    if (market_disk_save("hello-world", package, (u32)strlen(package))) return 6;
    puts("PASS marketplace save, load, delete, tamper rejection, secure mode");
    return 0;
}
