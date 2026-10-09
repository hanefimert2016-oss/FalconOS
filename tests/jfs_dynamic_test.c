#include "falcon.h"
#include <stdio.h>
#include <string.h>
static u8 DISK[32768u*512u];
static i32 fail_after=-1;
static bool damaged;
settings_t SET;
void k_memcpy(void *a,const void *b,u32 n){memcpy(a,b,n);}
void k_memset(void *a,u8 c,u32 n){memset(a,c,n);}
char *k_strcpy(char *a,const char *b){return strcpy(a,b);}
i32 k_strcmp(const char *a,const char *b){return strcmp(a,b);}
void sha256_hash(const u8 *p,u32 n,u8 out[32]){
    u32 sum=0x12345678u;
    for(u32 i=0;i<n;i++)sum=(sum^p[i])*16777619u;
    for(u32 i=0;i<32;i++){sum=sum*1664525u+1013904223u;out[i]=(u8)(sum>>24);}
}
bool diskdb_store_io(u32 sector,u8 *data,u32 sectors,bool write){
    if(SET.install_disk<0||!data||!sectors||sectors>10||
       sector<16||sector+sectors>32768u)return false;
    if(write){
        if(fail_after==0){damaged=true;return false;}
        if(fail_after>0)fail_after--;
        memcpy(DISK+512u*sector,data,512u*sectors);
    }else memcpy(data,DISK+512u*sector,512u*sectors);
    return true;
}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"JFS FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
static u8 a[131072],b[131072];
int main(void){
    SET.install_disk=-1;
    CHECK(!jfs_mount());
    SET.install_disk=0;
    CHECK(jfs_mount()&&jfs_file_count()==0);
    CHECK(jfs_free_sectors()==16384);
    for(u32 i=0;i<sizeof(a);i++)a[i]=(u8)((i*19u+13u)&255u);
    CHECK(jfs_write("/project/program.elf",a,sizeof a));
    CHECK(jfs_file_count()==1&&jfs_free_sectors()<16384);
    CHECK(jfs_read("/project/program.elf",b,sizeof b)==131072);
    CHECK(memcmp(a,b,sizeof a)==0);
    CHECK(jfs_mount()&&jfs_file_count()==1);
    CHECK(jfs_read("/project/program.elf",b,sizeof b)==131072);
    CHECK(!memcmp(a,b,sizeof a));
    memset(a,0xEF,sizeof a);
    /* Force interruption during payload transaction, after intent. */
    fail_after=4;damaged=false;
    CHECK(!jfs_write("/project/program.elf",a,sizeof a)&&damaged);
    fail_after=-1;
    CHECK(jfs_mount());
    CHECK(jfs_file_count()==1);
    CHECK(jfs_read("/project/program.elf",b,sizeof b)==131072);
    CHECK(b[0]==13);
    CHECK(jfs_incomplete()>=1);
    CHECK(jfs_write("/project/program.elf",a,sizeof a));
    CHECK(jfs_mount());
    CHECK(jfs_read("/project/program.elf",b,sizeof b)==131072);
    CHECK(b[0]==0xEF&&b[131071]==0xEF);
    CHECK(jfs_remove("/project/program.elf"));
    CHECK(jfs_mount()&&jfs_file_count()==0);
    CHECK(jfs_read("/project/program.elf",b,sizeof b)<0);
    CHECK(!jfs_write("/../bad",a,12));
    CHECK(!jfs_write("relative",a,12));
    CHECK(!jfs_write("/big",a,131073));
    for(u32 i=0;i<128;i++){
        char path[56];
        snprintf(path,sizeof(path),"/file%03u",i);
        CHECK(jfs_write(path,a,2));
    }
    CHECK(jfs_file_count()==128);
    CHECK(!jfs_write("/over-capacity",a,1));
    CHECK(jfs_mount()&&jfs_file_count()==128);
    u32 files=0,bad=0;
    CHECK(jfs_fsck(&files,&bad)&&files==128);
    CHECK(DISK[0]==0);
    puts("PASS JFS2 variable-sized file journal, replay, interrupted writes, SHA, 128 names, no MBR overwrite");
    return 0;
}
