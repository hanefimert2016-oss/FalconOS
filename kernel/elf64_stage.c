/* FalconOS ELF64 strict *validation and staging* for a future Ring-3 loader.
 *
 * This is deliberately NOT executable until a GDT/TSS, per-process page
 * tables (U/S bits), syscall entry, and interrupt return isolation exist.
 * Untrusted ELF binaries are never invoked in Ring 0.
 */
#include "falcon.h"
#define ELF_MIN_VADDR 0x400000ull
#define ELF_MAX_VADDR 0x600000ull
#define ELF_MAX_MEMORY 0x200000ull
#define ELF_HDR 64u
#define ELF_PHDR 56u
#define ELF_PT_LOAD 1u
#define ELF_PF_X 1u
#define ELF_PF_W 2u

static u16 le16(const u8 *p){return (u16)p[0]|((u16)p[1]<<8);}
static u32 le32(const u8 *p){return (u32)p[0]|((u32)p[1]<<8)|
                                   ((u32)p[2]<<16)|((u32)p[3]<<24);}
static u64 le64(const u8 *p){return (u64)le32(p)|((u64)le32(p+4)<<32);}

typedef struct { u64 start,end;u32 flags; } elf_region_t;
static bool check(const u8 *image,u32 length,u64 *entry_out,
                  elf_region_t *segments,u32 *nsegments){
    if(!image||length<ELF_HDR||length>32768u||
       image[0]!=0x7Fu||image[1]!='E'||image[2]!='L'||image[3]!='F'||
       image[4]!=2||image[5]!=1||image[6]!=1)return false;
    if(le16(image+16)!=2 || le16(image+18)!=62 || le32(image+20)!=1 ||
       le16(image+52)!=ELF_HDR || le16(image+54)!=ELF_PHDR)return false;
    u64 phoff=le64(image+32),entry=le64(image+24);
    u32 count=le16(image+56);
    if(!count||count>16||phoff<ELF_HDR||phoff>(u64)length ||
       (u64)count*ELF_PHDR>(u64)length-phoff ||
       entry<ELF_MIN_VADDR||entry>=ELF_MAX_VADDR)return false;
    u32 loaded=0;
    bool executable_entry=false;
    for(u32 i=0;i<count;i++){
        const u8 *ph=image+(u32)phoff+i*ELF_PHDR;
        if(le32(ph)!=ELF_PT_LOAD)continue;
        u32 flags=le32(ph+4);
        u64 file_off=le64(ph+8);
        u64 addr=le64(ph+16);
        u64 filesize=le64(ph+32);
        u64 memsize=le64(ph+40);
        u64 align=le64(ph+48);
        if((flags & (ELF_PF_X|ELF_PF_W))==(ELF_PF_X|ELF_PF_W) ||
           (flags&~7u)!=0 || !memsize || filesize>memsize ||
           addr<ELF_MIN_VADDR || addr>=ELF_MAX_VADDR ||
           memsize>ELF_MAX_VADDR-addr ||
           file_off>length || filesize>(u64)length-file_off ||
           (align!=0&&align!=1&&(align&(align-1))!=0)||
           (align>1 && ((addr-file_off)&(align-1))!=0))return false;
        if(loaded>=16)return false;
        u64 end=addr+memsize;
        for(u32 j=0;j<loaded;j++){
            if(addr<segments[j].end && segments[j].start<end)return false;
        }
        segments[loaded].start=addr;segments[loaded].end=end;
        segments[loaded].flags=flags;
        loaded++;
        if((flags&ELF_PF_X) && entry>=addr && entry<addr+filesize)
            executable_entry=true;
    }
    if(!loaded||!executable_entry)return false;
    *entry_out=entry;*nsegments=loaded;
    return true;
}
bool elf64_inspect(const u8 *image,u32 length,u64 *entry_out,u32 *nsegments){
    elf_region_t regions[16];
    u64 entry=0;u32 count=0;
    if(!check(image,length,&entry,regions,&count))return false;
    if(entry_out)*entry_out=entry;
    if(nsegments)*nsegments=count;
    return true;
}
/* Copy a validated executable into a caller-owned contiguous staging arena,
 * never into a privileged fixed identity-mapped address. The arena is not
 * executable in kernel mode and has no direct access to PCI/MMIO.
 */
bool elf64_stage(const u8 *image,u32 length,u8 *arena,u32 arena_length,
                 u64 *entry_out){
    elf_region_t regions[16];
    u64 entry=0;u32 n=0;
    if(!arena||arena_length<ELF_MAX_MEMORY||
       !check(image,length,&entry,regions,&n))return false;
    k_memset(arena,0,ELF_MAX_MEMORY);
    u64 phoff=le64(image+32);
    u32 pcount=le16(image+56);
    for(u32 i=0;i<pcount;i++){
        const u8 *ph=image+(u32)phoff+i*ELF_PHDR;
        if(le32(ph)!=ELF_PT_LOAD)continue;
        u32 offset=(u32)(le64(ph+16)-ELF_MIN_VADDR);
        u32 file_offset=(u32)le64(ph+8);
        u32 filesz=(u32)le64(ph+32);
        if(filesz)k_memcpy(arena+offset,image+file_offset,filesz);
    }
    if(entry_out)*entry_out=entry;
    return true;
}
