/* FalconOS executable package validator (FAPP/1).
 * A .app.pkg is this OS's application file, NOT a Windows PE/.exe, and
 * presently contains strictly allowlisted FAPP/1 shell program commands.
 * Never run an arbitrary downloaded binary or import host executables.
 * Validate the WHOLE file before executing any line. */
#include "falcon.h"
#include "shfs.h"
static bool safe_id_(const char *s,u32 n){
    if(n<2||n>32||s[0]<'a'||s[0]>'z')return false;
    for(u32 i=1;i<n;i++){
        char ch=s[i];
        if(!((ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch=='-'))return false;
    }
    return true;
}
static bool printable_(const char *s,u32 n){
    if(n<2||n>75)return false;
    for(u32 i=0;i<n;i++)if((u8)s[i]<32||(u8)s[i]>126||s[i]=='|')
        return false;
    return true;
}
static bool version_(const char *s,u32 n){
    if(n<5||n>24)return false;
    u32 dots=0;
    for(u32 i=0;i<n;i++){
        if(s[i]=='.'){dots++;continue;}
        if(!((s[i]>='0'&&s[i]<='9')||s[i]=='-'||
             (s[i]>='a'&&s[i]<='z')))return false;
    }
    return dots>=2;
}
static bool field_(const char **at,const char *key,u32 max,
                   char *out,u32 outcap){
    const char *p=*at;
    u32 pref=k_strlen(key);
    if(k_strncmp(p,key,pref)!=0)return false;
    p+=pref;
    u32 n=0;
    while(*p && *p!='\n'&&n<=max){
        if(n+1>=outcap)return false;
        out[n++]=*p++;
    }
    if(!n||n>max||*p!='\n')return false;
    out[n]=0;*at=p+1;return true;
}
bool app_pkg_validate(const char *path,char out_name[76],const char **script){
    if(!path||!out_name||!script)return false;
    *script=NULL;out_name[0]=0;
    u32 plen=k_strlen(path);
    if(plen<9 || k_strcmp(path+plen-8,".app.pkg")!=0)return false;
    shfs_ent_t *e=shfs_lookup(path);
    if(!e||e->is_dir||e->len<70||e->len>4096||e->len>=SHFS_FBYTES ||
       e->data[e->len]!=0)return false;
    for(u32 i=0;i<e->len;i++){
        u8 c=(u8)e->data[i];
        if(c==0||c=='\r'||c>127||(c<32&&c!='\n'))return false;
    }
    const char *p=e->data;
    if(k_strncmp(p,"FAPP/1\nid=",10)!=0)return false;
    p+=7; /* points to id= */
    char id[33],name[76],ver[25],summary[76];
    if(!field_(&p,"id=",32,id,sizeof id) ||
       !field_(&p,"name=",75,name,sizeof name) ||
       !field_(&p,"version=",24,ver,sizeof ver) ||
       !field_(&p,"summary=",75,summary,sizeof summary))return false;
    if(!safe_id_(id,k_strlen(id)) ||
       !printable_(name,k_strlen(name)) ||
       !printable_(summary,k_strlen(summary)) ||
       !version_(ver,k_strlen(ver)) || *p!='\n')return false;
    p++;
    const char *body=p;
    bool command=false;
    while(*p){
        const char *line=p;
        while(*p&&*p!='\n'){
            if(p-line>180)return false;
            p++;
        }
        if(*p!='\n')return false;
        u32 n=(u32)(p-line);
        if(n&&line[0]!='#'){
            if(!market_line_allowed(line,(i32)n))return false;
            command=true;
        }
        p++;
    }
    if(!command)return false;
    k_strcpy(out_name,name);
    *script=body;
    return true;
}
