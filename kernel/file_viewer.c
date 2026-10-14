#include "file_viewer.h"
static char ascii_lower(char c){return c>='A'&&c<='Z'?(char)(c+32):c;}
static bool extension(const char*path,const char*ext){
    if(!path||!ext)return false;
    u32 n=0,m=0;while(path[n]&&n<256u)n++;
    if(path[n])return false;
    while(ext[m]&&m<16u)m++;
    if(ext[m]||m>n)return false;
    for(u32 i=0;i<m;i++)if(ascii_lower(path[n-m+i])!=ascii_lower(ext[i]))return false;
    return true;
}
fv_kind_t fv_kind(const char *path){
    if(extension(path,".app.pkg")||extension(path,".app.kkg"))return FV_PACKAGE;
    if(extension(path,".bmp"))return FV_BITMAP;
    static const char*const TEXT[]={
        ".txt",".md",".markdown",".log",".json",".csv",".xml",
        ".html",".htm",".css",".js",".mjs",".ts",".py",".fpy",
        ".fal",".fsh",".fs",".c",".h",".cpp",".hpp",".sh",
        ".ini",".cfg",".conf",".yaml",".yml",".toml",".svg"
    };
    for(u32 i=0;i<sizeof TEXT/sizeof *TEXT;i++)
        if(extension(path,TEXT[i]))return FV_TEXT;
    return FV_UNKNOWN;
}
static u16 rd16(const u8*p){return (u16)p[0]|((u16)p[1]<<8);}
static u32 rd32(const u8*p){return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24);}
bool fv_bmp_open(const void *bytes,u32 size,fv_bitmap_t *bmp){
    if(!bytes||!bmp||size<54u)return false;
    const u8*p=(const u8*)bytes;
    if(p[0]!='B'||p[1]!='M'||rd32(p+14)<40u||
       rd16(p+26)!=1u||rd16(p+28)!=24u||rd32(p+30)!=0u)return false;
    u32 w=rd32(p+18),h=rd32(p+22),offset=rd32(p+10);
    /* SHFS files are currently tiny. Reject huge and top-down dimensions. */
    if(!w||!h||w>4096u||h>4096u||offset<54u||offset>size)return false;
    u32 stride=(w*3u+3u)&~3u;
    if(stride>size||h>(size-offset)/stride)return false;
    bmp->width=w;bmp->height=h;bmp->offset=offset;bmp->stride=stride;
    return true;
}
u32 fv_bmp_pixel(const void *bytes,const fv_bitmap_t *bmp,u32 x,u32 y){
    if(!bytes||!bmp||x>=bmp->width||y>=bmp->height)return 0;
    const u8*p=(const u8*)bytes+bmp->offset+
        (bmp->height-1u-y)*bmp->stride+x*3u;
    return ((u32)p[2]<<16)|((u32)p[1]<<8)|p[0];
}
