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
    if(extension(path,".ppm"))return FV_PPM;
    if(extension(path,".wav"))return FV_WAVE;
    static const char*const TEXT[]={
        ".txt",".md",".markdown",".log",".json",".csv",".xml",
        ".html",".htm",".css",".js",".mjs",".ts",".py",".fpy",
        ".fal",".fsh",".fs",".c",".h",".cpp",".hpp",".sh",
        ".ini",".cfg",".conf",".yaml",".yml",".toml",".svg",
        ".gitignore",".properties",".bat",".ps1",".rs",".go",".java"
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
/* PPM P6: exact binary RGB, 8-bit maximum 255; cap allocations to source.
 * Whitespace/comments allowed in header, but never interpreted in pixel bytes.
 * Pixel byte 0x0A is therefore an ordinary color value, not a separator. */
static bool white(u8 c){return c==' '||c=='\r'||c=='\n'||c=='\t';}
static bool ppm_number(const u8*p,u32 size,u32*at,u32*out){
    u32 pos=*at,guards=0;
    for(;;){
        while(pos<size&&white(p[pos]))pos++;
        if(pos<size&&p[pos]=='#'){
            if(++guards>16u)return false;
            while(pos<size&&p[pos]!='\n')pos++;
            continue;
        }
        break;
    }
    if(pos>=size||p[pos]<'0'||p[pos]>'9')return false;
    u32 v=0,count=0;
    while(pos<size&&p[pos]>='0'&&p[pos]<='9'){
        if(++count>6u)return false;
        v=v*10u+(u32)(p[pos++]-'0');
    }
    *at=pos;*out=v;return true;
}
bool fv_ppm_open(const void*bytes,u32 size,fv_bitmap_t*ppm){
    if(!bytes||!ppm||size<12u)return false;
    const u8*p=(const u8*)bytes;
    if(p[0]!='P'||p[1]!='6'||!white(p[2]))return false;
    u32 at=2,w=0,h=0,maxval=0;
    if(!ppm_number(p,size,&at,&w)||!ppm_number(p,size,&at,&h)||
       !ppm_number(p,size,&at,&maxval)||maxval!=255u||
       w==0||h==0||w>4096u||h>4096u||at>=size||!white(p[at]))
        return false;
    /* A single required whitespace separates maxval from raster. */
    if(p[at]=='\r'&&at+1<size&&p[at+1]=='\n')at+=2u;
    else at++;
    u32 stride=w*3u;
    if(at>size||stride>size||h>(size-at)/stride)return false;
    ppm->width=w;ppm->height=h;ppm->offset=at;ppm->stride=stride;
    return true;
}
u32 fv_ppm_pixel(const void*bytes,const fv_bitmap_t*ppm,u32 x,u32 y){
    if(!bytes||!ppm||x>=ppm->width||y>=ppm->height)return 0;
    const u8*p=(const u8*)bytes+ppm->offset+y*ppm->stride+x*3u;
    return ((u32)p[0]<<16)|((u32)p[1]<<8)|p[2];
}
/* RIFF PCM: parse chunk lengths and padding rather than assuming a fixed
 * 44-byte header. Metadata-only until independently verified audio drivers. */
bool fv_wave_open(const void*bytes,u32 size,fv_wave_t*wav){
    if(!bytes||!wav||size<44u)return false;
    const u8*p=(const u8*)bytes;
    if(p[0]!='R'||p[1]!='I'||p[2]!='F'||p[3]!='F'||
       p[8]!='W'||p[9]!='A'||p[10]!='V'||p[11]!='E')return false;
    u32 declared=rd32(p+4);
    if(declared<36u||declared>size-8u)return false;
    u32 end=declared+8u,at=12u,rate=0,data_off=0,data_size=0;
    u16 channels=0,bits=0,align=0;bool have_fmt=false,have_data=false;
    while(at<=end&&end-at>=8u){
        u32 len=rd32(p+at+4),off=at+8u;
        if(len>end-off)return false;
        const u8*id=p+at;
        if(id[0]=='f'&&id[1]=='m'&&id[2]=='t'&&id[3]==' '){
            if(have_fmt||len<16u)return false;
            if(rd16(p+off)!=1u)return false; /* PCM only */
            channels=rd16(p+off+2);rate=rd32(p+off+4);
            align=rd16(p+off+12);bits=rd16(p+off+14);
            if((channels!=1u&&channels!=2u)||rate<8000u||rate>192000u||
               (bits!=8u&&bits!=16u)||align!=channels*(bits/8u))
                return false;
            if(rd32(p+off+8)!=rate*(u32)align)return false;
            have_fmt=true;
        }else if(id[0]=='d'&&id[1]=='a'&&id[2]=='t'&&id[3]=='a'){
            if(have_data)return false;
            data_off=off;data_size=len;have_data=true;
        }
        u32 step=len+(len&1u);
        if(step>end-off)return false;
        at=off+step;
    }
    if(!have_fmt||!have_data||!data_size||data_size%align)return false;
    wav->data_offset=data_off;wav->data_size=data_size;
    wav->sample_rate=rate;wav->channels=channels;wav->bits_per_sample=bits;
    wav->frames=data_size/align;
    return true;
}
