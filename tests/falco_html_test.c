#include "falco_html_text.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void){
    char buf[512];
    const char *html="<html><head><style>SECRET_STYLE</style></head>"
      "<body><h1>Falco &amp; FalconOS</h1><p>Turk&#231;e &lt;b&gt;"
      " &quot;secure&quot;</p><script>SECRET_JS('test');</script>"
      "<ul><li>First</li><li>Second &nbsp; item</li></ul>"
      "<!-- SECRET_COMMENT --><svg><text>SECRET_SVG</text></svg>"
      "<div title='1 > 0'>Last &#x2192; line</div></body></html>";
    int n=falco_html_extract(html,buf,sizeof buf);
    assert(n>0 && n<(int)sizeof buf);
    assert(strstr(buf,"Falco & FalconOS"));
    assert(strstr(buf,"Turk\xC3\xA7e <b> \"secure\""));
    assert(strstr(buf,"* First"));
    assert(strstr(buf,"* Second"));
    assert(strstr(buf,"Last \xE2\x86\x92 line"));
    assert(!strstr(buf,"SECRET_STYLE"));
    assert(!strstr(buf,"SECRET_JS"));
    assert(!strstr(buf,"SECRET_COMMENT"));
    assert(!strstr(buf,"SECRET_SVG"));
    char narrow[9];
    assert(falco_html_extract("<h1>1234567890123456789</h1>",narrow,9)>=0);
    assert(narrow[8]=='\0');
    assert(falco_html_extract(NULL,buf,sizeof buf)==0);
    assert(falco_html_extract(html,NULL,sizeof buf)==0);
    puts("PASS: Falco SemanticText/2 entity, paragraph, inert script and bounds tests");
    return 0;
}
