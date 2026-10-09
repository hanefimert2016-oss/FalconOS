/* Native regression tests for the PFS1 copy-on-write records. */
#include "falcon.h"
#include "shfs.h"
#include <stdio.h>
#include <string.h>
#define SECTORS 2400u
static u8 disk[SECTORS*512u];
settings_t SET;
i32 k_strlen(const char *s) {return (i32)strlen(s);}
i32 k_strcmp(const char *a,const char *b) {return strcmp(a,b);}
i32 k_strncmp(const char *a,const char *b,i32 n){return strncmp(a,b,(size_t)n);}
char *k_strcpy(char *a,const char *b){return strcpy(a,b);}
char *k_strcat(char *a,const char *b){return strcat(a,b);}
void k_memcpy(void *a,const void *b,u32 n){memcpy(a,b,n);}
void k_memset(void *a,u8 v,u32 n){memset(a,v,n);}
void k_itoa(u32 v,char *p,i32 base){(void)base;snprintf(p,16,"%u",v);}
void sha256_hash(const u8 *src,u32 n,u8 digest[32]){
    /* Test-only deterministic checksum; production uses real SHA-256. */
    u32 a=0x811c9dc5u;
    for(u32 i=0;i<n;i++)a=(a^(u32)src[i])*16777619u;
    for(i32 i=0;i<32;i++){a=(a<<7)|(a>>25);digest[i]=(u8)(a+(u32)i);}
}
bool diskdb_store_io(u32 offset,u8 *buffer,u32 count,bool write){
    if(SET.install_disk<0||!count||count>10||offset<16||offset+count>SECTORS)return false;
    if(write)memcpy(disk+offset*512,buffer,count*512);
    else memcpy(buffer,disk+offset*512,count*512);
    return true;
}
#define CHECK(x,n) do{if(!(x)){fprintf(stderr,"PFS test failed at %s:%d\n",__FILE__,__LINE__);return n;}}while(0)
int main(void){
    memset(&SET,0,sizeof(SET)); SET.install_disk=0;
    shfs_init();
    pfs_mount(); CHECK(pfs_mounted(),1);
    shfs_ent_t *f=shfs_open_w_abs("/home/falcon/Desktop/note.txt",false);
    CHECK(f,2);memcpy(f->data,"first",6);f->len=5;
    CHECK(pfs_flush_all(),3);
    unsigned slot=(unsigned)(f-shfs_slot(0));
    CHECK(disk[(1024u+slot*20u)*512u]=='P',4);
    f=shfs_open_w_abs("/home/falcon/Desktop/note.txt",false);
    CHECK(f,5);memcpy(f->data,"second",7);f->len=6;
    CHECK(pfs_flush_all(),6);
    pfs_mount();
    f=shfs_lookup("/home/falcon/Desktop/note.txt");
    CHECK(f&&f->len==6&&!strcmp(f->data,"second"),7);
    /* Break the newer copy: after reboot, older copy must survive. */
    disk[(1024u+slot*20u+10u)*512u+128u]^=0x40u;
    pfs_mount();
    f=shfs_lookup("/home/falcon/Desktop/note.txt");
    CHECK(f&&f->len==5&&!strcmp(f->data,"first"),8);
    CHECK(shfs_rm_abs("/home/falcon/Desktop/note.txt"),9);
    CHECK(pfs_flush_all(),10);
    pfs_mount();
    CHECK(shfs_lookup("/home/falcon/Desktop/note.txt")==NULL,11);
    /* Maximum valid file payload is 4096 bytes, not 512. */
    f=shfs_open_w_abs("/home/falcon/Desktop/full.txt",false);
    CHECK(f,12);
    memset(f->data,'X',4096);f->len=4096;f->data[4096]=0;
    CHECK(pfs_flush_all(),13);
    pfs_mount();
    f=shfs_lookup("/home/falcon/Desktop/full.txt");
    CHECK(f&&f->len==4096&&f->data[4095]=='X',14);
    CHECK(disk[0]==0,15); /* Never write disk MBR/LBA0. */
    SET.install_disk=-1;pfs_mount();
    CHECK(!pfs_mounted(),16);
    puts("PASS PFS1 4096-byte save/restore, COW fallback, deletion, secure mode");
    return 0;
}
