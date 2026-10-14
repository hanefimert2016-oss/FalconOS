#include "falco_miniweb.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
void k_memset(void *d,u8 v,u32 n){memset(d,v,n);}
i32 k_strlen(const char*s){return (i32)strlen(s);}
static fmw_node_t*node(fmw_page_t*p,const char*id){
    for(u32 i=0;i<p->count;i++)if(strcmp(p->nodes[i].id,id)==0)return &p->nodes[i];
    return NULL;
}
int main(void){
    static fmw_page_t page;
    const char *html="<html><head><title>MiniWeb test</title>"
       "<style>.hero {color:#1589c5;font-size:30px;font-weight:bold}"
       "#status {background-color:#ecf0f5;text-align:center}</style>"
       "</head><body style='background-color:#f0f6fc'>"
       "<h1 class='hero'>Hello FalconOS</h1>"
       "<p id='status'>Waiting</p>"
       "<button onclick=\"document.getElementById('status').textContent='Clicked'\">Press</button>"
       "<script>document.getElementById('status').textContent='Loaded';"
       "eval('evil'); fetch('/evil'); </script></body></html>";
    assert(falco_miniweb_parse(&page,html));
    assert(page.count==3 && page.buttons==1);
    assert(strcmp(page.title,"MiniWeb test")==0);
    assert(page.page_bg==0xF0F6FC);
    assert(page.nodes[0].color==0x1589c5);
    assert(page.nodes[0].font_px==30 && page.nodes[0].bold);
    assert(node(&page,"status")->bg==0xECF0F5);
    assert(node(&page,"status")->align==1);
    assert(strcmp(node(&page,"status")->text,"Loaded")==0);
    assert(page.js_mutations==1);
    assert(falco_miniweb_action(&page,2));
    assert(strcmp(node(&page,"status")->text,"Clicked")==0);
    assert(!falco_miniweb_action(&page,1));
    const char *x="<p id='a'>safe</p><script>eval('bad');"
                  "document.cookie='secret';</script>";
    assert(falco_miniweb_parse(&page,x));
    assert(strcmp(node(&page,"a")->text,"safe")==0);
    assert(page.js_mutations==0);
    assert(!falco_miniweb_parse(&page,"<script>alert(1)</script>"));
    puts("PASS: MiniWeb/3 CSS id/class + safe JS literal DOM + native buttons");
    return 0;
}
