/* FalconBoot/1: FalconOS-owned UEFI x86_64 bootloader.
 * No GRUB/Limine/shim. Boots the fixed-address ELF-derived kernel image,
 * constructs Multiboot2-compatible GOP/RAM descriptors, exits firmware,
 * installs private identity page tables and jumps into native long_start.
 * Experimental QEMU OVMF only. Do not install on physical disks.
 */
#include "uefi.h"
#ifndef FALCON_KERNEL_BYTES
#error "Pass FALCON_KERNEL_BYTES"
#endif
#ifndef FALCON_KERNEL_ENTRY
#error "Pass FALCON_KERNEL_ENTRY"
#endif
#define KBASE 0x02000000ULL
#define KSIZE (64ULL*1024*1024)
#define BOOTINFO_LEN 32768ULL
#define MAPBUF_LEN 65536ULL
typedef STATUS (*HANDLE_PROTOCOL)(HANDLE,const GUID*,void**);
typedef STATUS (*LOCATE_PROTOCOL)(const GUID*,void*,void**);
typedef STATUS (*ALLOCATE_PAGES)(uint32_t,uint32_t,uint64_t,uint64_t*);
typedef STATUS (*GET_MEMORY_MAP)(uint64_t*,MEMDESC*,uint64_t*,uint64_t*,uint32_t*);
typedef STATUS (*EXIT_BOOT_SERVICES)(HANDLE,uint64_t);
typedef STATUS (*OPEN_VOLUME)(FS*,FILEP**);
typedef STATUS (*OPEN_FILE)(FILEP*,FILEP**,const CHAR16*,uint64_t,uint64_t);
typedef STATUS (*READ_FILE)(FILEP*,uint64_t*,void*);
typedef STATUS (*CLOSE_FILE)(FILEP*);
typedef STATUS (*QUERY_MODE)(GOP*,uint32_t,uint64_t*,GOPINFO**);
typedef STATUS (*SET_MODE)(GOP*,uint32_t);
typedef STATUS (*FREE_POOL)(void*);
typedef STATUS (*STALL)(uint64_t);
typedef struct {uint16_t scan_code,unicode_char;} INPUTKEY;
typedef struct {void*reset,*read,*wait;} SIMPLEINPUT;
typedef STATUS (*READ_KEY)(SIMPLEINPUT*,INPUTKEY*);
static uint32_t boot_preference=1u; /* HD 720, FHD 1080 (default), QHD 1440 */
extern void falcon_jump_to_kernel(uint64_t,uint64_t,uint64_t,uint64_t);
static ST*st;static BS*bs;static GOP*gop;
static uint64_t framebuffer,pgtable,stack,info_addr,map_addr;
static uint32_t width,height,pitch,mbpos;static uint8_t*bootinfo;
static void out(uint16_t p,uint8_t b){__asm__ volatile("outb %0,%1"::"a"(b),"Nd"(p));}
static void trace(const char*s){for(;*s;s++)out(0xe9,(uint8_t)*s);}
static void cp(void*d,const void*s,uint64_t n){uint8_t*a=d;const uint8_t*b=s;for(uint64_t i=0;i<n;i++)a[i]=b[i];}
static void clear(void*d,uint64_t n){uint8_t*a=d;for(uint64_t i=0;i<n;i++)a[i]=0;}
static uint32_t pad8(uint32_t n){return (n+7u)&~7u;}
static void w32(void*p,uint32_t n){cp(p,&n,4);}
static void w64(void*p,uint64_t n){cp(p,&n,8);}
static uint8_t*make_tag(uint32_t type,uint32_t len){
    uint32_t end=mbpos+pad8(len);
    if(end+8>BOOTINFO_LEN)return 0;
    uint8_t*p=bootinfo+mbpos;clear(p,pad8(len));
    w32(p,type);w32(p+4,len);mbpos=end;return p;
}
static uint64_t pages(uint64_t base,uint64_t size,int anywhere){
    ALLOCATE_PAGES f=EFI_FN(bs,2,ALLOCATE_PAGES);
    uint64_t at=base;
    if(f(anywhere?0u:2u,2u,(size+4095u)/4096u,&at)!=0)return 0;
    return at>=0x100000000ULL?0:at;
}
static const uint8_t FONT[][6]={
{65,0x7e,0x11,0x11,0x11,0x7e},{66,0x7f,0x49,0x49,0x49,0x36},
{67,0x3e,0x41,0x41,0x41,0x22},{68,0x7f,0x41,0x41,0x22,0x1c},
{69,0x7f,0x49,0x49,0x49,0x41},{70,0x7f,0x09,0x09,0x09,0x01},
{71,0x3e,0x41,0x49,0x49,0x7a},{72,0x7f,0x08,0x08,0x08,0x7f},
{73,0x41,0x41,0x7f,0x41,0x41},{76,0x7f,0x40,0x40,0x40,0x40},
{77,0x7f,0x02,0x0c,0x02,0x7f},{78,0x7f,0x04,0x08,0x10,0x7f},
{79,0x3e,0x41,0x41,0x41,0x3e},{82,0x7f,0x09,0x19,0x29,0x46},
{83,0x46,0x49,0x49,0x49,0x31},{84,0x01,0x01,0x7f,0x01,0x01},
{85,0x3f,0x40,0x40,0x40,0x3f},{86,0x1f,0x20,0x40,0x20,0x1f},
{50,0x62,0x51,0x49,0x49,0x46},{89,0x03,0x04,0x78,0x04,0x03},
{46,0,0x40,0x40,0,0},
{45,0x08,0x08,0x08,0x08,0x08},{47,0x20,0x10,0x08,0x04,0x02},
{48,0x3e,0x51,0x49,0x45,0x3e},{49,0,0x42,0x7f,0x40,0},
{51,0x22,0x41,0x49,0x49,0x36},{52,0x18,0x14,0x12,0x7f,0x10},
{53,0x27,0x45,0x45,0x45,0x39},{54,0x3c,0x4a,0x49,0x49,0x30},
{55,0x01,0x71,0x09,0x05,0x03},{56,0x36,0x49,0x49,0x49,0x36},
{57,0x06,0x49,0x49,0x29,0x1e},
{74,0x20,0x40,0x41,0x3f,0x01},{75,0x7f,0x08,0x14,0x22,0x41},
{80,0x7f,0x09,0x09,0x09,0x06},{81,0x3e,0x41,0x51,0x21,0x5e},
{87,0x3f,0x40,0x38,0x40,0x3f},{88,0x63,0x14,0x08,0x14,0x63},
{90,0x61,0x51,0x49,0x45,0x43},
{32,0,0,0,0,0}
};
static void box(uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint32_t c){
    uint32_t*fb=(uint32_t*)(uintptr_t)framebuffer;
    for(uint32_t yy=y;yy<y+h&&yy<height;yy++){
        uint64_t at=((uint64_t)yy*pitch)/4u;
        for(uint32_t xx=x;xx<x+w&&xx<width;xx++)fb[at+xx]=c;
    }
}
static void label(uint32_t x,uint32_t y,const char*s,uint32_t scale,uint32_t color){
    for(;*s;s++){
        uint8_t ch=(uint8_t)*s;
        if(ch>='a'&&ch<='z')ch-=32;
        const uint8_t*g=FONT[sizeof(FONT)/sizeof(FONT[0])-1]+1;
        for(uint32_t i=0;i<sizeof(FONT)/sizeof(FONT[0]);i++)
            if(FONT[i][0]==ch){g=FONT[i]+1;break;}
        for(uint32_t col=0;col<5;col++)
            for(uint32_t row=0;row<7;row++)
                if(g[col]&(1u<<row))
                    box(x+col*scale,y+row*scale,scale,scale,color);
        x+=6*scale;
    }
}
static void paint_loader(void){
    uint32_t w=width,h=height;
    for(uint32_t y=0;y<h;y++){
        uint32_t c=((4u+y*16u/h)<<16)|((15u+y*20u/h)<<8)|(36u+y*29u/h);
        box(0,y,w,1,c);
    }
    uint32_t cx=w/2,cy=h/2;
    uint32_t z=w>=1920?5u:3u;
    label(cx-11*6*z/2,cy+54,"FALCONOS V2",z,0xeff6ff);
    label(cx-7*6*3/2,cy+110,"V2 BETA",3,0x70dffa);
    label(cx-21*6*2/2,cy+153,"FALCONBOOT UEFI LOADER",2,0xb0cce7);
    box(cx-205,cy+187,410,4,0x315e8a);
    box(cx-205,cy+187,325,4,0x60d5f5);
    label(cx-14*6*2/2,cy+207,"BOOTING SYSTEM",2,0xd7e7fa);
}
/* Draw a build-baked frame of the project's actual animated GLB.
 * A bounded, uncompressed 24-bit BMP is read from the same read-only
 * FalconBoot ESP as the kernel. Missing/corrupt images are skipped.
 * No untrusted GLB parsing or large graphics runtime in UEFI. */
