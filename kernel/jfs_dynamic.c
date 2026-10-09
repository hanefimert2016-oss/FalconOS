/* FalconOS JFS2 — append-only, dynamically allocated and replayed journal.
 *
 * Compatible with PFS1/XFS1: sectors 16384..32767 belong exclusively to JFS2,
 * inside an already-validated MBR type 0xFA partition. No partitioning.
 *
 * Every transaction: 512-byte intent header, variable sectors of data,
 * 512-byte COMMIT footer with sequence + SHA256. ATA writes flush between
 * stages. A crash before COMMIT never publishes new metadata. On reboot,
 * replay stops at the first incomplete transaction and preserves prior files.
 *
 * This bounded 8-MiB log is deliberately not yet a compacting POSIX FS:
 * 128 paths, <=128 KiB/file and no garbage collection. Exhaustion fails
 * closed, never wraps around or overwrites old committed data.
 */
#include "falcon.h"
#define JFS_BEGIN 16384u
#define JFS_SECTORS 16384u
#define JFS_LIMIT (128u*1024u)
#define JFS_MAX_FILES 128u
#define JFS_PATH 56u
#define JFS_INTENT 0x324E524Au /* JRN2 */
#define JFS_COMMIT 0x4D4F434Au /* JCOM */
typedef struct {
    char path[JFS_PATH];
    u32 start,length,sequence;
    bool present;
} jfs_entry_t;
static jfs_entry_t entries[JFS_MAX_FILES];
static u32 next_sector,seq,invalid_records;
static bool mounted;
static u8 header[512],footer[512],payload[JFS_LIMIT],hash_buffer[96+JFS_LIMIT];

