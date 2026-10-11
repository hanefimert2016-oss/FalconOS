/* FScript 0.1 — FalconOS's own declarative app language.
 * This is NOT C, HTML, JavaScript, shell or a Windows PE binary.
 * Executes no native code. Bounded tokenizer + statement evaluator paints
 * controls directly into the FalconOS window manager.
 *
 * Grammar (each line 1 statement, ASCII names, UTF-8 string text permitted):
 * FSC/1
 * uygulama My Counter
 * arka #132238
 * renk #65D3AE
 * baslik Counter
 * metin This is a real FScript window
 * sayac hits 0
 * dugme Click me hits 1
 * deger hits
 * cizgi
 */
#include "falcon.h"
#define FS_MAX_STATEMENTS 40
#define FS_LINE_TEXT 100
#define FS_MAX_VARIABLES 8
typedef enum {FS_TITLE,FS_TEXT,FS_BUTTON,FS_VALUE,FS_LINE} fstype_t;
typedef struct{fstype_t type;char text[FS_LINE_TEXT];i32 var,amount;} fs_stmt;
typedef struct{char key[24];i32 value;} fs_var;
static fs_stmt stmts[FS_MAX_STATEMENTS];
static fs_var vars[FS_MAX_VARIABLES];
static i32 nstmt,nvar,scroll,selected;
static u32 backdrop=0x122138u,accent=0x64D3AEu;
static char title[48]="FScript application";
static const char *status="FScript/1 — interpreted, sandboxed UI";
static bool active=false;
static void copy(u8 *out,i32 cap,const char *src,i32 len){
    if(len>=cap)len=cap-1;
    for(i32 i=0;i<len;i++)out[i]=(u8)src[i];
    out[len]=0;
}
static bool token(const char *value,i32 len){
    if(len<1||len>22)return false;
    for(i32 i=0;i<len;i++){
        char c=value[i];
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
             (c>='0'&&c<='9')||c=='_'))return false;
    }
    return true;
}
static i32 var_index(const char *name){
    for(i32 i=0;i<nvar;i++)if(k_strcmp(name,vars[i].key)==0)return i;
    return -1;
}
static bool color_hex(const char *text,u32 *color){
    if(!text||text[0]!='#'||k_strlen(text)!=7)return false;
    u32 n=0;
    for(i32 i=1;i<7;i++){
        char c=text[i];u32 v=0;
        if(c>='0'&&c<='9')v=c-'0';
        else if(c>='A'&&c<='F')v=c-'A'+10;
        else if(c>='a'&&c<='f')v=c-'a'+10;
        else return false;
        n=(n<<4)|v;
    }
    *color=n;return true;
}
static bool decimal(const char *p,i32 *out){
    if(!p||!*p)return false;
    bool negative=false;
    if(*p=='-'){negative=true;p++;}
    if(!*p)return false;
    i32 n=0;
    while(*p){
        if(*p<'0'||*p>'9')return false;
        if(n>100000)return false;
        n=n*10+*p++-'0';
    }
    *out=negative?-n:n;return true;
}
static bool parse_line(const char *line,i32 size){
    if(!size||line[0]=='#')return true;
    i32 k=0;
    while(k<size&&line[k]!=' ')k++;
    char op[24];copy((u8*)op,sizeof op,line,k);
    const char *arg=k<size?line+k+1:"";
    if(k<size&&!arg[0])return false;
    if(k_strcmp(op,"uygulama")==0){
        if(k_strlen(arg)>44)return false;
        copy((u8*)title,sizeof title,arg,(i32)k_strlen(arg));
        return true;
    }
    if(k_strcmp(op,"arka")==0)return color_hex(arg,&backdrop);
    if(k_strcmp(op,"renk")==0)return color_hex(arg,&accent);
    if(k_strcmp(op,"sayac")==0){
        if(nvar>=FS_MAX_VARIABLES)return false;
        const char *sep=arg;
        while(*sep&&*sep!=' ')sep++;
        i32 n=(i32)(sep-arg);
        char key[24];
        if(!token(arg,n)||!*sep)return false;
        copy((u8*)key,sizeof key,arg,n);
        i32 value;
        if(var_index(key)>=0||!decimal(sep+1,&value))return false;
        k_strcpy(vars[nvar].key,key);
        vars[nvar++].value=value;return true;
    }
    if(k_strcmp(op,"baslik")==0 || k_strcmp(op,"metin")==0 ||
       k_strcmp(op,"dugme")==0 || k_strcmp(op,"deger")==0 ||
       k_strcmp(op,"cizgi")==0){
        if(nstmt>=FS_MAX_STATEMENTS)return false;
        fs_stmt *s=&stmts[nstmt];
        s->var=-1;s->amount=0;
        if(k_strcmp(op,"cizgi")==0){
            if(*arg)return false;
            s->type=FS_LINE;s->text[0]=0;
        }else if(k_strcmp(op,"deger")==0){
            i32 v=var_index(arg);
            if(v<0)return false;
            s->type=FS_VALUE;s->var=v;k_strcpy(s->text,arg);
        }else if(k_strcmp(op,"dugme")==0){
            /* Syntax: dugme Label text -> counter +1
             * Buttons can only mutate a previously declared counter.
             * No filesystem/terminal/network access from this language. */
            const char *arrow=NULL;
            for(const char *p=arg;*p;p++)
                if(p[0]==' '&&p[1]=='-'&&p[2]=='>'&&p[3]==' '){arrow=p;break;}
            if(!arrow||arrow==arg||arrow-arg>=FS_LINE_TEXT)return false;
            const char *next=arrow+4;
            const char *sep=next;
            while(*sep&&*sep!=' ')sep++;
            if(!*sep||!token(next,(i32)(sep-next)))return false;
            char id[24];copy((u8*)id,sizeof id,next,(i32)(sep-next));
            i32 v=var_index(id),amount=0;
            if(v<0||!decimal(sep+1,&amount)||amount<-1000||amount>1000)return false;
            s->var=v;s->amount=amount;s->type=FS_BUTTON;
            copy((u8*)s->text,sizeof s->text,arg,(i32)(arrow-arg));
        }else{
            if(k_strlen(arg)>=FS_LINE_TEXT)return false;
            s->type=k_strcmp(op,"baslik")==0?FS_TITLE:FS_TEXT;
            k_strcpy(s->text,arg);
        }
        nstmt++;
        return true;
    }
    return false;
}
bool fscript_is_source(const char *p,u32 len){
    return p&&len>=7&&len<=4095&&
        k_strncmp(p,"FSC/1\n",6)==0&&p[len-1]=='\n'&&p[len]==0;
}
bool fscript_load(const char *p,u32 len){
    active=false;nstmt=nvar=selected=scroll=0;
    backdrop=0x122138u;accent=0x64D3AEu;
    k_strcpy(title,"FScript application");
    if(!fscript_is_source(p,len)){status="Not a bounded FSC/1 package";return false;}
    u32 pos=6;
    while(pos<len){
        u32 begin=pos;
        while(pos<len&&p[pos]!='\n'){
            u8 c=(u8)p[pos];
            if(c<32||c==127||pos-begin>=160){status="Invalid script character/line";return false;}
            pos++;
        }
        if(pos>=len){status="Unterminated source line";return false;}
        char line[161];copy((u8*)line,sizeof line,p+begin,(i32)(pos-begin));
        if(!parse_line(line,(i32)(pos-begin))){status="Invalid FScript statement";return false;}
        pos++;
    }
    if(nstmt<1){status="FScript app needs visible UI";return false;}
    active=true;status="FScript loaded — native FalconOS controls";
#ifdef FALCON_QEMU_UI_GALLERY
    outb(0xE9,'F');outb(0xE9,'S');
#endif
    return true;
}
const char *fscript_title(void){return active?title:"FScript";}
void fscript_key(i32 key){
    if(!active)return;
    if(key==KEY_UP&&selected>0)selected--;
    if(key==KEY_DOWN&&selected<nstmt-1)selected++;
    if(key==KEY_ENTER||key==' '){
        if(stmts[selected].type==FS_BUTTON&&stmts[selected].var>=0){
            i32 var=stmts[selected].var,delta=stmts[selected].amount;
            if((delta>0&&vars[var].value<100000-delta)||
               (delta<0&&vars[var].value>-100000-delta))
                vars[var].value+=delta;
#ifdef FALCON_QEMU_UI_GALLERY
            outb(0xE9,'F');outb(0xE9,'B');
#endif
        }
    }
}
void fscript_render(i32 x,i32 y,i32 w,i32 h,u32 frame){
    (void)frame;
    if(w<260||h<220)return;
    gfx_round_rect(x+15,y+8,w-30,h-30,20,backdrop);
    gfx_round_rect(x+28,y+20,w-56,76,16,0x0A1725u);
    gfx_circle(x+58,y+58,17,accent);
    gfx_text_lg(x+91,y+32,title,0xECFAFFu);
    gfx_text(x+92,y+76,"FScript 0.1  |  native UI VM  |  arrows + Enter",0xA3CBD7u);
    if(!active){
        gfx_text(x+46,y+132,status,0xF5A6A2u);return;
    }
    i32 cy=y+123;
    for(i32 i=0;i<nstmt&&cy+54<y+h-54;i++){
        fs_stmt *s=&stmts[i];
        if(s->type==FS_TITLE){
            gfx_text_lg(x+50,cy+5,s->text,0xF5FBFFu);
            cy+=70;
        }else if(s->type==FS_TEXT){
            gfx_text(x+50,cy+9,s->text,0xC5DCE4u);
            cy+=39;
        }else if(s->type==FS_BUTTON){
            gfx_round_rect(x+47,cy,w-94,48,13,i==selected?accent:0x244458u);
            gfx_text(x+69,cy+16,s->text,i==selected?0x09222Au:0xE4F4F7u);
            cy+=60;
        }else if(s->type==FS_VALUE){
            char val[24];k_itoa((u32)(vars[s->var].value<0?
                              -vars[s->var].value:vars[s->var].value),val,10);
            gfx_text(x+50,cy+9,s->text,0x91CBBCu);
            if(vars[s->var].value<0){
                char temp[24];k_strcpy(temp,"-");k_strcat(temp,val);k_strcpy(val,temp);
            }
            gfx_text_lg(x+270,cy,val,accent);
            cy+=56;
        }else{gfx_rect(x+50,cy+10,w-100,1,0x516E76u);cy+=28;}
    }
    gfx_text(x+47,y+h-60,status,0x9AB7BEu);
#ifdef FALCON_QEMU_UI_GALLERY
    outb(0xE9,'F');outb(0xE9,'R');
#endif
}
