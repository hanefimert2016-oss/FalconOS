/* FalconOS XFS1 — durable large-object extension, 32 x 32-KiB files.
 * Explicitly separate from old PFS1 layout; old SHFS records remain intact.
 * Each slot has two complete generations, 66 sectors per generation.
 * Data sectors go out first; header sector commits last. A crash before
 * the header is written cannot replace the previous verified generation.
 *
 * Operates ONLY on an existing validated MBR type-0xFA partition through
 * diskdb_store_io(), which revalidates partition boundaries on each I/O.
 * This is NOT a block allocator or a general journaled filesystem.
 */
#include "falcon.h"
#define XFS_SLOTS 32u
#define XFS_START 4096u
#define XFS_SECTORS 66u
#define XFS_BYTES (XFS_SECTORS*512u)
#define XFS_PAYLOAD 512u
#define XFS_MAX_BYTES 32768u
#define XFS_END (XFS_START+XFS_SLOTS*2u*XFS_SECTORS)
#define XFS_NAME 56u
typedef char xfs_record_size_check[(XFS_PAYLOAD+XFS_MAX_BYTES<=XFS_BYTES)?1:-1];

typedef struct {
    u32 generation, length;
    u8 bank, used, exists;
    char name[XFS_NAME];
} xfs_index_t;
static xfs_index_t slots[XFS_SLOTS];
static u8 record[XFS_BYTES];
static u8 digest_input[96u+XFS_MAX_BYTES];
static bool active;
static u32 invalid_copies;

