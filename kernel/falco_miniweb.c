#include "falco_miniweb.h"
/* No heap, libc, network, embedded interpreter, eval or executable code. */
static char low(char c){return c>='A'&&c<='Z'?(char)(c+32):c;}
static bool alpha(char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z');}
static bool space(char c){return c==' '||c=='\t'||c=='\n'||c=='\r';}
static bool eq(const char*a,const char*b){
    while(*a&&*b){if(low(*a++)!=low(*b++))return false;}
    return !*a&&!*b;
}
static bool starts(const char*a,const char*b){
    while(*b){if(!*a || low(*a++)!=low(*b++))return false;}
    return true;
}
static void strcopy(char*d,u32 cap,const char*s){
    u32 j=0;while(s[j]&&j+1<cap){d[j]=s[j];j++;}d[j]=0;
}
static void trim(char*s){
    u32 n=0;while(space(s[n]))n++;
    if(n){u32 i=0;while(s[n])s[i++]=s[n++];s[i]=0;}
    u32 l=0;while(s[l])l++;
    while(l&&space(s[l-1]))s[--l]=0;
}
static void slice(char*d,u32 cap,const char*b,const char*e){
    u32 n=0;while(b<e&&n+1<cap)d[n++]=*b++;d[n]=0;trim(d);
}
static bool contains_class(const char*names,const char*klass){
    u32 n=0;const char *p=names;
    while(*p){while(space(*p))p++;const char *b=p;
        while(*p&&!space(*p))p++;
        if(p>b){char token[32];slice(token,sizeof token,b,p);if(eq(token,klass))return true;}
        if(++n>12)break;
    }return false;
}
static const char*find_ci(const char*p,const char*what){
    for(;*p;p++)if(starts(p,what))return p;
    return 0;
}
static u32 hex(char c){
    if(c>='0'&&c<='9')return (u32)(c-'0');
    c=low(c);return c>='a'&&c<='f'?(u32)(c-'a'+10):99;
}
static u32 color(const char*v,u32 previous){
    if(*v=='#'){
        v++;u32 n=0,value=0;
        while(n<6&&hex(v[n])<16){value=(value<<4)|hex(v[n]);n++;}
        if(n==3&&!v[n])return (((value>>8)&15)*17<<16)|(((value>>4)&15)*17<<8)|((value&15)*17);
        if(n==6&&!v[n])return value;
    }
    if(eq(v,"red"))return 0xCF304Bu;
    if(eq(v,"blue"))return 0x285ACC;
    if(eq(v,"green"))return 0x148E68;
    if(eq(v,"white"))return 0xFFFFFFu;
    if(eq(v,"black"))return 0x111111u;
    if(eq(v,"navy"))return 0x131E3Du;
    if(eq(v,"gray")||eq(v,"grey"))return 0x808080u;
    if(eq(v,"orange"))return 0xD98222u;
    /* Basic CSS rgb() functional notation. Never permit wraparound. */
    if(starts(v,"rgb(")){
        v+=4;u32 channels[3]={0,0,0};
        for(u32 i=0;i<3;i++){
            while(space(*v))v++;
            if(*v<'0'||*v>'9')return previous;
            u32 n=0,digits=0;
            while(*v>='0'&&*v<='9'&&digits++<3)n=n*10u+(u32)(*v++-'0');
            if(n>255 || (*v>='0'&&*v<='9'))return previous;
            channels[i]=n;
            while(space(*v))v++;
            if(*v++!=(i==2?')':','))return previous;
        }
        if(*v) return previous;
        return (channels[0]<<16)|(channels[1]<<8)|channels[2];
    }
    if(eq(v,"transparent"))return 0xFFFFFFFFu;
    return previous;
}
static void declarations(fmw_node_t*n,const char*data,u32 len){
    const char*p=data,*end=data+len;
    for(u32 guard=0;guard<32&&p<end;guard++){
        const char*b=p;while(p<end&&*p!=':'&&*p!=';')p++;
        if(p>=end||*p!=':'){while(p<end&&*p!=';')p++;if(p<end)p++;continue;}
        char key[32],value[42];slice(key,sizeof key,b,p++);b=p;
        while(p<end&&*p!=';')p++;
        slice(value,sizeof value,b,p);if(p<end)p++;
        if(eq(key,"color"))n->color=color(value,n->color);
        else if(eq(key,"background")||eq(key,"background-color"))n->bg=color(value,n->bg);
        else if(eq(key,"font-size")){
            u32 px=0;
            for(u32 i=0;value[i]>='0'&&value[i]<='9'&&i<3;i++)px=px*10u+(u32)(value[i]-'0');
            if(px>=10&&px<=72)n->font_px=(u16)px;
        }else if(eq(key,"text-align"))n->align=eq(value,"center")?1:(eq(value,"right")?2:0);
        else if(eq(key,"font-weight"))n->bold=eq(value,"bold")||eq(value,"700");
        else if(eq(key,"display")&&eq(value,"none"))n->hidden=1;
    }
}
typedef struct{char selector[36];char decl[240];} rule_t;
static rule_t rules[FMW_STYLES];
static u32 rule_count;
static void parse_css(fmw_page_t*page,const char*begin,const char*end){
    const char*p=begin;
    while(p<end&&rule_count<FMW_STYLES){
        const char*b=p;
        while(p<end&&*p!='{'&&*p!='}')p++;
        if(p>=end)break;
        if(*p=='}'){p++;continue;}
        rule_t*r=&rules[rule_count];slice(r->selector,sizeof r->selector,b,p++);
        b=p;while(p<end&&*p!='}')p++;
        slice(r->decl,sizeof r->decl,b,p);
        if(p<end)p++;
        if(r->selector[0]&&r->decl[0])rule_count++;
    }
    page->css_rules=rule_count;
}
/* Bounded simple CSS selector support: tag, #id, .class, tag.class,
 * #id.class and comma-separated groups. No combinators or pseudo-classes.
 * Apply type -> class -> id to respect basic CSS specificity independently
 * from stylesheet ordering; inline styles remain the final override. */
static i32 one_selector(const fmw_node_t*n,const char*begin,const char*end){
    i32 level=0,parts=0;const char*p=begin;
    while(p<end){
        if(space(*p))return -1; /* descendant selectors unsupported */
        char kind=0;
        if(*p=='#'||*p=='.')kind=*p++;
        const char*b=p;
        while(p<end&&*p!='.'&&*p!='#')p++;
        if(b==p||++parts>8)return -1;
        char token[36];slice(token,sizeof token,b,p);
        if(kind=='#'){if(!eq(n->id,token))return -1;level=3;}
        else if(kind=='.'){if(!contains_class(n->klass,token))return -1;
            if(level<2)level=2;}
        else{if(!eq(n->tag,token))return -1;if(level<1)level=1;}
    }
    return level;
}
static i32 selector_level(const fmw_node_t*n,const char*s){
    i32 best=-1;const char*p=s,*b=s;
    for(;;){
        if(*p==','||!*p){
            const char*e=p;
            while(b<e&&space(*b))b++;
            while(e>b&&space(e[-1]))e--;
            if(e>b){i32 level=one_selector(n,b,e);if(level>best)best=level;}
            if(!*p)break;
            b=p+1;
        }
        p++;
    }
    return best;
}
static void apply_rules(fmw_node_t*n){
    for(i32 level=1;level<=3;level++)
        for(u32 i=0;i<rule_count;i++)
            if(selector_level(n,rules[i].selector)==level)
                declarations(n,rules[i].decl,(u32)k_strlen(rules[i].decl));
}
/* HTML attributes are quoted literals only. This ignores events other than
 * inline onclick, which is parsed but NEVER evaluated as arbitrary JS. */
static void attr(const char*b,const char*end,const char*key,char*out,u32 cap){
    out[0]=0;
    while(b<end){
        while(b<end&&(space(*b)||*b=='/'))b++;
        const char*s=b;
        while(b<end&&(alpha(*b)||*b=='-'||*b=='_'))b++;
        if(b==s){b++;continue;}
        char name[28];slice(name,sizeof name,s,b);
        while(b<end&&space(*b))b++;
        if(b>=end||*b!='=')continue;
        b++;while(b<end&&space(*b))b++;
        if(b>=end)break;
        char q=0;if(*b=='"'||*b==39)q=*b++;
        s=b;
        if(q){while(b<end&&*b!=q)b++;}
        else{while(b<end&&!space(*b)&&*b!='>')b++;}
        if(eq(name,key)){slice(out,cap,s,b);return;}
        if(b<end&&q&&*b==q)b++;
    }
}
static bool is_node(const char*t){
    return eq(t,"h1")||eq(t,"h2")||eq(t,"h3")||eq(t,"p")||eq(t,"div")||
           eq(t,"section")||eq(t,"article")||eq(t,"li")||eq(t,"button")||
           eq(t,"a")||eq(t,"blockquote")||eq(t,"label");
}
static void push_text(fmw_node_t*n,const char*p,const char*end){
    u32 used=(u32)k_strlen(n->text);
    for(;p<end&&used+1<sizeof n->text;p++){
        if(*p=='&'){
            if(end-p>=5&&starts(p,"&amp;")){n->text[used++]='&';p+=4;continue;}
            if(end-p>=4&&starts(p,"&lt;")){n->text[used++]='<';p+=3;continue;}
            if(end-p>=4&&starts(p,"&gt;")){n->text[used++]='>';p+=3;continue;}
        }
        if(space(*p)){
            if(used&&n->text[used-1]!=' ')n->text[used++]=' ';
        }else if((unsigned char)*p>=32)n->text[used++]=*p;
    }
    n->text[used]=0;
}
/* JS subset: exactly document.getElementById('id').textContent='literal'
 * or document.getElementById('id').style.color='color'.
 * No functions, expressions, timers, fetch, dynamic code or script src. */
static u32 mutate(fmw_page_t*page,const char*js,u32 length){
    if(!js||length>768)return 0;
    const char*prefix="document.getElementById(";
    const char*p=js,*end=js+length;
    u32 changed=0;
    while(p<end&&changed<16){
        const char*b=p;
        while(b<end&&!starts(b,prefix))b++;
        if(b>=end)break;
        p=b+k_strlen(prefix);
        if(p>=end||(*p!=39&&*p!='"')){p++;continue;}
        char quote=*p++;b=p;
        while(p<end&&*p!=quote&&p-b<32)p++;
        char id[32];slice(id,sizeof id,b,p);
        if(p>=end||*p!=quote){p++;continue;}
        p++;if(p>=end||*p++!=')')continue;
        bool text=false,fg=false,bg=false;
        if(end-p>=12&&starts(p,".textContent")){p+=12;text=true;}
        else if(end-p>=10&&starts(p,".innerText")){p+=10;text=true;}
        else if(end-p>=22&&starts(p,".style.backgroundColor")){p+=22;bg=true;}
        else if(end-p>=12&&starts(p,".style.color")){p+=12;fg=true;}
        else continue;
        while(p<end&&space(*p))p++;
        if(p>=end||*p++!='=')continue;
        while(p<end&&space(*p))p++;
        if(p>=end||(*p!=39&&*p!='"'))continue;
        quote=*p++;b=p;
        while(p<end&&*p!=quote&&p-b<224)p++;
        if(p>=end||*p!=quote){p++;continue;}
        char value[224];slice(value,sizeof value,b,p);p++;
        for(u32 i=0;i<page->count;i++)if(eq(page->nodes[i].id,id)){
            if(text)strcopy(page->nodes[i].text,sizeof page->nodes[i].text,value);
            if(fg)page->nodes[i].color=color(value,page->nodes[i].color);
            if(bg)page->nodes[i].bg=color(value,page->nodes[i].bg);
            changed++;break;
        }
    }
    return changed;
}
bool falco_miniweb_parse(fmw_page_t*page,const char*html){
    if(!page||!html)return false;
    k_memset(page,0,sizeof *page);page->page_bg=0xFFFFFFu;
    rule_count=0;
    const char*p=html;
    /* First pass CSS. Limit all scans to input already bounded by host
     * bridge (browser_result has 4096 bytes). Scripts are never executed. */
    for(u32 i=0;p[i]&&i<4094;i++)if(p[i]=='<'&&starts(p+i,"<style")){
        const char*open=p+i;while(*open&&*open!='>')open++;
        if(!*open)break;
        open++;
        const char*end=find_ci(open,"</style>");
        if(!end)break;
        parse_css(page,open,end);
        i=(u32)(end-p)+7u;
    }
    char embedded_js[768]={0};u32 jslen=0;u32 cur=FMW_NODES;
    for(u32 i=0;p[i]&&i<4094;){
        if(p[i]!='<'){
            const char*b=p+i;while(p[i]&&p[i]!='<'&&i<4094)i++;
            if(cur<page->count)push_text(&page->nodes[cur],b,p+i);
            continue;
        }
        if(starts(p+i,"<!--")){
            const char*end=find_ci(p+i+4,"-->");if(!end)break;i=(u32)(end-p)+3u;continue;
        }
        const char*tag=p+i+1;bool closing=false;
        if(*tag=='/'){closing=true;tag++;}
        const char*name=tag;while(alpha(*tag)||(*tag>='0'&&*tag<='9'))tag++;
        char node_tag[16];slice(node_tag,sizeof node_tag,name,tag);
        const char*gt=tag;char q=0;
        while(*gt&&gt-p<4095){
            if(q){if(*gt==q)q=0;}
            else if(*gt==39||*gt=='"')q=*gt;
            else if(*gt=='>')break;
            gt++;
        }
        if(*gt!='>')break;
        i=(u32)(gt-p)+1u;
        if(eq(node_tag,"style")||eq(node_tag,"script")||eq(node_tag,"title")){
            if(!closing){
                char close[22]="</";u32 n=2;
                for(u32 k=0;node_tag[k]&&n+2<sizeof close;k++)close[n++]=node_tag[k];
                close[n++]='>';close[n]=0;
                const char*end=find_ci(p+i,close);if(!end)break;
                if(eq(node_tag,"script")){
                    /* Separate inline blocks execute in document order.
                     * External script URLs are intentionally not fetched. */
                    char src[80];attr(tag,gt,"src",src,sizeof src);
                    u32 count=(u32)(end-(p+i));
                    if(!src[0]&&count&&count+jslen+2u<sizeof embedded_js){
                        if(jslen)embedded_js[jslen++]=';';
                        for(u32 k=0;k<count;k++)embedded_js[jslen++]=p[i+k];
                        embedded_js[jslen]=0;
                    }
                }
                if(eq(node_tag,"title"))slice(page->title,sizeof page->title,p+i,end);
                i=(u32)(end-p)+(u32)k_strlen(close);
            }
            continue;
        }
        if(closing){
            if(cur<page->count&&eq(page->nodes[cur].tag,node_tag))cur=FMW_NODES;
            continue;
        }
        if(eq(node_tag,"body")){
            char style[160];attr(tag,gt,"style",style,sizeof style);
            fmw_node_t body={0};body.bg=page->page_bg;
            strcopy(body.tag,sizeof body.tag,"body");
            apply_rules(&body);
            declarations(&body,style,(u32)k_strlen(style));
            page->page_bg=body.bg;continue;
        }
        if(!is_node(node_tag)||page->count>=FMW_NODES)continue;
        cur=page->count++;
        fmw_node_t*n=&page->nodes[cur];strcopy(n->tag,sizeof n->tag,node_tag);
        strcopy(n->id,sizeof n->id,"");
        n->color=0x273346u;n->bg=0xFFFFFFFFu;
        n->font_px=eq(node_tag,"h1")?34:eq(node_tag,"h2")?26:16;
        n->bold=eq(node_tag,"h1")||eq(node_tag,"h2")||eq(node_tag,"h3");
        n->button=eq(node_tag,"button");
        attr(tag,gt,"id",n->id,sizeof n->id);
        attr(tag,gt,"class",n->klass,sizeof n->klass);
        attr(tag,gt,"onclick",n->onclick,sizeof n->onclick);
        apply_rules(n);
        char inline_style[160];attr(tag,gt,"style",inline_style,sizeof inline_style);
        declarations(n,inline_style,(u32)k_strlen(inline_style));
        if(n->button)page->buttons++;
    }
    if(jslen)page->js_mutations=mutate(page,embedded_js,jslen);
    return page->count>0;
}
bool falco_miniweb_action(fmw_page_t*page,u32 index){
    if(!page||index>=page->count||!page->nodes[index].button)return false;
    return mutate(page,page->nodes[index].onclick,(u32)k_strlen(page->nodes[index].onclick))!=0;
}
