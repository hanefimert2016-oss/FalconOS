/* Host-native policy tests for experimental Ring3 task grants.
 * No mocks: this links the same policy object the kernel uses. */
#include "falcon.h"
#include <stdio.h>
extern bool ring3_caps_valid(u32 caps);
extern bool ring3_caps_authorize(u32 caps,u64 call);
#define CHECK(x) do{if(!(x)){fprintf(stderr,"R3CAP FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void){
    const u32 diag=1u,time=2u;
    CHECK(ring3_caps_valid(0u));
    CHECK(ring3_caps_valid(diag|time));
    CHECK(!ring3_caps_valid(4u));
    CHECK(!ring3_caps_valid(0xffffffffu));
    CHECK(!ring3_caps_authorize(0,1u)); /* SYS_diag */
    CHECK(!ring3_caps_authorize(0,4u)); /* SYS_ticks */
    CHECK(ring3_caps_authorize(diag,1u));
    CHECK(!ring3_caps_authorize(diag,4u));
    CHECK(!ring3_caps_authorize(time,1u));
    CHECK(ring3_caps_authorize(time,4u));
    for(u32 c=0;c<=3;c++){
        CHECK(ring3_caps_authorize(c,2u)); /* exit */
        CHECK(ring3_caps_authorize(c,3u)); /* getpid */
        CHECK(ring3_caps_authorize(c,5u)); /* yield */
        CHECK(ring3_caps_authorize(c,99u)); /* preserve ENOSYS */
    }
    /* Unknown grants cannot expand authority even if caller bypasses spawn. */
    CHECK(!ring3_caps_authorize(0xffffffffu,1u));
    CHECK(!ring3_caps_authorize(0xffffffffu,4u));
    puts("PASS Ring3 default-deny per-task diag/clock permissions");
    return 0;
}