static void put32(u8 *p,u32 n) {
    p[0]=(u8)n;p[1]=(u8)(n>>8);p[2]=(u8)(n>>16);p[3]=(u8)(n>>24);
}
static u32 pull32(const u8 *p){
    return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24);
}
static bool filename_ok(const char *name){
    if(!name||!name[0])return false;
    for(u32 i=0;name[i];i++){
        u8 c=(u8)name[i];
        if(i>=XFS_NAME-1 || c=='.' && name[i+1]=='.')return false;
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
             (c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'))return false;
    }
    return true;
}
static u32 sector(i32 slot,u32 bank){
    return XFS_START+((u32)slot*2u+bank)*XFS_SECTORS;
}
static bool sectors_io(i32 slot,u32 bank,bool write){
    u32 location=sector(slot,bank);
    if(write){
        /* Never publish a new header until all trailing sectors reached disk. */
        for(u32 i=10;i<XFS_SECTORS;i+=10){
            u32 n=XFS_SECTORS-i;if(n>10)n=10;
            if(!diskdb_store_io(location+i,record+i*512u,n,true))return false;
        }
        return diskdb_store_io(location,record,10u,true);
    }
    for(u32 i=0;i<XFS_SECTORS;i+=10){
        u32 n=XFS_SECTORS-i;if(n>10)n=10;
        if(!diskdb_store_io(location+i,record+i*512u,n,false))return false;
    }
    return true;
}
static void hash_record(u32 bytes,u8 out[32]){
    k_memcpy(digest_input,record,96);
    if(bytes)k_memcpy(digest_input+96,record+XFS_PAYLOAD,bytes);
    sha256_hash(digest_input,96+bytes,out);
}
static bool valid(i32 slot){
    if(record[0]!='X'||record[1]!='F'||record[2]!='S'||record[3]!='1'
       ||pull32(record+8)!=(u32)slot)return false;
    u32 n=pull32(record+12);
    bool used=record[72]==1u;
    if(record[72]>1 || n>XFS_MAX_BYTES || (!used && n))return false;
    if(used&&!filename_ok((const char *)(record+16)))return false;
    if(!used && record[16]!=0)return false;
    u8 hash[32],difference=0;
    hash_record(n,hash);
    for(u32 i=0;i<32;i++)difference|=hash[i]^record[96+i];
    return difference==0;
}
bool xfs_mount(void){
    active=false;invalid_copies=0;
    k_memset(slots,0,sizeof slots);
    if(SET.install_disk<0 || !diskdb_store_io(XFS_END-1u,record,1u,false))
        return false;
    for(i32 slot=0;slot<(i32)XFS_SLOTS;slot++){
        for(u32 bank=0;bank<2;bank++){
            if(!diskdb_store_io(sector(slot,bank),record,1u,false))
                continue;
            if(record[0]!='X'||record[1]!='F'||record[2]!='S'||record[3]!='1')
                continue;
            if(!sectors_io(slot,bank,false) || !valid(slot)){
                invalid_copies++;
                continue;
            }
            u32 seq=pull32(record+4);
            xfs_index_t *entry=&slots[slot];
            if(!entry->exists||(i32)(seq-entry->generation)>0){
                entry->exists=1;entry->generation=seq;
                entry->bank=(u8)bank;
                entry->used=record[72];
                entry->length=pull32(record+12);
                k_memcpy(entry->name,record+16,XFS_NAME);
            }
        }
    }
    active=true;
    return true;
}
bool xfs_ready(void){return active;}
u32 xfs_max_size(void){return XFS_MAX_BYTES;}
u32 xfs_capacity(void){return XFS_SLOTS;}
u32 xfs_corrupt_copies(void){return invalid_copies;}
static i32 find(const char *name){
    for(i32 i=0;i<(i32)XFS_SLOTS;i++)
        if(slots[i].exists && slots[i].used &&
           k_strcmp(slots[i].name,name)==0)return i;
    return -1;
}
i32 xfs_read(const char *name,u8 *out,u32 capacity){
    if(!active||!filename_ok(name)||!out)return -1;
    i32 slot=find(name);
    if(slot<0)return -1;
    xfs_index_t *entry=&slots[slot];
    if(capacity<entry->length)return -1;
    if(!sectors_io(slot,entry->bank,false) || !valid(slot) ||
       pull32(record+4)!=entry->generation)return -1;
    if(entry->length)k_memcpy(out,record+XFS_PAYLOAD,entry->length);
    return (i32)entry->length;
}
static bool commit(i32 slot,const char *name,const u8 *data,u32 n){
    xfs_index_t *entry=&slots[slot];
    u32 next=entry->exists ? (entry->bank^1u) : 0u;
    u32 seq=entry->generation+1;if(!seq)seq=1;
    k_memset(record,0,sizeof record);
    record[0]='X';record[1]='F';record[2]='S';record[3]='1';
    put32(record+4,seq);put32(record+8,(u32)slot);put32(record+12,n);
    if(name){k_strcpy((char *)(record+16),name);record[72]=1;}
    if(n)k_memcpy(record+XFS_PAYLOAD,data,n);
    hash_record(n,record+96);
    if(!sectors_io(slot,next,true))return false;
    /* In-memory index is advanced only after committed disk write. */
    entry->generation=seq;entry->bank=(u8)next;
    entry->exists=1;entry->used=name?1:0;entry->length=n;
    if(name)k_strcpy(entry->name,name);
    else entry->name[0]=0;
    return true;
}
bool xfs_write(const char *name,const u8 *data,u32 n){
    if(!active||!filename_ok(name)||n>XFS_MAX_BYTES || (n&&!data))
        return false;
    i32 slot=find(name);
    if(slot<0)for(i32 i=0;i<(i32)XFS_SLOTS;i++){
        if(!slots[i].used){slot=i;break;}
    }
    return slot>=0 && commit(slot,name,data,n);
}
bool xfs_remove(const char *name){
    if(!active||!filename_ok(name))return false;
    i32 slot=find(name);
    return slot>=0&&commit(slot,NULL,NULL,0);
}
u32 xfs_count(void){
    u32 n=0;
    for(u32 i=0;i<XFS_SLOTS;i++)if(slots[i].used)n++;
    return n;
}
/* Non-destructive fsck: count corrupt copies and valid logical files.
 * Repairs are intentionally never automatic; both banks may contain valid
 * older states and should be preserved until user explicitly authorizes.
 */
bool xfs_fsck(u32 *files,u32 *bad_copies){
    if(!xfs_mount())return false;
    if(files)*files=xfs_count();
    if(bad_copies)*bad_copies=invalid_copies;
    return true;
}
