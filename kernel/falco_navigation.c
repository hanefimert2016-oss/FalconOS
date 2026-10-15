#include "falco_navigation.h"
/* No heap, network calls, libc or active content. */
static bool prefix(const char*a,const char*b){
    while(*b)if(*a++!=*b++)return false;
    return true;
}
static u32 len(const char*s,u32 limit){
    u32 n=0;if(!s)return limit;
    while(n<limit&&s[n])n++;
    return n;
}
static bool clean_url(const char*s){
    if(!s||!*s)return false;
    for(u32 i=0;s[i]&&i<FNAV_URL;i++){
        unsigned char c=(unsigned char)s[i];
        if(c<33||c>126||c=='<'||c=='>'||c=='"'||
           c==39||c=='\\')return false;
    }
    return len(s,FNAV_URL)<FNAV_URL;
}
static bool copy(char*out,u32 cap,const char*s){
    u32 n=len(s,FNAV_URL);
    if(!out||!cap||n>=cap||n>=FNAV_URL)return false;
    for(u32 i=0;i<=n;i++)out[i]=s[i];
    return true;
}
static bool same(const char*a,const char*b){
    while(*a&&*b&&*a==*b){a++;b++;}
    return !*a&&!*b;
}
bool fnav_record(fnav_history_t*h,const char*url){
    if(!h||!clean_url(url)||(!prefix(url,"https://")&&!same(url,"falco.demo")))
        return false;
    if(h->count>FNAV_COUNT||h->cursor>=FNAV_COUNT)return false;
    if(h->count&&same(h->urls[h->cursor],url))return true;
    if(h->count&&h->cursor+1<h->count)h->count=h->cursor+1;
    if(h->count==FNAV_COUNT){
        for(u32 i=1;i<FNAV_COUNT;i++)
            for(u32 j=0;j<FNAV_URL;j++)h->urls[i-1][j]=h->urls[i][j];
        h->count--;h->cursor=h->count?h->count-1:0;
    }
    if(!copy(h->urls[h->count],FNAV_URL,url))return false;
    h->cursor=h->count++;
    return true;
}
const char*fnav_step(fnav_history_t*h,i32 delta){
    if(!h||!h->count||h->cursor>=h->count)return 0;
    if(delta==-1&&h->cursor)h->cursor--;
    else if(delta==1&&h->cursor+1<h->count)h->cursor++;
    else return 0;
    return h->urls[h->cursor];
}
/* Supports only absolute https and same-origin absolute/relative paths.
 * No credentialed URLs, ports, fragments, schemes or path traversal.
 * This is fail-closed hyperlink resolution, not a general URL parser. */
bool fnav_resolve(const char*current,const char*href,char*out,u32 cap){
    if(!out||!cap)return false;
    out[0]=0;
    if(!clean_url(href)||len(href,FNAV_URL)>=FNAV_URL)return false;
    if(prefix(href,"https://")){
        /* browser_load validates hostname and transport separately */
        if(!href[8]||!copy(out,cap,href))return false;
        return true;
    }
    if(!prefix(current,"https://")||!clean_url(current)||
       !href[0]||href[0]=='#'||href[0]=='?'||
       href[0]==':'||href[0]=='/'&&href[1]=='/')return false;
    /* Reject any parent-navigation or colon (custom scheme) in relatives. */
    for(u32 i=0;href[i];i++){
        if(href[i]==':')return false;
        if(href[i]=='.'&&href[i+1]=='.')return false;
    }
    u32 host=8;
    while(current[host]&&current[host]!='/'&&current[host]!='?'&&
          current[host]!='#')host++;
    if(host==8||host>=FNAV_URL)return false;
    for(u32 j=8;j<host;j++){
        char c=current[j];
        if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||
             (c>='0'&&c<='9')||c=='-'||c=='.'))return false;
    }
    u32 basis=host;
    if(href[0]=='/'){
        /* New absolute path on the same origin. */
    }else{
        basis=host+1;
        if(current[host]=='/'){
            u32 last=host;
            for(u32 i=host;current[i]&&current[i]!='?'&&current[i]!='#';i++)
                if(current[i]=='/')last=i;
            basis=last+1;
        }
    }
    u32 nh=len(href,FNAV_URL);
    if(basis+nh+1>cap||basis+nh+1>FNAV_URL)return false;
    for(u32 i=0;i<basis;i++){
        if(i<host)out[i]=current[i];
        else if(current[i])out[i]=current[i];
        else out[i]='/';
    }
    for(u32 i=0;i<nh;i++)out[basis+i]=href[i];
    out[basis+nh]=0;
    return true;
}
