/* Unit-test production FAPP/1 builder, without libc linked into FalconOS. */
#include "falcon.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
i32 k_strlen(const char *p){return (i32)strlen(p);}
i32 k_strncmp(const char*a,const char*b,i32 n){return strncmp(a,b,(size_t)n);}
void k_memset(void*d,u8 v,u32 n){memset(d,v,n);}
bool market_line_allowed(const char*s,i32 n){
    if(n>=5&&strncmp(s,"echo ",5)==0) return true;
    if(n==5&&strncmp(s,"uname",5)==0) return true;
    if(n==5&&strncmp(s,"clear",5)==0) return true;
    return false;
}
static void must(int cond,int code){
    if(!cond){fprintf(stderr,"CodeDium package error %d\n",code);exit(code);}
}
static char package[4097];
static u32 result;
int main(void){
    const char *src="# app-id: our-first-app\n# app-name: Our First App\n"
                    "# app-version: 1.10.0\n"
                    "# app-summary: Published from FalconOS\n"
                    "clear\necho It works\nuname\n";
    must(codedium_build_pkg(src,(u32)strlen(src),package,sizeof package,&result),1);
    must(strstr(package,"FAPP/1\nid=our-first-app\nname=Our First App\n"
                        "version=1.10.0\nsummary=Published from FalconOS\n\n")!=NULL,2);
    must(result==strlen(package),3);
    must(!codedium_build_pkg("# app-id: aa\nuname\n",20,package,sizeof package,&result),4);
    const char *unsafe="# app-id: our-first-app\n# app-name: Our First App\n"
                    "# app-version: 1.0.0\n# app-summary: Sample test package\n"
                    "echo foo; reboot\n";
    must(!codedium_build_pkg(unsafe,(u32)strlen(unsafe),package,sizeof package,&result),5);
    const char *badver="# app-id: our-first-app\n# app-name: Our First App\n"
                    "# app-version: 01.0.0\n# app-summary: Sample test package\n"
                    "echo hi\n";
    must(!codedium_build_pkg(badver,(u32)strlen(badver),package,sizeof package,&result),6);
    puts("PASS CodeDium native metadata and FAPP/1 app.pkg export");
    return 0;
}