static u32 get32(const u8 *p){
    return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24);
}
static void put32(u8 *p,u32 v){
    for(u32 i=0;i<4;i++)p[i]=(u8)(v>>(8*i));
}
static bool name_valid(const char *p){
    if(!p||!p[0]||p[0]!='/')return false;
    bool nul=false;
    for(u32 i=0;i<JFS_PATH;i++){
        char c=p[i];
        if(!c){nul=true;break;}
        if((u8)c<0x20 || c=='\\')return false;
        if(c=='.' && p[i+1]=='.' && (i==0||p[i-1]=='/'))return false;
    }
    return nul;
}
static bool io(u32 relative,u8 *data,u32 sectors,bool write){
    if(relative>=JFS_SECTORS || !sectors ||
       sectors>JFS_SECTORS-relative)return false;
    for(u32 i=0;i<sectors;){
        u32 n=sectors-i;if(n>10)n=10;
        if(!diskdb_store_io(JFS_BEGIN+relative+i,data+i*512u,n,write))
            return false;
        i+=n;
    }
    return true;
}
static void digest(u32 n,u8 out[32]){
    k_memcpy(hash_buffer,header,96);
    if(n)k_memcpy(hash_buffer+96,payload,n);
    sha256_hash(hash_buffer,96+n,out);
}
static i32 find(const char *path){
    for(u32 i=0;i<JFS_MAX_FILES;i++)
        if(entries[i].present && k_strcmp(entries[i].path,path)==0)
            return (i32)i;
    return -1;
}
static i32 vacant(void){
    for(u32 i=0;i<JFS_MAX_FILES;i++)
        if(!entries[i].present)return (i32)i;
    return -1;
}
static void update_index(const char *name,u32 start,u32 n,u32 generation,bool deleted){
    i32 idx=find(name);
    if(idx<0 && !deleted)idx=vacant();
    if(idx<0)return;
    entries[idx].present=!deleted;
    entries[idx].start=start;
    entries[idx].length=n;
    entries[idx].sequence=generation;
    if(!deleted)k_strcpy(entries[idx].path,name);
    else entries[idx].path[0]=0;
}
static bool valid_transaction(u32 pos,u32 sectors,u32 n){
    if(!io(pos+1u,payload,sectors,false))return false;
    if(!io(pos+1u+sectors,footer,1,false))return false;
    if(get32(footer)!=JFS_COMMIT ||
       get32(footer+4)!=get32(header+4))return false;
    u8 calculated[32],bad=0;
    digest(n,calculated);
    for(u32 i=0;i<32;i++)
        bad|=calculated[i]^header[96+i]^footer[8+i]^header[96+i];
    if(bad)return false;
    for(u32 i=0;i<32;i++)if(calculated[i]!=header[96+i] ||
                             calculated[i]!=footer[8+i])return false;
    return true;
}
bool jfs_mount(void){
    mounted=false;seq=0;invalid_records=0;next_sector=0;
    k_memset(entries,0,sizeof entries);
    if(SET.install_disk<0 ||
       !diskdb_store_io(JFS_BEGIN+JFS_SECTORS-1,header,1,false))
        return false;
    for(u32 pos=0;pos+2<=JFS_SECTORS;){
        if(!io(pos,header,1,false))return false;
        if(get32(header)!=JFS_INTENT) {
            /* No magic means end of log (new partition should be zero). */
            if(get32(header)!=0)invalid_records++;
            next_sector=pos;mounted=true;return true;
        }
        u32 generation=get32(header+4);
        u32 len=get32(header+8);
        u32 sectors=get32(header+12);
        u32 type=get32(header+16);
        if(!generation || generation<=seq || len>JFS_LIMIT ||
           sectors!=(len+511u)/512u ||
           pos+2u+sectors>JFS_SECTORS || type>1u ||
           (type==0 && len) || !name_valid((const char *)(header+32)) ||
           !valid_transaction(pos,sectors,len)) {
            invalid_records++;
            next_sector=pos;mounted=true;return true;
        }
        update_index((const char *)(header+32),pos,len,generation,type==0);
        seq=generation;
        pos+=sectors+2u;
        next_sector=pos;
    }
    mounted=true;
    return true;
}
bool jfs_ready(void){return mounted;}
u32 jfs_file_count(void){
    u32 n=0;for(u32 i=0;i<JFS_MAX_FILES;i++)if(entries[i].present)n++;
    return n;
}
u32 jfs_free_sectors(void){return mounted?JFS_SECTORS-next_sector:0u;}
u32 jfs_incomplete(void){return invalid_records;}
static bool commit(const char *name,const u8 *buf,u32 n,bool deleted){
    if(!mounted||!name_valid(name)||n>JFS_LIMIT||(n&&!buf)||(!deleted&&
       find(name)<0&&vacant()<0))return false;
    u32 blocks=(n+511u)/512u;
    if(blocks+2u>JFS_SECTORS-next_sector)return false;
    u32 generation=seq+1u;if(!generation)return false;
    k_memset(header,0,sizeof header);
    put32(header,JFS_INTENT);put32(header+4,generation);
    put32(header+8,n);put32(header+12,blocks);
    put32(header+16,deleted?0u:1u);
    k_strcpy((char *)(header+32),name);
    if(n)k_memcpy(payload,buf,n);
    if(n%512u)k_memset(payload+n,0,512u-n%512u);
    u8 digest_bytes[32];
    digest(n,digest_bytes);
    k_memcpy(header+96,digest_bytes,32);
    k_memset(footer,0,sizeof footer);
    put32(footer,JFS_COMMIT);put32(footer+4,generation);
    k_memcpy(footer+8,digest_bytes,32);
    /* Intent -> payload -> final commit, each store flushes ATA cache. */
    if(!io(next_sector,header,1,true))return false;
    if(blocks&&!io(next_sector+1u,payload,blocks,true))return false;
    if(!io(next_sector+1u+blocks,footer,1,true))return false;
    update_index(name,next_sector,n,generation,deleted);
    seq=generation;next_sector+=blocks+2u;
    return true;
}
bool jfs_write(const char *name,const u8 *data,u32 len){
    return commit(name,data,len,false);
}
bool jfs_remove(const char *name){
    return find(name)>=0&&commit(name,NULL,0,true);
}
i32 jfs_read(const char *name,u8 *out,u32 cap){
    if(!mounted||!out||!name_valid(name))return -1;
    i32 index=find(name);if(index<0)return -1;
    jfs_entry_t *e=&entries[index];
    if(cap<e->length || !io(e->start,header,1,false))return -1;
    u32 sectors=(e->length+511u)/512u;
    if(!valid_transaction(e->start,sectors,e->length) ||
       get32(header+4)!=e->sequence)return -1;
    if(e->length)k_memcpy(out,payload,e->length);
    return (i32)e->length;
}
bool jfs_fsck(u32 *files,u32 *incomplete){
    if(!jfs_mount())return false;
    if(files)*files=jfs_file_count();
    if(incomplete)*incomplete=invalid_records;
    return true;
}
