/* FalconOS Falcon Web: bounded history and HTTPS-only hyperlink resolution. */
#ifndef FALCO_NAVIGATION_H
#define FALCO_NAVIGATION_H
#include "falcon.h"
#define FNAV_COUNT 10u
#define FNAV_URL 224u
typedef struct {
    char urls[FNAV_COUNT][FNAV_URL];
    u32 count,cursor;
} fnav_history_t;
bool fnav_record(fnav_history_t*h,const char*url);
const char*fnav_step(fnav_history_t*h,i32 delta);
bool fnav_resolve(const char*current,const char*href,char*out,u32 capacity);
#endif
