/* ISO Studio v0.1: REAL read-back-verifiable ISO9660 level-1 image builder.
 * Runs in FalconOS as a built-in target for the Marketplace's FAPP/1
 * "isostudio" safe command. This is NOT a USB writer: FalconOS currently
 * lacks a reviewed, safe USB mass-storage block-write driver.
 *
 * Produce an actual ISO9660 archive containing ONE selected <=4096-byte
 * SHFS file. Store it in the dedicated safe 0xFA-partition's crash-safe JFS2.
 * The result is 22..23 2048-byte sectors (~45 KiB). No host disk writes,
 * raw device access, or unsafe execution occurs. */
#include "falcon.h"
#include "shfs.h"
#define ISOBLOCK 2048u
#define ISOBLOCKS 24u
#define ISO_OUTPUT "/home/falcon/Images/ARCHIVE.ISO"
static u8 image[ISOBLOCK*ISOBLOCKS];
static char source[SHFS_PATH]="/home/falcon/readme.txt";
static char active_iso[SHFS_PATH]=ISO_OUTPUT;
static const char *status="Select source with keys 1/2/3, Enter to build ISO9660";
static u32 last_size,block_count;
static bool verified;
static void setle16(u8 *p,u32 n){p[0]=(u8)n;p[1]=(u8)(n>>8);}
static void setbe16(u8 *p,u32 n){p[0]=(u8)(n>>8);p[1]=(u8)n;}
static void setle32(u8 *p,u32 n){
    for(u32 i=0;i<4;i++)p[i]=(u8)(n>>(i*8));
}
static void setbe32(u8 *p,u32 n){
    for(u32 i=0;i<4;i++)p[i]=(u8)(n>>((3-i)*8));
}
static void dual16(u8 *p,u32 n){setle16(p,n);setbe16(p+2,n);}
static void dual32(u8 *p,u32 n){setle32(p,n);setbe32(p+4,n);}
static u32 record(u8 *p,u32 extent,u32 size,bool directory,
                  const char *name,u32 namelen){
    u32 bytes=33u+namelen+(namelen%2u==0u?1u:0u);
    k_memset(p,0,bytes);
    p[0]=(u8)bytes;p[1]=0;
    dual32(p+2,extent);dual32(p+10,size);
    p[18]=126;p[19]=10;p[20]=11;p[21]=12;p[22]=0;p[23]=0;p[24]=0;
    p[25]=directory?2u:0u;
    dual16(p+28,1);p[32]=(u8)namelen;
    k_memcpy(p+33,name,namelen);
    return bytes;
}
static void volume_descriptor(u8 *p,u8 type){
    p[0]=type;
    const char *sig="CD001";
    k_memcpy(p+1,sig,5);p[6]=1;
}
void iso_studio_choose(const char *abs_path){
    if(!abs_path||k_strlen(abs_path)>=SHFS_PATH)return;
    shfs_ent_t *f=shfs_lookup(abs_path);
    if(!f||f->is_dir||!f->len||f->len>4096u){
        status="Source must be a small saved SHFS file";return;
    }
    k_strcpy(source,abs_path);
    status="File selected. Enter creates authentic ISO9660 on FalconFS.";
}
static bool check(const u8 *raw,u32 bytes){
    if(bytes<22u*ISOBLOCK||bytes>ISOBLOCKS*ISOBLOCK ||
       raw[16u*ISOBLOCK]!=1||raw[17u*ISOBLOCK]!=255)return false;
    for(u32 i=0;i<5;i++){
        if(raw[16u*ISOBLOCK+1u+i]!="CD001"[i] ||
           raw[17u*ISOBLOCK+1u+i]!="CD001"[i])return false;
    }
    return true;
}
static bool build(void){
    shfs_ent_t *file=shfs_lookup(source);
    if(!file||file->is_dir||!file->len||file->len>4096u){
        status="Source missing or larger than 4096B SHFS limit";return false;
    }
    if(!jfs_ready()){
        status="Mount a safe 0xFA FalconFS VM disk for JFS2 ISO storage";
        return false;
    }
    u32 n=file->len;
    block_count=21u+(n+ISOBLOCK-1u)/ISOBLOCK;
    if(block_count>ISOBLOCKS)return false;
    u32 total=block_count*ISOBLOCK;
    k_memset(image,0,total);
    u8 *pvd=image+16u*ISOBLOCK,*end=image+17u*ISOBLOCK;
    volume_descriptor(pvd,1);volume_descriptor(end,255);
    for(u32 i=8;i<72;i++)pvd[i]=' ';
    k_memcpy(pvd+8,"FALCONOS",8);
    k_memcpy(pvd+40,"FALCONOS_ARCHIVE",16);
    dual32(pvd+80,block_count);
    dual16(pvd+120,1);dual16(pvd+124,1);
    dual16(pvd+128,ISOBLOCK);
    dual32(pvd+132,10);
    setle32(pvd+140,18);setbe32(pvd+148,19);
    u8 zero=0,one=1;
    record(pvd+156,20,ISOBLOCK,true,(const char*)&zero,1);
    u8 *pt=image+18u*ISOBLOCK,*ptbe=image+19u*ISOBLOCK;
    pt[0]=ptbe[0]=1;pt[1]=ptbe[1]=0;
    setle32(pt+2,20);setbe32(ptbe+2,20);
    setle16(pt+6,1);setbe16(ptbe+6,1);
    pt[8]=ptbe[8]=0;
    u8 *dir=image+20u*ISOBLOCK;
    u32 pos=record(dir,20,ISOBLOCK,true,(const char*)&zero,1);
    pos+=record(dir+pos,20,ISOBLOCK,true,(const char*)&one,1);
    const char *name="FILE.TXT;1";
    (void)record(dir+pos,21,n,false,name,10);
    k_memcpy(image+21u*ISOBLOCK,file->data,n);
    if(!check(image,total)){status="ISO9660 metadata validation failed";return false;}
    if(!jfs_write(ISO_OUTPUT,image,total)){
        status="JFS2 refused ISO write (no room or unsafe disk)";return false;
    }
    k_memset(image,0,total);
    i32 read=jfs_read(ISO_OUTPUT,image,sizeof image);
    if(read!=(i32)total || !check(image,(u32)read)){
        status="ISO read-back check failed";return false;
    }
    last_size=total;verified=true;
    (void)shfs_mkdir_abs("/home/falcon/Images");
    status="ISO9660 verified on JFS2; open Files > Images.";
#ifdef FALCON_QEMU_UI_GALLERY
    outb(0xE9,'i');outb(0xE9,'B');
#endif
    return true;
}
bool iso_studio_open_file(const char *path){
    if(!path||k_strcmp(path,ISO_OUTPUT)!=0)return false;
    i32 n=jfs_read(path,image,sizeof image);
    if(n<=0||!check(image,(u32)n)){
        status="ISO readback failed";verified=false;return false;
    }
    last_size=(u32)n;verified=true;
    k_strcpy(active_iso,path);
    status="Verified native ISO9660 volume, stored on FalconFS";
    return true;
}
void iso_studio_key(i32 key){
    if(key=='1')iso_studio_choose("/home/falcon/readme.txt");
    if(key=='2')iso_studio_choose("/home/falcon/hello.sh");
    if(key=='3')iso_studio_choose("/home/falcon/Desktop/Notes.txt");
    if(key==KEY_ENTER)(void)build();
    if(key==KEY_F5)(void)iso_studio_open_file(ISO_OUTPUT);
}
void iso_studio_render(i32 x,i32 y,i32 w,i32 h,u32 frame){
    (void)frame;
    gfx_round_rect(x+20,y+16,w-40,100,20,0x10263Du);
    gfx_round_rect(x+42,y+38,57,57,15,0x52D6B8u);
    gfx_text_lg_centered(x+70,y+48,"I",0x09242Eu);
    gfx_text_lg(x+116,y+31,"ISO Studio",0xFFFFFFu);
    gfx_text(x+118,y+83,"ISO9660 builder  |  safe VM storage",0xB5DAD8u);
    i32 lx=x+37,sy=y+147,rw=w-74;
    gfx_round_rect_a(lx,sy,rw,112,16,PAL_PANEL_DEEP,255);
    gfx_text(lx+20,sy+17,"SOURCE FILE  -  FAPP/1 ISO STUDIO",PAL_ACCENT);
    gfx_text(lx+20,sy+51,source,PAL_TEXT);
    gfx_text(lx+20,sy+79,"1 README.TXT   2 HELLO.SH   3 NOTES.TXT",PAL_TEXT_DIM);
    gfx_round_rect_a(lx,sy+130,rw,156,16,PAL_PANEL_DEEP,255);
    gfx_text(lx+22,sy+148,"ENTER: create single-file ISO9660 archive",PAL_TEXT);
    gfx_text(lx+22,sy+180,"F5: verify stored archive from FalconFS JFS2",PAL_TEXT_DIM);
    gfx_text(lx+22,sy+212,status,verified?COL_OK:PAL_TEXT_DIM);
    if(verified){
        char digits[20];k_itoa((i32)last_size,digits,10);
        gfx_text(lx+22,sy+245,digits,PAL_ACCENT);
        gfx_text(lx+105,sy+245,"bytes - CD001 verified",PAL_TEXT);
    }
    gfx_round_rect(x+37,y+h-93,w-74,69,13,0x473318u);
    gfx_text(x+55,y+h-76,"SAFE: No USB block writing in this version.",0xFFE8AEu);
    gfx_text(x+55,y+h-50,"Future raw USB write requires media identity + explicit consent.",0xE8CEA8u);
}
