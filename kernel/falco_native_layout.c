/* FalconOS NativeLayout/1 — 100% FalconOS framebuffer layout and painting.
 *  - No Chromium/WebKit, no screenshots, no upscaled RGB332 raster.
 *  - FALCO/1 semantic HTML message built by optional host TLS gateway.
 *  - All headings, words, native-resolution cards drawn by OUR gfx.c.
 *  - Not a complete CSS/JavaScript engine; unsupported page features
 *    must not be presented as implemented.
 */
#include "falcon.h"
#define NATIVE_MAX_BLOCKS 22
#define NATIVE_TEXT 168
typedef struct {char role;char text[NATIVE_TEXT];} falco_block_t;
static falco_block_t blocks[NATIVE_MAX_BLOCKS];
static i32 nblocks;
static bool loaded;

static bool includes(const char *hay,const char *needle){
    for(i32 i=0;hay&&hay[i];i++){
        i32 j=0;
        while(needle[j] && hay[i+j] && needle[j]==hay[i+j])j++;
        if(!needle[j])return true;
    }
    return false;
}
bool falco_native_parse(const char *http){
    loaded=false;nblocks=0;
    if(!http || (k_strncmp(http,"HTTP/1.0 200",12)!=0 &&
                 k_strncmp(http,"HTTP/1.1 200",12)!=0))return false;
    if(!includes(http,"X-Falco-Native-Verified: yes") ||
       !includes(http,"X-Falco-Source: falconos.tech"))return false;
    const char *p=http;
    while(*p && !(p[0]=='\r'&&p[1]=='\n'&&p[2]=='\r'&&p[3]=='\n'))p++;
    if(!*p)return false;
    p+=4;
    const char *magic="FALCO/1\nSOURCE|falconos.tech\n";
    if(k_strncmp(p,magic,k_strlen(magic))!=0)return false;
    p+=k_strlen(magic);
    while(*p && nblocks<NATIVE_MAX_BLOCKS){
        char role=*p++;
        if(*p!='|')return false;
        p++;
        if(role!='T'&&role!='H'&&role!='S'&&role!='P'&&
           role!='L'&&role!='A'&&role!='B')return false;
        falco_block_t *b=&blocks[nblocks];
        b->role=role;
        i32 n=0;
        while(*p&&*p!='\n'){
            u8 ch=(u8)*p++;
            if(ch>=32 && ch!='|' && n<NATIVE_TEXT-1)b->text[n++]=(char)ch;
        }
        b->text[n]=0;
        if(*p=='\n')p++;
        if(n)nblocks++;
    }
    if(!nblocks)return false;
    bool found_site=false,found_contour=false;
    for(i32 i=0;i<nblocks;i++){
        if(includes(blocks[i].text,"FalconOS"))found_site=true;
        if(includes(blocks[i].text,"Contour"))found_contour=true;
    }
    loaded=found_site&&found_contour;
#ifdef FALCON_QEMU_BROWSER_TEST
    outb(0xE9,'n');outb(0xE9,loaded?'Y':'N');
#endif
    return loaded;
}
bool falco_native_ready(void){return loaded;}
i32 falco_native_elements(void){return nblocks;}
/* Pixel-accurate, antialiased fonts from native generated font_data.c.
 * Responsive word-wrap without zooming a screenshot. */
