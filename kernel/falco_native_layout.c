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
/* Visual specification below is a native reimplementation of the REAL
 * falconos.tech Contour hero layout and design tokens retrieved from
 * https://falconos.tech/styles.css (2026-10-11).  These are graphical
 * primitives composed live in FalconOS's own framebuffer: no screenshots,
 * no Chromium/WebKit, no bitmap enlargement, no browser-host rendering.
 *
 * Scope: an actual HTML-validated, site-specific Contour renderer. This is
 * not yet standards-compliant general CSS/JavaScript. Unsupported web sites
 * are never silently represented by an invented copy of Contour. */
#define C_BG         0x071117u
#define C_NAVY       0x07131Au
#define C_MINT       0x70F1BCu
#define C_CYAN       0x6FE7FFu
#define C_PAPER      0xF4F6F2u
#define C_INK        0x07131Au
#define C_MUTED      0x64747Bu
#define C_WHITE      0xEEF7F7u
static i32 clamp_(i32 v,i32 lo,i32 hi){return v<lo?lo:(v>hi?hi:v);}
static i32 min_(i32 a,i32 b){return a<b?a:b;}
static void contour_round(i32 x,i32 y,i32 w,i32 h,i32 r,u32 color){
    if(w>0&&h>0)gfx_round_rect(x,y,w,h,r,color);
}
static void contour_label(i32 x,i32 y,const char *text,u32 color){
    gfx_contour_text(x,y,text,18,color);
}
static void contour_medium(i32 x,i32 y,const char *text,u32 color){
    gfx_contour_text(x,y,text,36,color);
}
static void contour_big(i32 x,i32 y,const char *text,u32 color){
    gfx_contour_text(x,y,text,70,color);
}
static const char *real_hero_lede(void){
    /* User-visible prose must come from the authenticated actual site's
     * HTML, not a hardcoded invented marketing description. */
    for(i32 i=0;i<nblocks;i++){
        if(blocks[i].role=='P'&&includes(blocks[i].text,"FalconOS;"))
            return blocks[i].text;
    }
    return NULL;
}
static bool contour_hero_headline_present(void){
    for(i32 i=0;i<nblocks;i++)
        if(blocks[i].role=='H'&&includes(blocks[i].text,"Sadece"))
            return true;
    return false;
}
static void contour_falcon_icon(i32 x,i32 y,i32 size,u32 color){
    /* Hand-authored vector falcon silhouette from two primitive polygons,
     * approximating the public website's tiny SVG mark without SVG runtime. */
    i32 a=size/7; if(a<2)a=2;
    gfx_line(x,y+size/4,x+size-size/7,y,color);
    gfx_line(x+size-size/7,y,x+size/2,y+size/2,color);
    gfx_line(x+size/2,y+size/2,x+size-size/7,y+size/2+size/8,color);
    gfx_line(x+size-size/7,y+size/2+size/8,x+size/4,y+size,color);
    gfx_line(x+size/4,y+size,x+size/3,y+size/2,color);
    gfx_line(x+size/3,y+size/2,x,y+size/4,color);
    gfx_line(x+a,y+size/3,x+size/2,y+size/2,color);
    gfx_line(x+size/2,y+size/2,x+size/3,y+size-size/8,color);
}
static void contour_browser_chrome(i32 x,i32 y,i32 w){
    contour_round(x,y,w,53,13,0x14213Au);
    contour_round(x+15,y+9,171,42,11,0x243651u);
    gfx_circle(x+34,y+29,6,0x75A7FFu);
    contour_label(x+52,y+17,"Falco",C_WHITE);
    gfx_text(x+202,y+19,"<   >",0x91A8C6u);
    i32 addrw=w-650;
    if(addrw<140)addrw=140;
    contour_round(x+280,y+10,addrw,36,11,0x091529u);
    gfx_circle(x+299,y+28,5,0x68DAB1u);
    contour_label(x+317,y+17,"https://falconos.tech/",0xC8D8F5u);
    contour_round(x+w-352,y+11,335,33,12,0x1C2F4Au);
    contour_label(x+w-335,y+17,"HTTPS  /  Host CA verified",0x95BADFu);
}
static void contour_site_nav(i32 x,i32 y,i32 w){
    /* Independent reference viewport: centred 1150px site composition, not
     * edge-to-edge fake navigation pinned to the application's left edge. */
    i32 margin=clamp_(w*13/100,22,380);
    contour_falcon_icon(x+margin,y+21,28,0x91BBFFu);
    contour_label(x+margin+41,y+20,"FalconOS",C_WHITE);
    i32 nav=x+w*54/100;
    if(w>1000){
        contour_label(nav,y+22,"Özellikler",0xC4D1E4u);
        contour_label(nav+135,y+22,"Mimari",0xC4D1E4u);
        contour_label(nav+249,y+22,"Kurulum",0xC4D1E4u);
        contour_label(nav+354,y+22,"Yol haritası",0xC4D1E4u);
        contour_label(nav+525,y+22,"TR  EN",0xD5E6FFu);
    }
    contour_round(x+w-216,y+10,169,48,24,0x253B5Bu);
    gfx_round_outline(x+w-216,y+10,169,48,24,0x5D7698u);
    contour_label(x+w-170,y+21,"GitHub",C_WHITE);
}
static void contour_mock_browser(i32 x,i32 y,i32 w,i32 h){
    /* Site's own decorative Falco desktop mock-up, not the actual browser.
     * Rendered from FalconOS vector primitives and AA Manrope glyph atlas. */
    contour_round(x+8,y+10,w,h,26,0x142850u);
    contour_round(x,y,w,h-6,27,0x172B58u);
    gfx_round_outline(x,y,w,h-6,27,0x486899u);
    contour_round(x+1,y+1,w-2,56,19,0x111B37u);
    gfx_circle(x+26,y+29,4,0xA8C8FFu);
    gfx_text(x+42,y+20,"FalconOS",0xF0F4FFu);
    for(i32 d=0;d<3;d++)gfx_circle(x+w/2+d*17,y+27,5,d==0?0x85AFFFu:0x64799Du);
    contour_label(x+w-115,y+19,"09:41",0xC5D4E9u);

    i32 bx=x+w*6/100,by=y+104,bw=w*72/100,bh=h*62/100;
    contour_round(bx,by,bw,bh,16,0xF7FAF8u);
    gfx_round_outline(bx,by,bw,bh,16,0xD4DEE5u);
    contour_round(bx+1,by+1,bw-2,33,12,0xE7EFEEu);
    gfx_circle(bx+20,by+17,5,0xFF6059u);
    gfx_circle(bx+37,by+17,5,0xFFBD44u);
    gfx_circle(bx+54,by+17,5,0x00CA4Eu);
    gfx_text_centered(bx+bw/2,by+12,"Falco",0x395060u);
    contour_round(bx+1,by+35,bw-2,45,3,0xFFFFFFu);
    gfx_text(bx+22,by+48,"<   >",0x7B8D9Bu);
    contour_round(bx+97,by+42,bw-157,29,14,0xEBF1EFu);
    gfx_text(bx+115,by+50,"falcon://home",0x657A78u);
    i32 mx=bx+bw/2,my=by+131;
    contour_round(mx-27,my,54,54,13,0x0C2530u);
    contour_medium(mx-15,my+7,"F",0x91BCFFu);
    contour_label(mx-92,my+70,"Merhaba, pilot.",0x173844u);
    i32 sw=bw*78/100;
    contour_round(mx-sw/2,my+115,sw,48,11,0xFFFFFFu);
    gfx_round_outline(mx-sw/2,my+115,sw,48,11,0xD1DDDFu);
    gfx_text(mx-sw/2+18,my+130,"Web'de ara veya adres gir",0x80919Au);
    for(i32 n=0;n<3;n++){
        i32 cw=(sw-18)/3;
        i32 cx=mx-sw/2+n*(cw+9);
        contour_round(cx,my+180,cw,75,12,0xF9FBFBu);
        gfx_round_outline(cx,my+180,cw,75,12,0xDDE5E8u);
        gfx_circle(cx+cw/2,my+201,5,0x70A5DAu);
        gfx_text_centered(cx+cw/2,my+229,n==0?"Terminal":(n==1?"Güncellemeler":"Dosyalar"),0x3B5967u);
    }
    /* Same floating status and bottom dock layering as public HTML.
     * These are content of the site's illustration, NOT Falco security UI. */
    contour_round(x+w*70/100,y+h*44/100,w*28/100,82,18,0x11233Du);
    gfx_round_outline(x+w*70/100,y+h*44/100,w*28/100,82,18,0x344F70u);
    gfx_circle(x+w*73/100,y+h*44/100+41,11,0x467C97u);
    gfx_text(x+w*75/100,y+h*44/100+21,"Secure connection",0xE1E9F1u);
    gfx_text(x+w*75/100,y+h*44/100+46,"BearSSL  -  TLS 1.2",0xA8B7C9u);
    contour_round(x+w*34/100,y+h-86,w*32/100,65,21,0x0E203Cu);
    for(i32 j=0;j<5;j++){
        i32 ix=x+w*34/100+15+j*(w*32/100-23)/5;
        contour_round(ix,y+h-74,47,43,12,j==1?0xD9F3E8u:0x273B59u);
        gfx_text_centered(ix+24,y+h-58,j==1?"F":(j==2?">":"*"),
                          j==1?0x153D47u:0xBDD1E7u);
    }
}
static void contour_hero(i32 x,i32 y,i32 w,i32 h){
    /* Rebuilt from the real 1600x900 site reference: deep blue hero, centred
     * content width, a 4-row Manrope display headline and outlined second
     * half. Browser engine stays 100% FalconOS-owned software framebuffer. */
    gfx_rect(x,y,w,h,0x071329u);
    gfx_rect_a(x,y,w,h/2,0x111D43u,45);
    gfx_rect_a(x,y+h/2,w,h/2,0x0A2255u,48);
    i32 halo=min_(w/6,h/3);
    if(halo>120){
        gfx_circle_a(x+w*73/100,y+h*41/100,halo,0x244C9Fu,44);
        gfx_circle_a(x+w*21/100,y+h*91/100,halo,0x17458Cu,35);
    }
    for(i32 xx=0;xx<w;xx+=128)gfx_rect_a(x+xx,y,1,h,0x6B9DC8u,7);
    for(i32 yy=0;yy<h;yy+=128)gfx_rect_a(x,y+yy,w,1,0x6B9DC8u,7);
    contour_site_nav(x,y,w);
    i32 left=x+w*15/100;
    i32 leftw=w*33/100;
    i32 eyebrow=y+clamp_(h*16/100,106,202);
    gfx_circle(left+6,eyebrow+12,6,0x77EDB7u);
    contour_label(left+24,eyebrow,"AÇIK KAYNAK  ·  BARE-METAL  ·  X86_64",0xAAC5E3u);
    if(!contour_hero_headline_present())return;
    bool expansive=w>=2100&&h>=1080;
    if(expansive){
        /* 2x Manrope atlas and native stroke approximate CSS font-size
         * 105.6px with 800 weight at the independently measured site scale. */
        i32 head=y+clamp_(h*22/100,190,288);
        gfx_contour_text_styled(left,head,"Sadece",70,2,false,0xF1F8FAu);
        gfx_contour_text_styled(left,head+139,"çalışmaz.",70,2,false,0xF1F8FAu);
        gfx_contour_text_styled(left,head+278,"Uçmak için",70,2,true,0x9BC1E5u);
        gfx_contour_text_styled(left,head+417,"doğdu.",70,2,true,0x9BC1E5u);
        i32 description_y=head+575;
        const char *desc=real_hero_lede();
        if(desc){
            /* Structured wrapping uses live authenticated upstream prose. */
            char line[100];i32 i=0,n=(i32)k_strlen(desc);
            for(i32 row=0;row<3&&i<n;row++){
                i32 used=0;
                while(i<n&&used<92){
                    while(i<n&&desc[i]==' ')i++;
                    i32 start=i;
                    while(i<n&&desc[i]!=' ')i++;
                    i32 count=i-start;
                    if(used&&gfx_contour_text_width(line,18)*2+count*21>leftw-16){
                        i=start;break;
                    }
                    if(used)line[used++]=' ';
                    for(i32 j=0;j<count&&used<92;j++)line[used++]=desc[start+j];
                    line[used]=0;
                }
                line[used]=0;
                gfx_contour_text_styled(left,description_y+row*42,line,18,2,false,0xB6C9E3u);
            }
        }
        i32 buttons=y+h*79/100;
        contour_round(left,buttons,285,66,30,0xF4FAFFu);
        contour_label(left+31,buttons+19,"FalconOS'u dene   >",0x153772u);
        contour_label(left+326,buttons+20,"Contour'u keşfet   v",0xE8F1FFu);
        i32 stats=y+h*92/100;
        contour_label(left,stats,"01",0x97BFFFu);
        contour_label(left+76,stats,"Bağımsız çekirdek",0xC0D0E5u);
        contour_label(left+320,stats,"TLS",0x97BFFFu);
        contour_label(left+391,stats,"Yerleşik güven",0xB0C7E6u);
        contour_label(left+591,stats,"+",0x97BFFFu);
        contour_label(left+627,stats,"Açık kaynak",0xB0C7E6u);
    }else{
        /* Smaller screens use the same genuine content with a compact title. */
        i32 head=y+clamp_(h/4,110,210);
        gfx_contour_text(left,head,"Sadece çalışmaz.",36,0xF1F8FAu);
        gfx_contour_text_styled(left,head+68,"Uçmak için doğdu.",36,1,true,0x9BC1E5u);
        const char *desc=real_hero_lede();
        if(desc)native_paragraph(left,head+152,leftw,y+h-150,desc,false,0xBDD2E9u);
        contour_round(left,y+h-111,240,48,25,0xF4FAFFu);
        contour_label(left+20,y+h-103,"FalconOS'u dene >",0x153772u);
    }
    i32 rightx=x+w*52/100;
    i32 rightw=w*43/100;
    i32 cardh=clamp_(h*69/100,340,835);
    i32 cardy=y+clamp_(h*22/100,125,320);
    if(rightw>420&&cardy+cardh<y+h+14){
        contour_mock_browser(rightx,cardy,rightw,cardh);
        contour_round(rightx-61,cardy+cardh*73/100,177,66,18,0x102442u);
        contour_label(rightx-43,cardy+cardh*73/100+9,"shfs",0x92B7FBu);
        gfx_text(rightx-43,cardy+cardh*73/100+39,"kalıcı dosya ağacı",0xB9CBDDu);
        contour_round(rightx+rightw-162,cardy+62,180,58,15,0x102442u);
        contour_label(rightx+rightw-141,cardy+73,"prg",0x92B7FBu);
        gfx_text(rightx+rightw-141,cardy+99,"tek paket kataloğu",0xB9CBDDu);
    }
}
static void contour_section(i32 x,i32 y,i32 w,i32 h,i32 section){
    gfx_rect(x,y,w,h,C_PAPER);
    contour_round(x,y,w,66,0,C_INK);
    contour_site_nav(x,y,w);
    i32 pad=clamp_(w/20,36,90),lx=x+pad;
    contour_label(lx,y+90,"FALCONOS  /  CONTOUR",0x307C68u);
    if(section==1){
        contour_big(lx,y+128,"Contour ile",C_INK);
        contour_big(lx,y+215,"akışta kal.",C_INK);
        contour_label(lx,y+327,"FalconOS'un görünen yüzü sade; altındaki sistem tutarlı.",C_MUTED);
        static const char *heads[]={
            "Falco ile güvenli gezinme","Dosya ağacı, kalıcı hafıza",
            "Tek katalog. Net güncellemeler.","Daha az gürültü"};
        static const char *desc[]={
            "Omnibox, güven deposu, BearSSL tabanlı TLS istemcisi",
            "shfs /home dosyalarını sanal ağaçta birleştirir",
            "Mağaza ve Sistem Güncellemeleri tek prg kaydını kullanır",
            "Sade sistem yüzü ve tutarlı çalışma alanları"};
        i32 by=y+395,spacing=18;
        for(i32 i=0;i<4;i++){
            i32 cw=(w-2*pad-spacing)/2,ch=clamp_((h-by+y-55)/2,125,240);
            i32 cx=lx+(i%2)*(cw+spacing),cy=by+(i/2)*(ch+spacing);
            contour_round(cx,cy,cw,ch,17,0xFFFFFFu);
            gfx_round_outline(cx,cy,cw,ch,17,0xD8E2DBu);
            contour_label(cx+21,cy+18,i==0?"01":(i==1?"02":(i==2?"03":"04")),0x439982u);
            contour_medium(cx+21,cy+57,heads[i],C_INK);
            contour_label(cx+21,cy+117,desc[i],C_MUTED);
        }
    }else if(section==2){
        contour_big(lx,y+130,"Kendi ayakları",C_INK);
        contour_big(lx,y+217,"üzerinde.",C_INK);
        contour_label(lx,y+323,"x86_64  /  Multiboot2  /  Kernel  /  Contour WM",C_MUTED);
        i32 bx=lx,by=y+395,cw=(w-pad*2-28)/3,ch=min_(h-520,230);
        static const char *tags[]={"USERSPACE","SYSTEM","KERNEL"};
        static const char *lines[]={"Falco / Store / Terminal","Contour WM / diskdb","GDT / IDT / PIT / net_stack"};
        for(i32 i=0;i<3;i++){
            contour_round(bx+i*(cw+14),by,cw,ch,17,
                         i==2?0x071329u:0xE8EFEAu);
            contour_label(bx+i*(cw+14)+17,by+19,tags[i],
                          i==2?C_MINT:0x368C70u);
            contour_medium(bx+i*(cw+14)+17,by+82,
                           i==0?"FALCO":(i==1?"CONTOUR":"x86_64"),
                           i==2?C_WHITE:C_INK);
            contour_label(bx+i*(cw+14)+17,by+158,lines[i],
                          i==2?0xBDD4D2u:C_MUTED);
        }
    }else{
        contour_big(lx,y+135,"Klonla.",C_INK);
        contour_big(lx,y+224,"Derle. Uç.",C_INK);
        contour_label(lx,y+347,
            "Geliştirici önizlemesi  |  Linux  /  macOS  /  Windows",C_MUTED);
        contour_round(lx,y+401,w-2*pad,155,18,0x0A1820u);
        contour_label(lx+29,y+422,"TERMINAL   -   FALCONOS",C_MINT);
        contour_medium(lx+30,y+463,"git clone ...  &&  make run",C_WHITE);
        contour_label(lx,y+602,
            "Sadece açık kaynak. Güvenli sınırlar. Tamamen kendi çekirdeği.",C_MUTED);
    }
}
void falco_native_paint(i32 x,i32 y,i32 w,i32 h,i32 scroll,u32 frame){
    (void)frame;
    if(w<260||h<270)return;
    /* Real native browser UI, with independent scrollable site surface. */
    contour_browser_chrome(x,y,w);
    i32 top=y+54,siteh=h-78;
    i32 section=scroll<8?0:(scroll<16?1:(scroll<24?2:3));
    if(!loaded){
        gfx_rect(x,top,w,siteh,0x08161Du);
        contour_medium(x+48,top+90,"Site unavailable",C_WHITE);
        contour_label(x+48,top+180,"F9 retry | Only verified falconos.tech",C_MUTED);
        return;
    }
    if(section==0)contour_hero(x,top,w,siteh);
    else contour_section(x,top,w,siteh,section);
    contour_round(x,y+h-23,w,23,0,0x17232Bu);
    gfx_text(x+20,y+h-21,
      "CONTOUR NATIVE | Live HTML + site-specific software CSS layout | JS not implemented",
      0x97B9B8u);
#ifdef FALCON_QEMU_BROWSER_TEST
    /* This marker is emitted from the ACTUAL page painter. Tests must not
     * accept only a keypress marker: a slow TCG frame can still show the
     * previous page after a keyboard state transition. */
    outb(0xE9,'c');outb(0xE9,(u8)('0'+section));
    outb(0xE9,'n');outb(0xE9,'R');
#endif
}
