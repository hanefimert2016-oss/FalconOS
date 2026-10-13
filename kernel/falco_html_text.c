/* Falco SemanticText/2. Every decoded byte is inert framebuffer text.
 * Blocks are line-separated, script/style/noscript/template/svg content
 * never reaches the UI, and buffers are bounded before every write.
 * This is a semantic HTML reader, NOT a modern CSS/JS browser engine. */
#include "falco_html_text.h"
static char lower(char c){return c>='A'&&c<='Z'?(char)(c+32):c;}
static bool name_eq(const char*a,const char*b){
    while(*a&&*b){if(lower(*a++)!=lower(*b++))return false;}
    return !*a&&!*b;
}
static bool letter(char c){return (c>='A'&&c<='Z')||(c>='a'&&c<='z');}
static void append(char*out,u32 cap,u32*used,char c){
    if(*used+1<cap)out[(*used)++]=c;
}
static void break_line(char*out,u32 cap,u32*used,bool paragraph){
    if(*used && out[*used-1]==' ')--*used;
    if(!*used)return;
    if(out[*used-1]!='\n')append(out,cap,used,'\n');
    if(paragraph&&*used&&out[*used-1]=='\n'&&
       (*used<2||out[*used-2]!='\n'))append(out,cap,used,'\n');
}
static bool entity(const char*p,u32 *consumed,char out[4],u32*n){
    const char*end=p;u32 length=0;
    while(*end&&*end!=';'&&length<12){end++;length++;}
    if(*end!=';'||!length)return false;
    u32 value=0;
    if(length==3&&p[0]=='a'&&p[1]=='m'&&p[2]=='p')value='&';
    else if(length==2&&p[0]=='l'&&p[1]=='t')value='<';
    else if(length==2&&p[0]=='g'&&p[1]=='t')value='>';
    else if(length==4&&p[0]=='q'&&p[1]=='u'&&p[2]=='o'&&p[3]=='t')value='"';
    else if(length==4&&p[0]=='a'&&p[1]=='p'&&p[2]=='o'&&p[3]=='s')value=39;
    else if(length==4&&p[0]=='n'&&p[1]=='b'&&p[2]=='s'&&p[3]=='p')value=' ';
    else if(p[0]=='#'){
        u32 pos=1,base=10;
        if(pos<length&&(p[pos]=='x'||p[pos]=='X')){pos++;base=16;}
        if(pos>=length)return false;
        for(;pos<length;pos++){
            char c=lower(p[pos]);u32 d;
            if(c>='0'&&c<='9')d=(u32)(c-'0');
            else if(base==16&&c>='a'&&c<='f')d=(u32)(c-'a'+10);
            else return false;
            if(d>=base || value>0x10FFFFu/base)return false;
            value=value*base+d;
        }
        if(!value||value>0x10FFFFu||(value>=0xD800u&&value<=0xDFFFu))return false;
    }else return false;
    *consumed=length+1;
    if(value<128){out[0]=(char)value;*n=1;}
    else if(value<2048){
        out[0]=(char)(0xC0u|(value>>6));
        out[1]=(char)(0x80u|(value&63));*n=2;
    }else if(value<65536){
        out[0]=(char)(0xE0u|(value>>12));
        out[1]=(char)(0x80u|((value>>6)&63));
        out[2]=(char)(0x80u|(value&63));*n=3;
    }else{
        out[0]=(char)(0xF0u|(value>>18));
        out[1]=(char)(0x80u|((value>>12)&63));
        out[2]=(char)(0x80u|((value>>6)&63));
        out[3]=(char)(0x80u|(value&63));*n=4;
    }
    return true;
}
static bool blocked(const char *name){
    return name_eq(name,"script")||name_eq(name,"style")||
           name_eq(name,"noscript")||name_eq(name,"template")||
           name_eq(name,"svg")||name_eq(name,"iframe");
}
static bool block(const char *name){
    return name_eq(name,"div")||name_eq(name,"p")||name_eq(name,"h1")||
           name_eq(name,"h2")||name_eq(name,"h3")||
           name_eq(name,"article")||name_eq(name,"section")||
           name_eq(name,"header")||name_eq(name,"footer")||
           name_eq(name,"li")||name_eq(name,"ul")||name_eq(name,"ol")||
           name_eq(name,"blockquote")||name_eq(name,"main")||
           name_eq(name,"title")||name_eq(name,"tr");
}
i32 falco_html_extract(const char*html,char*out,u32 cap){
    if(!html||!out||cap<2)return 0;
    u32 used=0;out[0]=0;
    char skip[16]={0};
    for(u32 i=0;html[i]&&used+5<cap;){
        char c=html[i];
        if(c=='<'){
            if(html[i+1]=='!'&&html[i+2]=='-'&&html[i+3]=='-'){
                i+=4;
                while(html[i]&&!(html[i]=='-'&&html[i+1]=='-'&&html[i+2]=='>'))i++;
                if(html[i])i+=3;
                continue;
            }
            u32 at=i+1;bool closing=false;
            if(html[at]=='/'){closing=true;at++;}
            while(html[at]==' '||html[at]=='\t')at++;
            char name[16];u32 n=0;
            while(letter(html[at])&&n<15)name[n++]=lower(html[at++]);
            name[n]=0;
            char quote=0;
            while(html[at]&&!(html[at]=='>'&&!quote)){
                if(quote){
                    if(html[at]==quote)quote=0;
                }else if(html[at]=='"'||html[at]==39)quote=html[at];
                at++;
            }
            if(html[at]=='>')at++;
            if(!n){i=at>i?at:i+1;continue;}
            i=at;
            if(skip[0]){
                if(closing&&name_eq(name,skip))skip[0]=0;
                continue;
            }
            if(!closing&&blocked(name)){u32 k=0;
                while(name[k]&&k<15){skip[k]=name[k];k++;}
                skip[k]=0;continue;
            }
            if(name_eq(name,"br"))break_line(out,cap,&used,false);
            if(block(name))break_line(out,cap,&used,
                name_eq(name,"p")||name_eq(name,"h1")||name_eq(name,"h2"));
            if(!closing&&name_eq(name,"li")){
                append(out,cap,&used,'*');append(out,cap,&used,' ');
            }
            continue;
        }
        if(skip[0]){i++;continue;}
        if(c=='&'){
            char encoded[4];u32 size=0,consumed=0;
            if(entity(html+i+1,&consumed,encoded,&size)){
                for(u32 k=0;k<size&&used+1<cap;k++)
                    append(out,cap,&used,encoded[k]);
                i+=consumed+1;continue;
            }
        }
        if(c=='\r'||c=='\n'||c=='\t'||c==' '){
            if(used&&out[used-1]!=' '&&out[used-1]!='\n')
                append(out,cap,&used,' ');
        }else if((unsigned char)c>=32u){
            append(out,cap,&used,c);
        }
        i++;
    }
    while(used&&(out[used-1]==' '||out[used-1]=='\n'))used--;
    out[used]=0;
    return (i32)used;
}