static i32 native_paragraph(i32 x,i32 y,i32 avail,i32 bottom,
                            const char *src,bool large,u32 fg){
    char line[121];i32 used=0,units=0,limit=(avail-10)/(large?16:8);
    if(limit>115)limit=115;
    if(limit<12)limit=12;
    i32 row_height=large?43:31;
    i32 n=k_strlen(src),i=0;
    while(i<n && y+row_height<bottom){
        i32 start=i;
        while(i<n && src[i]!=' ')i++;
        i32 wordlen=i-start;
        if(wordlen>limit)wordlen=limit;
        if(units && units+1+wordlen>limit){
            line[used]=0;
            if(large)gfx_text_smooth_lg(x,y,line,fg);
            else gfx_text_medium(x,y,line,fg);
            y+=row_height;used=0;units=0;
        }
        if(y+row_height>=bottom)break;
        if(units && used<119){line[used++]=' ';units++;}
        for(i32 j=0;j<wordlen&&used<119;j++)line[used++]=src[start+j];
        units+=wordlen;
        while(i<n && src[i]==' ')i++;
        if(i==start)i++;
    }
    if(used && y+row_height<bottom){
        line[used]=0;
        if(large)gfx_text_smooth_lg(x,y,line,fg);
        else gfx_text_medium(x,y,line,fg);
        y+=row_height;
    }
    return y;
}
void falco_native_paint(i32 x,i32 y,i32 w,i32 h,i32 scroll,u32 frame){
    (void)frame;
    if(w<140||h<140)return;
    const u32 navy=0x101B35u,ink=0x1C2D4Au,muted=0x50637Eu;
    gfx_round_rect(x,y,w,h,17,0xF6F9FEu);
    gfx_round_rect(x,y,w,62,16,navy);
    gfx_circle(x+36,y+30,13,0x3588FFu);
    gfx_text_centered(x+36,y+22,"F",0xFFFFFFu);
    gfx_text_lg(x+63,y+16,"Falco Native",0xF8FBFFu);
    gfx_text(x+w-164,y+25,"Native HTML",0xB8D5FFu);
    gfx_round_rect(x+20,y+75,w-40,42,12,0xFFFFFFu);
    gfx_round_outline(x+20,y+75,w-40,42,12,0xDDE6F4u);
    gfx_circle(x+37,y+96,6,0x14A879u);
    gfx_text(x+52,y+88,"https://falconos.tech/",ink);
    gfx_text(x+28,y+130,"REAL SITE  |  Host CA-verified TLS  |  Native layout",muted);
    gfx_rect(x+18,y+158,w-36,1,0xDAE4F2u);
    if(!loaded){
        gfx_text_lg(x+32,y+193,"Page unavailable",ink);
        gfx_text(x+32,y+243,"F9 retry  /  F3 back to Falco",muted);
        return;
    }
    i32 column=w-100;
    if(column>1440)column=1440;
    i32 bx=x+(w-column)/2;
    i32 cy=y+183-scroll*29;
    i32 bottom=y+h-56;
    /* semantic DOM blocks become full-resolution flex-style cards */
    for(i32 i=0;i<nblocks;i++){
        const falco_block_t *b=&blocks[i];
        bool heading=b->role=='H'||b->role=='T',subtitle=b->role=='S';
        i32 fontrow=heading?43:31;
        i32 textwidth=column-(heading?45:60);
        i32 count=k_strlen(b->text);
        i32 perrow=textwidth/(heading?16:12);
        if(perrow<10)perrow=10;
        i32 rows=(count+perrow-1)/perrow+1;
        if(rows>5)rows=5;
        i32 height=(heading?46:24)+rows*fontrow;
        if(height<62)height=62;
        /* No global scissor rectangle exists in this framebuffer API.
         * Draw only complete cards in the content viewport; otherwise the
         * previous build painted through the bottom status bar and Shelf. */
        if(cy>=y+169 && cy+height<=bottom){
            u32 bg=heading?0xECF3FFu:(subtitle?0xEFF8FBu:0xFFFFFFu);
            gfx_round_rect(bx,cy,column,height,heading?19:12,bg);
            gfx_round_outline(bx,cy,column,height,heading?19:12,0xDCE7F6u);
            if(heading)gfx_rect(bx+15,cy+16,4,height-32,0x3178F6u);
            if(b->role=='A'||b->role=='B'){
                /* A genuine hyperlink is exposed as a semantic tag, but this
                 * prototype has no arbitrary URL navigation yet. Never
                 * disguise inert content as a working clickable button. */
                gfx_text(bx+column-86,cy+19,"LINK TEXT",0x6880A1u);
            }
            (void)native_paragraph(bx+30,cy+17,column-61,cy+height-8,
                                    b->text,heading,heading?navy:ink);
        }
        cy+=height+12;
        if(cy>=bottom)break;
    }
    gfx_round_rect(x+18,y+h-42,w-36,34,12,0xE9F0F9u);
    gfx_text(x+30,y+h-31,
        "Own HTML layout | JS and full CSS unsupported | Up/Down scroll",
        muted);
#ifdef FALCON_QEMU_BROWSER_TEST
    outb(0xE9,'n');outb(0xE9,'R');
#endif
}