static uint16_t bmp16(const uint8_t*b){return (uint16_t)b[0]|(uint16_t)((uint16_t)b[1]<<8);}
static uint32_t bmp32(const uint8_t*b){return (uint32_t)b[0]|((uint32_t)b[1]<<8)|
    ((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);}
static void paint_dragon(HANDLE image){
    HANDLE_PROTOCOL hp=EFI_FN(bs,16,HANDLE_PROTOCOL);
    LOADED*li=0;FS*fs=0;FILEP*root=0,*file=0;
    if(hp(image,&ID_LOADED,(void**)&li)||!li)return;
    if(hp(li->device,&ID_FS,(void**)&fs)||!fs)return;
    if(((OPEN_VOLUME)fs->open_volume)(fs,&root)||!root)return;
    static const CHAR16 path[]=L"\\FALCONOS\\BOOT.BMP";
    if(((OPEN_FILE)root->open)(root,&file,path,1,0)||!file)goto close_root;
    uint64_t at=pages(0,512u*1024u,1);
    if(!at)goto close_file;
    uint8_t*buf=(uint8_t*)(uintptr_t)at;
    uint64_t n=512u*1024u;
    if(((READ_FILE)file->read)(file,&n,buf)||n<54u)goto close_file;
    if(buf[0]!='B'||buf[1]!='M'||bmp32(buf+14)<40u||
       bmp16(buf+26)!=1u||bmp16(buf+28)!=24u||bmp32(buf+30)!=0u)
        goto close_file;
    uint32_t off=bmp32(buf+10),w=bmp32(buf+18),h=bmp32(buf+22);
    if(w<32u||w>480u||h<32u||h>360u||w>width||
       ((uint64_t)(((w*3u)+3u)&~3u)*h)+off>n)goto close_file;
    uint32_t stride=(w*3u+3u)&~3u;
    uint32_t x0=(width-w)/2u,y0=(height>760u?height/2u-320u:35u);
    uint32_t*fb=(uint32_t*)(uintptr_t)framebuffer;
    for(uint32_t y=0;y<h&&y0+y<height;y++){
        const uint8_t*src=buf+off+(uint64_t)(h-1u-y)*stride;
        for(uint32_t x=0;x<w;x++){
            uint8_t b=src[x*3u],g=src[x*3u+1u],r=src[x*3u+2u];
            /* Black background is transparent over the boot gradient. */
            if(r<12u&&g<12u&&b<12u)continue;
            fb[((uint64_t)(y0+y)*pitch)/4u+x0+x]=
                ((uint32_t)r<<16)|((uint32_t)g<<8)|b;
        }
    }
    trace("FALCONBOOT/1 GLB BMP SHOWN\\n");
close_file:
    ((CLOSE_FILE)file->close)(file);
close_root:
    ((CLOSE_FILE)root->close)(root);
}
static void paint_choices(uint32_t current){
    uint32_t cx=width/2u,cy=height/2u;
    uint32_t first=cy+230u;
    const char*const choices[3]={
        "FALCONOS HD 720","FALCONOS FHD 1080","FALCONOS QHD 1440"
    };
    for(uint32_t i=0;i<3u;i++){
        uint32_t y=first+i*37u;
        box(cx-205u,y,410u,31u,i==current?0x1f658b:0x152940);
        box(cx-205u,y,4u,31u,i==current?0x76f5e3:0x344b64);
        label(cx-174u,y+8u,choices[i],2u,i==current?0xffffff:0xb2c6df);
    }
}
static void boot_menu(void){
    paint_choices(boot_preference);
    SIMPLEINPUT*in=(SIMPLEINPUT*)st->in;
    STALL pause=EFI_FN(bs,28,STALL);
    if(!in||!in->read)return; /* headless firmware retains safe default */
    for(uint32_t tick=0;tick<50u;tick++){
        INPUTKEY key={0,0};
        if(((READ_KEY)in->read)(in,&key)==0){
            uint32_t previous=boot_preference;
            if(key.scan_code==1u&&boot_preference>0)boot_preference--;
            else if(key.scan_code==2u&&boot_preference<2u)boot_preference++;
            else if(key.unicode_char==13u)break;
            if(boot_preference!=previous)paint_choices(boot_preference);
        }
        pause(100000u); /* 100ms, automatic boot after 5s */
    }
    trace("FALCONBOOT/1 MENU SELECTED\\n");
}
static int select_video(void){
    LOCATE_PROTOCOL locate=EFI_FN(bs,37,LOCATE_PROTOCOL);
    if(locate(&ID_GOP,0,(void**)&gop)||!gop||!gop->mode)return 0;
    uint32_t chosen=0xffffffffu,best=0;
    const uint32_t wanted_w=boot_preference==0u?1280u:
                             boot_preference==1u?1920u:2560u;
    const uint32_t wanted_h=boot_preference==0u?720u:
                             boot_preference==1u?1080u:1440u;
    for(uint32_t i=0;i<gop->mode->max;i++){
        uint64_t size=0;GOPINFO *inf=0;
        if(((QUERY_MODE)gop->query)(gop,i,&size,&inf)||!inf)continue;
        uint32_t w=inf->width,h=inf->height,score=0;
        if(inf->pixel_format==1 && w>=1024 && h>=720 && w<=2560 && h<=1440)
            score=w*h+(w==wanted_w&&h==wanted_h?5000000u:0u);
        if(score>best){best=score;chosen=i;}
        ((FREE_POOL)bs->f[6])(inf);
    }
    if(chosen==0xffffffffu || ((SET_MODE)gop->set)(gop,chosen))return 0;
    GOPMODE*m=gop->mode;
    if(!m->info||m->info->pixel_format!=1||m->base>=0x100000000ULL)return 0;
    framebuffer=m->base;width=m->info->width;height=m->info->height;
    pitch=m->info->scanline*4u;
    if(framebuffer+(uint64_t)pitch*height>0x100000000ULL)return 0;
    return 1;
}
static int load_image(HANDLE image){
    HANDLE_PROTOCOL hp=EFI_FN(bs,16,HANDLE_PROTOCOL);
    LOADED *li=0;FS*fs=0;FILEP*root=0,*file=0;
    if(hp(image,&ID_LOADED,(void**)&li)||!li)return 0;
    if(hp(li->device,&ID_FS,(void**)&fs)||!fs)return 0;
    if(((OPEN_VOLUME)fs->open_volume)(fs,&root)||!root)return 0;
    static const CHAR16 path[]=L"\\FALCONOS\\KERNEL.BIN";
    if(((OPEN_FILE)root->open)(root,&file,path,1,0)||!file){
        ((CLOSE_FILE)root->close)(root);return 0;
    }
    int ok=0;
    if(FALCON_KERNEL_BYTES<=KSIZE && pages(KBASE,KSIZE,0)==KBASE){
        clear((void*)(uintptr_t)KBASE,KSIZE);
        uint64_t n=FALCON_KERNEL_BYTES;
        ok=(((READ_FILE)file->read)(file,&n,(void*)(uintptr_t)KBASE)==0 &&
            n==FALCON_KERNEL_BYTES);
    }
    ((CLOSE_FILE)file->close)(file);
    ((CLOSE_FILE)root->close)(root);
    return ok;
}
static void init_paging(void){
    pgtable=pages(0,6*4096u,1);
    if(!pgtable)return;
    clear((void*)(uintptr_t)pgtable,6*4096u);
    uint64_t*pml=(uint64_t*)(uintptr_t)pgtable;
    uint64_t*pdpt=(uint64_t*)(uintptr_t)(pgtable+4096);
    pml[0]=(pgtable+4096)|3u;
    for(uint32_t i=0;i<4;i++){
        pdpt[i]=(pgtable+8192u+(uint64_t)i*4096u)|3u;
        uint64_t*pd=(uint64_t*)(uintptr_t)(pgtable+8192u+(uint64_t)i*4096u);
        for(uint32_t j=0;j<512;j++)pd[j]=((uint64_t)(i*512+j)*0x200000u)|0x83u;
    }
}
static uint32_t memory_kind(uint32_t e){
    if(e==7||e==3||e==4)return 1;
    if(e==9)return 3;
    if(e==10)return 4;
    return 2;
}
static void prepare_info(uint8_t*mem,uint64_t size,uint64_t stride){
    bootinfo=(uint8_t*)(uintptr_t)info_addr;clear(bootinfo,BOOTINFO_LEN);
    mbpos=8;
    uint8_t*fb=make_tag(8,38);
    if(fb){
        w64(fb+8,framebuffer);w32(fb+16,pitch);w32(fb+20,width);w32(fb+24,height);
        fb[28]=32;fb[29]=1;
        fb[32]=16;fb[33]=8;fb[34]=8;fb[35]=8;fb[36]=0;fb[37]=8;
    }
    uint32_t n=(uint32_t)(size/stride);
    if(n>120)n=120;
    uint8_t*mm=make_tag(6,16+n*24u);
    if(mm){
        w32(mm+8,24);w32(mm+12,0);
        uint32_t count=0;
        for(uint32_t i=0;i<n;i++){
            MEMDESC*e=(MEMDESC*)(mem+(uint64_t)i*stride);
            if(e->pages==0)continue;
            uint8_t*d=mm+16+count*24u;
            w64(d,e->physical);w64(d+8,e->pages*4096u);
            w32(d+16,memory_kind(e->type));w32(d+20,0);count++;
        }
        w32(mm+4,16+count*24u);
        mbpos=pad8((uint32_t)(mm-bootinfo)+16+count*24u);
    }
    make_tag(0,8);w32(bootinfo,mbpos);w32(bootinfo+4,0);
}
STATUS EfiMain(HANDLE image,ST*system){
    st=system;bs=system->boot;
    trace("FALCONBOOT/1 START\n");
    if(!select_video()){trace("FALCONBOOT/1 FAIL GOP\n");return 1;}
    trace("FALCONBOOT/1 GOP\n");
    paint_loader();
    paint_dragon(image);
    boot_menu();
    if(boot_preference!=1u){
        if(!select_video()){trace("FALCONBOOT/1 FAIL SELECTED GOP\\n");return 1;}
        paint_loader();
        paint_dragon(image);
    }
    if(!load_image(image)){trace("FALCONBOOT/1 FAIL KERNEL ADDRESS/READ\n");return 2;}
    trace("FALCONBOOT/1 KERNEL\n");
    info_addr=pages(0,BOOTINFO_LEN,1);
    map_addr=pages(0,MAPBUF_LEN,1);
    stack=pages(0,65536u,1);
    init_paging();
    if(!info_addr||!map_addr||!stack||!pgtable){
        trace("FALCONBOOT/1 FAIL ALLOC\n");return 3;
    }
    trace("FALCONBOOT/1 MENU DRAWN\n");
    GET_MEMORY_MAP get=EFI_FN(bs,4,GET_MEMORY_MAP);
    EXIT_BOOT_SERVICES exit_boot=EFI_FN(bs,26,EXIT_BOOT_SERVICES);
    for(uint32_t try=0;try<5;try++){
        uint64_t n=MAPBUF_LEN,key=0,stride=0;uint32_t version=0;
        if(get(&n,(MEMDESC*)(uintptr_t)map_addr,&key,&stride,&version) ||
           !stride||n>MAPBUF_LEN){trace("FALCONBOOT/1 FAIL MEMMAP\n");return 4;}
        prepare_info((uint8_t*)(uintptr_t)map_addr,n,stride);
        if(exit_boot(image,key)==0){
            trace("FALCONBOOT/1 HANDOFF\n");
            falcon_jump_to_kernel((uint64_t)FALCON_KERNEL_ENTRY,
                                  info_addr,pgtable,stack+65536u);
            for(;;)__asm__ volatile("hlt");
        }
    }
    trace("FALCONBOOT/1 FAIL EXITBS\n");return 5;
}
