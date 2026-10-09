/* Independent host-side test of real copy-on-write disk layout.
 * A mock block device only; production partition validation stays in diskdb.c.
 */
#include "falcon.h"
#include <stdio.h>
#include <string.h>
#define SECTORS 8600u
static u8 disk[SECTORS*512u];
settings_t SET;
static i32 writes_until_failure=-1;
i32 k_strcmp(const char *a,const char *b){return strcmp(a,b);}
char *k_strcpy(char *a,const char *b){return strcpy(a,b);}
void k_memcpy(void *a,const void *b,u32 n){memcpy(a,b,n);}
void k_memset(void *a,u8 v,u32 n){memset(a,v,n);}
void sha256_hash(const u8 *p,u32 n,u8 out[32]){
    /* Only for disk record checksum integrity tests, not crypto assertions. */
    u32 state=0x811C9DC5u;
    for(u32 i=0;i<n;i++)state=(state^p[i])*16777619u;
    for(i32 i=0;i<32;i++){state=state*1664525u+1013904223u;out[i]=(u8)(state>>24);}
}
bool diskdb_store_io(u32 offset,u8 *buf,u32 count,bool write){
    if(SET.install_disk<0||!buf||count<1||count>10||offset<16||
       offset+count>SECTORS)return false;
    if(write){
        if(writes_until_failure==0)return false;
        if(writes_until_failure>0)writes_until_failure--;
        memcpy(disk+offset*512u,buf,count*512u);
    }else memcpy(buf,disk+offset*512u,count*512u);
    return true;
}
#define CHECK(e,n) do{if(!(e)){fprintf(stderr,"XFS failed line %d: %s\n",__LINE__,#e);return n;}}while(0)
static u8 source[32768],back[32768];
int main(void){
    SET.install_disk=0;
    CHECK(xfs_mount(),1);
    CHECK(xfs_capacity()==32&&xfs_max_size()==32768,2);
    for(u32 i=0;i<sizeof source;i++)source[i]=(u8)(i*37u);
    CHECK(xfs_write("big_file.bin",source,sizeof source),3);
    CHECK(xfs_count()==1,4);
    CHECK(xfs_read("big_file.bin",back,sizeof back)==32768,5);
    CHECK(!memcmp(source,back,sizeof back),6);
    CHECK(xfs_mount(),7);
    CHECK(xfs_read("big_file.bin",back,sizeof back)==32768,8);
    CHECK(!memcmp(source,back,sizeof back),9);
    memset(source,0xA5,sizeof source);
    writes_until_failure=3;
    CHECK(!xfs_write("big_file.bin",source,sizeof source),10);
    writes_until_failure=-1;
    CHECK(xfs_mount(),11);
    CHECK(xfs_read("big_file.bin",back,sizeof back)==32768,12);
    CHECK(back[0]==0&&back[1]==37,13);
    CHECK(xfs_write("big_file.bin",source,sizeof source),14);
    CHECK(xfs_mount(),15);
    CHECK(xfs_read("big_file.bin",back,sizeof back)==32768,16);
    CHECK(back[0]==0xA5&&back[32767]==0xA5,17);
    /* Damage most recent generation; fallback to original. */
    disk[(4096u+66u+10u)*512u+512u]^=0x55u;
    CHECK(xfs_mount(),18);
    CHECK(xfs_read("big_file.bin",back,sizeof back)==32768,19);
    CHECK(back[0]==0 && back[1]==37,20);
    CHECK(xfs_corrupt_copies()>=1,21);
    CHECK(xfs_remove("big_file.bin"),22);
    CHECK(xfs_mount(),23);
    CHECK(xfs_read("big_file.bin",back,sizeof back)<0,24);
    CHECK(!xfs_write("../evil",source,1),25);
    CHECK(!xfs_write("too_big",source,32769u),26);
    for(u32 i=0;i<32;i++){
        char name[20];snprintf(name,sizeof name,"file_%02u",i);
        CHECK(xfs_write(name,source,1),27);
    }
    CHECK(xfs_count()==32,28);
    CHECK(!xfs_write("excess",source,1),29);
    u32 files=0,corrupt=0;
    CHECK(xfs_fsck(&files,&corrupt)&&files==32,30);
    CHECK(disk[0]==0,31);
    SET.install_disk=-1;
    CHECK(!xfs_mount()&&!xfs_ready(),32);
    puts("PASS XFS1: 32 x 32KiB, reboot, torn-write, old-copy recovery, tombstones, fsck, no MBR writes");
    return 0;
}
