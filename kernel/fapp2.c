/* FAPP/2 - FalconOS native declarative GUI package format.
 * Inert TITLE/TEXT/CARD/BUTTON; no shell execution, no native binary loader,
 * no Chromium or external UI engine. Validate all bytes before displaying. */
#include "falcon.h"
#define GUI_NODES 12
#define GUI_TEXT 112
typedef struct {u8 kind;char label[GUI_TEXT],response[GUI_TEXT];} gui_node_t;
static gui_node_t nodes[GUI_NODES];
static u32 total;
static char app_name[42];
static char app_status[GUI_TEXT];
static u32 accent=0x38B79Du;
static u32 count_line(const char *p,u32 max){
    u32 i=0;while(i<max&&p[i]!='\n')i++;return i;
}
static bool safe_text(const char *p,u32 n){
    if(!n)return false;
    for(u32 i=0;i<n;i++){u8 c=(u8)p[i];if(c<32||c>126||c=='|'||c==92||c==96)return false;}
    return true;
}
static bool valid_id(const char *p,u32 n){
    if(n<2||n>32||p[0]<'a'||p[0]>'z')return false;
    for(u32 i=1;i<n;i++){
        char c=p[i];if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'))return false;
    }
    return true;
}
static bool equals(const char *s,u32 n,const char *want){
    return k_strlen(want)==n&&k_strncmp(s,want,(i32)n)==0;
}
static u32 hex_value(char c){
    if(c>='0'&&c<='9')return (u32)(c-'0');
    if(c>='a'&&c<='f')return (u32)(c-'a'+10);
    if(c>='A'&&c<='F')return (u32)(c-'A'+10);
    return 256u;
}
static bool parse(const char *p,u32 bytes,bool apply){
    if(!p||bytes<78||bytes>=4096||p[bytes]!=0||
       k_strncmp(p,"FAPP/2\n",7)!=0||p[bytes-1]!='\n')return false;
    u32 pos=7;u32 n=0;
    const char *keys[]={"id=","name=","version=","summary="};
    char candidate_name[42];
    for(u32 field=0;field<4;field++){
        u32 prefix=k_strlen(keys[field]);
        if(pos+prefix>=bytes||k_strncmp(p+pos,keys[field],prefix)!=0)return false;
        pos+=prefix;n=count_line(p+pos,bytes-pos);
        if(pos+n>=bytes||!n||n>80||!safe_text(p+pos,n))return false;
        if(field==0&&!valid_id(p+pos,n))return false;
        if(field==1){
            if(n>40)return false;
            k_memcpy(candidate_name,p+pos,n);candidate_name[n]=0;
        }
        if(field==2){
            if(n>24)return false;
            char version[25];k_memcpy(version,p+pos,n);version[n]=0;
            if(!market_version_valid(version))return false;
        }
        if(field==3&&n>75)return false;
        pos+=n+1;
    }
    if(pos>=bytes||p[pos]!='\n')return false;
    pos++;
    if(apply){total=0;accent=0x38B79Du;app_status[0]=0;}
    u32 parsed=0,buttons=0;bool heading=false;
    while(pos<bytes){
        n=count_line(p+pos,bytes-pos);
        if(n==0||pos+n>=bytes||n>232)return false;
        const char *line=p+pos,*divider=NULL;
        for(u32 i=0;i<n;i++)if(line[i]=='|'){divider=line+i;break;}
        if(!divider)return false;
        u32 keylen=(u32)(divider-line);u8 kind=0;
        if(equals(line,keylen,"TITLE"))kind=1;
        else if(equals(line,keylen,"TEXT"))kind=2;
        else if(equals(line,keylen,"CARD"))kind=3;
        else if(equals(line,keylen,"BUTTON"))kind=4;
        else if(equals(line,keylen,"ACCENT"))kind=5;
        else return false;
        const char *value=divider+1;
        u32 length=(u32)(n-(value-line));
        if(kind==5){
            if(length!=6)return false;
            u32 hex=0;
            for(u32 i=0;i<6;i++){
                u32 v=hex_value(value[i]);if(v>15u)return false;hex=(hex<<4)|v;
            }
            if(apply)accent=hex;
        }else{
            if(parsed>=GUI_NODES||!length)return false;
            const char *action=NULL;u32 second_len=0;
            if(kind==4){
                for(u32 j=0;j<length;j++)if(value[j]=='|'){action=value+j+1;break;}
                if(!action)return false;
                u32 titlelen=(u32)((action-1)-value);
                second_len=(u32)((line+n)-action);
                if(!titlelen||!second_len||titlelen>70||second_len>105||
                   !safe_text(value,titlelen)||!safe_text(action,second_len))return false;
                length=titlelen;buttons++;
            }else if(length>105||!safe_text(value,length))return false;
            if(kind==1)heading=true;
            if(apply){
                gui_node_t *dst=&nodes[parsed];dst->kind=kind;
                k_memcpy(dst->label,value,length);dst->label[length]=0;
                dst->response[0]=0;
                if(kind==4){
                    k_memcpy(dst->response,action,second_len);
                    dst->response[second_len]=0;
                }
            }
            parsed++;
        }
        pos+=n+1;
    }
    if(!heading||!parsed||buttons>5)return false;
    if(apply){
        total=parsed;
        k_strcpy(app_name,candidate_name);
        k_strcpy(app_status,"Application ready - buttons act locally");
    }
    return true;
}
bool fapp2_validate(const char *p,u32 length){return parse(p,length,false);}
bool fapp2_load(const char *p,u32 length){return parse(p,length,true);}
const char *fapp2_title(void){return app_name;}
u32 fapp2_accent(void){return accent;}
const char *fapp2_status(void){return app_status;}
u32 fapp2_count(void){return total;}
i32 fapp2_kind(u32 i){return i<total?nodes[i].kind:0;}
const char *fapp2_label(u32 i){return i<total?nodes[i].label:"";}
void fapp2_press(u32 i){
    if(i>=total||nodes[i].kind!=4)return;
    k_strcpy(app_status,nodes[i].response);
#ifdef FALCON_QEMU_UI_GALLERY
    outb(0xE9,'p');outb(0xE9,'B');
#endif
}
