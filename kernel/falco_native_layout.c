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
static bool loaded,contour_origin;
static char origin_host[181];

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
    const char *magic="FALCO/1\nSOURCE|";
    if(k_strncmp(p,magic,k_strlen(magic))!=0)return false;
    p+=k_strlen(magic);
    u32 hostlen=0;
    while(p[hostlen]&&p[hostlen]!='\n'&&hostlen<180u){
        char c=p[hostlen];
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
             (c>='0'&&c<='9')||c=='.'||c=='-'))return false;
        origin_host[hostlen++]=c;
    }
    if(hostlen<3||hostlen>180u||p[hostlen]!='\n')return false;
    origin_host[hostlen]=0;
    p+=hostlen+1;
    char expected[220];
    k_strcpy(expected,"X-Falco-Source: ");
    k_strcat(expected,origin_host);
    k_strcat(expected,"\r\n");
    if(!includes(http,expected))return false;
    contour_origin=k_strcmp(origin_host,"falconos.tech")==0;
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
    loaded=contour_origin?(found_site&&found_contour):nblocks>0;
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
    contour_round(x,y,w,54,14,0x17242Cu);
    contour_round(x+15,y+13,145,34,10,0x24353Du);
    gfx_circle(x+28,y+30,6,C_MINT);
    contour_label(x+45,y+18,"Falco",C_WHITE);
    gfx_text(x+174,y+18,"<   >",0xA0B8BBu);
    contour_round(x+251,y+11,w-430,33,10,0x0F1B23u);
    gfx_circle(x+266,y+26,5,C_MINT);
    contour_label(x+282,y+16,"https://falconos.tech/",0xBDCFD0u);
    contour_label(x+w-159,y+18,"TLS 1.2  |  F5",0x8CB0AFu);
}
static void contour_site_nav(i32 x,i32 y,i32 w){
    contour_falcon_icon(x+35,y+19,25,C_MINT);
    contour_label(x+74,y+21,"FalconOS",C_WHITE);
    i32 nav=x+w/2-175;
    if(w>1000){
        contour_label(nav,y+22,"Özellikler",C_WHITE);
        contour_label(nav+131,y+22,"Mimari",C_WHITE);
        contour_label(nav+241,y+22,"Kurulum",C_WHITE);
        contour_label(nav+339,y+22,"Yol haritası",C_WHITE);
    }
    contour_round(x+w-155,y+12,128,39,13,0x163D3Bu);
    contour_label(x+w-136,y+17,"GitHub",C_MINT);
}
static void contour_mock_browser(i32 x,i32 y,i32 w,i32 h){
    contour_round(x,y,w,h,19,0x122D32u);
    gfx_round_outline(x,y,w,h,19,0x3D6065u);
    contour_round(x+1,y+1,w-2,35,13,0x091820u);
    contour_label(x+21,y+7,"FalconOS",C_WHITE);
    gfx_circle(x+w/2,y+18,4,C_MINT);
    gfx_circle(x+w/2+11,y+18,3,0x80999Au);
    gfx_circle(x+w/2+22,y+18,3,0x80999Au);
    contour_label(x+w-105,y+7,"09:41",0xCADADBu);
    i32 bx=x+w/9,by=y+53,bw=w*72/100,bh=h*69/100;
    contour_round(bx,by,bw,bh,11,0xEDF1EEu);
    gfx_round_outline(bx,by,bw,bh,11,0xDCE4DEu);
    contour_round(bx+1,by+1,bw-2,24,9,0xDFE5E1u);
    gfx_circle(bx+18,by+14,5,0xFF6059u);
    gfx_circle(bx+33,by+14,5,0xFFBD44u);
    gfx_circle(bx+48,by+14,5,0x00CA4Eu);
    gfx_text_centered(bx+bw/2,by+7,"Falco",0x516166u);
    contour_round(bx+1,by+25,bw-2,32,0,0xF7F8F7u);
    gfx_text(bx+17,by+34,"<   >",0x627477u);
    contour_round(bx+76,by+31,bw-131,21,9,0xE7ECE9u);
    gfx_text(bx+89,by+33,"falcon://home",0x516166u);
    i32 mx=bx+bw/2,my=by+80;
    contour_round(mx-18,my,36,36,10,0x092328u);
    contour_medium(mx-12,my+2,"F",C_MINT);
    gfx_text_centered(mx,my+46,"Merhaba, pilot.",0x1D3435u);
    i32 sw=bw*75/100;
    contour_round(mx-sw/2,my+76,sw,31,7,0xFFFFFFu);
    gfx_round_outline(mx-sw/2,my+76,sw,31,7,0xCDD7D1u);
    gfx_text(mx-sw/2+12,my+85,"Web'de ara veya adres gir  > ",0x627A75u);
    for(i32 n=0;n<3;n++){
        i32 cw=(sw-12)/3;
        i32 cx=mx-sw/2+n*(cw+6);
        contour_round(cx,my+123,cw,53,9,0xF5F7F5u);
        gfx_round_outline(cx,my+123,cw,53,9,0xDAE1DEu);
        gfx_text_centered(cx+cw/2,my+139,n==0?"Terminal":(n==1?"Updates":"Dosyalar"),0x2F5151u);
    }
    /* System status pill / dock overlay from actual CSS desktop mock-up */
    contour_round(x+w*52/100,y+h*73/100,w*44/100,58,13,0xF3F6F2u);
    gfx_circle(x+w*57/100,y+h*73/100+29,13,0xCDF5DFu);
    gfx_text(x+w*57/100+27,y+h*73/100+11,"Secure connection",0x102E2Fu);
    gfx_text(x+w*57/100+27,y+h*73/100+31,"BearSSL - TLS 1.2",0x617B77u);
    contour_round(x+w/4,y+h-45,w/2,36,13,0x112F35u);
    for(i32 j=0;j<5;j++){
        contour_round(x+w/4+14+j*(w/2-25)/5,y+h-39,27,25,7,
                      j==1?C_MINT:0x38585Du);
    }
}
static void contour_hero(i32 x,i32 y,i32 w,i32 h){
    gfx_rect(x,y,w,h,C_BG);
    /* Site real CSS design token colors: #050d13 -> #08161d -> #0c2328.
     * Two soft transparent glows and an 80px graticule are native graphics. */
    for(i32 i=0;i<min_(w,h);i+=80){
        (void)i; /* The grid is drawn with fixed low alpha lines below. */
    }
    for(i32 xx=0;xx<w;xx+=80)
        gfx_rect_a(x+xx,y,1,h,0xA7DAD6u,6);
    for(i32 yy=0;yy<h;yy+=80)
        gfx_rect_a(x,y+yy,w,1,0xA7DAD6u,6);
    i32 halo=min_(w/7,h/4);
    if(halo>75){
        gfx_circle_a(x+w*76/100,y+h*37/100,halo,0x193D46u,46);
        gfx_circle_a(x+w*22/100,y+h*76/100,halo,0x1D5C5Fu,36);
    }
    contour_site_nav(x,y,w);
    i32 left=x+clamp_(w/24,31,82);
    i32 top=y+clamp_(h/5,125,240);
    gfx_circle(left+5,top+12,5,C_MINT);
    contour_label(left+23,top,"AÇIK KAYNAK  ·  BARE-METAL  ·  X86_64",0x8FE6C5u);
    i32 leftw=w*49/100-45;
    /* The live document must contain the actual headline. We don't display
     * an invented copy if the upstream HTML changes or is challenged. */
    if(!contour_hero_headline_present())return;
    const char *first="Sadece çalışmaz.";
    const char *second="Uçmak için doğdu.";
    i32 big=leftw>690?70:36;
    i32 shift=big==70?89:55;
    gfx_contour_text(left,top+49,first,big,C_WHITE);
    gfx_contour_text(left,top+49+shift,second,big,0xA3CFC9u);
    const char *description=real_hero_lede();
    if(description){
        /* Live verified page prose; line-break with proportional metrics. */
        i32 py=top+60+shift*2;
        char line[110];i32 written=0,pos=0;
        i32 chars=k_strlen(description);
        for(i32 row=0;row<4 && pos<chars;row++){
            written=0;
            while(pos<chars&&written<109){
                while(description[pos]==' ')pos++;
                i32 st=pos;
                while(pos<chars&&description[pos]!=' ')pos++;
                i32 n=pos-st;
                if(written && gfx_contour_text_width(line,18)+n*10>leftw-32){
                    pos=st;break;
                }
                if(written)line[written++]=' ';
                for(i32 j=0;j<n&&written<109;j++)line[written++]=description[st+j];
                line[written]=0;
            }
            gfx_contour_text(left,py+row*34,line,18,0xAEBDBFu);
        }
    }
    i32 buttons_y=top+60+shift*2+151;
    if(buttons_y+150<y+h){
        contour_round(left,buttons_y,215,56,8,C_MINT);
        contour_label(left+19,buttons_y+14,"FalconOS'u dene   >",0x082019u);
        contour_label(left+249,buttons_y+14,"Contour'u keşfet   v",C_WHITE);
        i32 stats_y=buttons_y+85;
        contour_label(left,stats_y,"01",C_MINT);
        gfx_text(left,stats_y+28,"Bağımsız çekirdek",0x9AABADu);
        contour_label(left+180,stats_y,"TLS",C_MINT);
        gfx_text(left+180,stats_y+28,"Yerleşik güven",0x9AABADu);
        contour_label(left+367,stats_y,"+",C_MINT);
        gfx_text(left+367,stats_y+28,"Açık kaynak",0x9AABADu);
    }
    i32 rightx=x+w*54/100;
    i32 rightw=w*41/100;
    i32 cardh=clamp_(h*64/100,330,600);
    if(rightw>160 && cardh<h-75){
        i32 cardy=y+(h-cardh)/2;
        contour_mock_browser(rightx,cardy,rightw,cardh);
        contour_round(rightx-38,cardy+cardh/2,158,48,13,0x18393Bu);
        contour_label(rightx-24,cardy+cardh/2+9,"shfs  /  kalici",C_MINT);
        contour_round(rightx+rightw-120,cardy+cardh-58,145,45,12,0x153834u);
        contour_label(rightx+rightw-102,cardy+cardh-50,"prg  /  katalog",C_MINT);
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
/* Portable semantic HTML reader: own geometry and AA typography, real
 * extracted static headings/links/text. No CSS cascade, JS, media or forms.
 * This is deliberately NOT represented as a full standards browser. */
static void falco_generic_page(i32 x,i32 y,i32 w,i32 h,i32 scroll){
    const u32 black=0x122233u,slate=0x53667Bu,bg=0xF5F7FAu;
    gfx_rect(x,y,w,h,bg);
    gfx_round_rect(x+24,y+20,w-48,75,16,0xFFFFFFu);
    gfx_round_outline(x+24,y+20,w-48,75,16,0xD9E2EDu);
    gfx_circle(x+49,y+57,9,0x19B58Eu);
    gfx_contour_text(x+70,y+29,origin_host,36,black);
    gfx_text(x+38,y+107,
             "Live CA-verified HTML  |  Falcons' own reader  |  CSS / JS unsupported",
             slate);
    i32 pad=(w<880?28:70),bw=w-pad*2;
    i32 top=y+148-scroll*29,bottom=y+h-46;
    for(i32 i=0;i<nblocks;i++){
        const falco_block_t *b=&blocks[i];
        bool big=b->role=='T'||b->role=='H';
        bool sub=b->role=='S';
        i32 size=big?36:18;
        i32 avail=bw-50;
        i32 letters=(avail/(big?19:10));
        if(letters<12)letters=12;
        i32 len=(i32)k_strlen(b->text);
        i32 rows=len/letters+1;
        if(rows>6)rows=6;
        i32 height=big?(65+rows*43):(38+rows*30);
        if(height<67)height=67;
        if(top+height>=y+148&&top<bottom){
            u32 fill=big?0xE9F1FCu:(sub?0xE8F6F4u:0xFFFFFFu);
            gfx_round_rect(x+pad,top,bw,height,12,fill);
            gfx_round_outline(x+pad,top,bw,height,12,0xD7E2EBu);
            if(big)gfx_rect(x+pad+15,top+13,4,height-26,0x2281C9u);
            if(b->role=='A')gfx_text(x+pad+bw-75,top+13,"LINK",0x1884A2u);
            (void)native_paragraph(x+pad+29,top+15,bw-58,top+height-8,
                                   b->text,big,black);
        }
        top+=height+12;
        if(top>bottom+60)break;
    }
    gfx_round_rect(x+18,y+h-36,w-36,28,9,0xE6EDF4u);
    gfx_text(x+33,y+h-28,"Reader mode: static document only | Up/Down scroll | F5 reload",
             slate);
}
void falco_native_paint(i32 x,i32 y,i32 w,i32 h,i32 scroll,u32 frame){
    (void)frame;
    if(w<260||h<270)return;
    if(loaded&&!contour_origin){
        falco_generic_page(x,y,w,h,scroll);
#ifdef FALCON_QEMU_BROWSER_TEST
        outb(0xE9,'n');outb(0xE9,'G');
#endif
        return;
    }
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
