#include "falcon.h"
#include <stdio.h>
#include <stdlib.h>
struct case_entry {const char *a; const char *b; i32 expected;};
static struct case_entry cases[] = {
  {"1.10.0","1.9.99",1}, {"1.0.0","1.0.0",0},
  {"2.0.0","1.99.99",1}, {"1.0.0","1.0.1",-1},
  {"1.0.0","1.0.0-rc.1",1}, {"1.0.0-beta.2","1.0.0-beta.11",-1},
  {"1.0.0-alpha","1.0.0-beta",-1},
  {"1.0.0-1","1.0.0-alpha",-1},
  {"1.0.0-rc.2","1.0.0-rc.1",1},
  {"1.0.0-rc","1.0.0-rc.1",-1},
  {"bad","1.0.0",-1}, {"1.0.0","bad",1},
  {"01.0.0","1.0.0",-1}, {"1.0.0-rc.","1.0.0-rc.1",-1}
};
int main(void) {
  u32 total=sizeof cases/sizeof cases[0];
  for(u32 i=0;i<total;i++) {
    i32 actual=market_version_compare(cases[i].a,cases[i].b);
    if (actual!=cases[i].expected) {
      fprintf(stderr, "SemVer #%u failed: %s vs %s = %d expected %d\n",
              i,cases[i].a,cases[i].b,actual,cases[i].expected);
      return 1;
    }
  }
  printf("PASS %u SemVer cases\n",total);
  return 0;
}
