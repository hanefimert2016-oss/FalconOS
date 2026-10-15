#include "falco_navigation.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
    fnav_history_t h={0};char url[FNAV_URL];
    assert(fnav_record(&h,"falco.demo"));
    assert(fnav_record(&h,"https://example.com/a"));
    assert(fnav_record(&h,"https://example.com/b"));
    assert(h.count==3&&h.cursor==2);
    assert(strcmp(fnav_step(&h,-1),"https://example.com/a")==0);
    assert(strcmp(fnav_step(&h,-1),"falco.demo")==0);
    assert(!fnav_step(&h,-1));
    assert(strcmp(fnav_step(&h,1),"https://example.com/a")==0);
    assert(fnav_record(&h,"https://falconos.tech/"));
    assert(h.count==3&&!fnav_step(&h,1));
    assert(fnav_resolve("https://example.com/a/b","/new",url,sizeof url));
    assert(strcmp(url,"https://example.com/new")==0);
    assert(fnav_resolve("https://example.com/a/b","next",url,sizeof url));
    assert(strcmp(url,"https://example.com/a/next")==0);
    assert(!fnav_resolve("https://example.com/a/b","javascript:alert(1)",url,sizeof url));
    assert(!fnav_resolve("https://example.com/a/b","http://bad.example",url,sizeof url));
    assert(!fnav_resolve("https://example.com/a/b","/../private",url,sizeof url));
    assert(!fnav_resolve("https://example.com/a/b","//other.example",url,sizeof url));
    assert(!fnav_resolve("falco.demo","/page",url,sizeof url));
    for(int i=0;i<12;i++){
        char path[64];sprintf(path,"https://example.com/%d",i);
        assert(fnav_record(&h,path));
    }
    assert(h.count==FNAV_COUNT&&h.cursor==FNAV_COUNT-1);
    assert(!fnav_step(&h,1));
    puts("PASS Falcon Web bounded history, forward truncation, strict HTTPS links");
    return 0;
}
