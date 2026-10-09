/* FalconFS PFS1: copy-on-write durable SHFS records.
 *
 * The existing vetted MBR type-0xFA partition is the ONLY disk target.
 * Partition-relative sectors [1024,2304) are reserved for PFS1:
 * 64 logical slots, 2 copies per slot, 10 sectors (5120 bytes) each.
 * Every update writes a complete alternate record with a generation and
 * SHA-256, then makes it the current copy. A torn write leaves the older
 * copy available at next mount. This is not a general journaling FS.
 *
 * No disk is formatted or partitioned here. A selected partition must
 * be large enough for these reserved sectors or PFS stays disabled.
 */
#include "falcon.h"
#include "shfs.h"

#define PFS_OFFSET 1024u
#define PFS_SECTORS 10u
#define PFS_COPIES 2u
#define PFS_RECORD_BYTES (PFS_SECTORS * 512u)
#define PFS_DATA_OFFSET 128u
#define PFS_LIMIT 4096u
#define PFS_LAST (PFS_OFFSET + SHFS_MAX_ENTRIES * PFS_COPIES * PFS_SECTORS)
typedef char pfs_record_fits[(PFS_DATA_OFFSET + PFS_LIMIT <= PFS_RECORD_BYTES) ? 1 : -1];

static u8 record[PFS_RECORD_BYTES];
static u8 hash_input[96 + PFS_LIMIT];
static u32 generations[SHFS_MAX_ENTRIES];
static u32 known_revision[SHFS_MAX_ENTRIES];
static u8 active_copy[SHFS_MAX_ENTRIES];
static i32 cursor;
static bool mounted;

static void set32(u8 *p, u32 x) {
    p[0]=(u8)x; p[1]=(u8)(x>>8); p[2]=(u8)(x>>16); p[3]=(u8)(x>>24);
}
static u32 get32(const u8 *p) {
    return (u32)p[0] | ((u32)p[1]<<8) | ((u32)p[2]<<16) | ((u32)p[3]<<24);
}
static u32 location(i32 slot, u32 bank) {
    return PFS_OFFSET + ((u32)slot * 2u + bank) * PFS_SECTORS;
}
static bool record_digest_ok(u32 size) {
    u8 actual[32], mismatch=0;
    k_memcpy(hash_input, record, 76);
    k_memcpy(hash_input+76, record+108, 20 + size);
    sha256_hash(hash_input, 96 + size, actual);
    for (u32 i=0;i<32;i++) mismatch |= (u8)(actual[i] ^ record[76+i]);
    return mismatch==0;
}
static void record_digest(u32 size) {
    k_memcpy(hash_input, record, 76);
    k_memcpy(hash_input+76, record+108, 20 + size);
    sha256_hash(hash_input, 96 + size, record+76);
}
static bool record_valid(i32 slot) {
    if (record[0]!='P'||record[1]!='F'||record[2]!='S'||record[3]!='1') return false;
    if (get32(record+8)!=(u32)slot) return false;
    u32 type=record[12], length=get32(record+16);
    if (type>2 || length>PFS_LIMIT || (type!=1 && length)) return false;
    if (type) {
        bool terminated=false;
        if (record[20]!='/') return false;
        for (u32 j=0;j<SHFS_PATH;j++) if (!record[20+j]) { terminated=true; break; }
        if (!terminated) return false;
    }
    for (u32 i=0;i<length;i++) {
        u8 c=record[PFS_DATA_OFFSET+i];
        if (!c || c=='\r') return false;
    }
    return record_digest_ok(length);
}
void pfs_mount(void) {
    mounted=false;
    if (SET.install_disk<0) return;
    /* Check capacity at the actual highest PFS address before any write. */
    if (!diskdb_store_io(PFS_LAST-1u,record,1u,false)) return;
    shfs_init();
    for (i32 i=0;i<SHFS_MAX_ENTRIES;i++) {
        generations[i]=0;
        known_revision[i]=shfs_slot_revision(i);
        active_copy[i]=1;
        bool found=false;
        u32 selected_seq=0;
        static u8 chosen[PFS_RECORD_BYTES];
        for (u32 bank=0;bank<2;bank++) {
            if (!diskdb_store_io(location(i,bank),record,1u,false)) continue;
            if (record[0]!='P'||record[1]!='F'||record[2]!='S'||record[3]!='1') continue;
            if (get32(record+8)!=(u32)i || record[12]>2) continue;
            if (!diskdb_store_io(location(i,bank),record,PFS_SECTORS,false)) continue;
            if (!record_valid(i)) continue;
            u32 seq=get32(record+4);
            if (!found || (i32)(seq-selected_seq)>0) {
                found=true; selected_seq=seq; active_copy[i]=(u8)bank;
                k_memcpy(chosen,record,PFS_RECORD_BYTES);
            }
        }
        if (!found) continue;
        generations[i]=selected_seq;
        k_memcpy(record,chosen,PFS_RECORD_BYTES);
        if (record[12]==0) shfs_replay_deleted(i);
        else if (!shfs_replay_slot(i,(const char *)(record+20),
                                   record[12]==2,(const char *)(record+PFS_DATA_OFFSET),
                                   get32(record+16))) {
            /* Invalid path on the chosen generation: ignore, never overwrite it. */
            continue;
        }
        known_revision[i]=shfs_slot_revision(i);
    }
    cursor=0;
    mounted=true;
}
bool pfs_mounted(void) { return mounted; }
void pfs_sync_step(void) {
    if (!mounted) return;
    for (i32 scanned=0;scanned<SHFS_MAX_ENTRIES;scanned++) {
        i32 i=cursor;
        cursor=(cursor+1)%SHFS_MAX_ENTRIES;
        u32 rev=shfs_slot_revision(i);
        if (rev==known_revision[i]) continue;
        shfs_ent_t *f=shfs_slot(i);
        if (!f || f->len>PFS_LIMIT) return;
        k_memset(record,0,PFS_RECORD_BYTES);
        record[0]='P';record[1]='F';record[2]='S';record[3]='1';
        u32 seq=generations[i]+1;
        if (!seq) seq=1;
        set32(record+4,seq);
        set32(record+8,(u32)i);
        record[12]= f->used ? (f->is_dir ? 2u : 1u) : 0u;
        u32 size=(f->used && !f->is_dir) ? f->len : 0u;
        set32(record+16,size);
        if (f->used) k_memcpy(record+20,f->path,SHFS_PATH);
        if (size) k_memcpy(record+PFS_DATA_OFFSET,f->data,size);
        record_digest(size);
        u32 next=active_copy[i]^1u;
        if (diskdb_store_io(location(i,next),record,PFS_SECTORS,true)) {
            known_revision[i]=rev;
            generations[i]=seq;
            active_copy[i]=(u8)next;
        }
        return; /* At most one 5-KiB record per frame. */
    }
}
bool pfs_flush_all(void) {
    if (!mounted) return false;
    for (i32 attempt=0;attempt<SHFS_MAX_ENTRIES;attempt++) pfs_sync_step();
    for (i32 i=0;i<SHFS_MAX_ENTRIES;i++)
        if (known_revision[i]!=shfs_slot_revision(i)) return false;
    return true;
}
