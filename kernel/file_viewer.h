/* FalconOS native file association + validated BMP/24-bit decoding.
 * No heap, codecs, host execution or extension-based executable dispatch. */
#ifndef FALCON_FILE_VIEWER_H
#define FALCON_FILE_VIEWER_H
#include "falcon.h"
typedef enum {FV_UNKNOWN,FV_TEXT,FV_BITMAP,FV_PACKAGE} fv_kind_t;
typedef struct {u32 width,height,offset,stride;} fv_bitmap_t;
fv_kind_t fv_kind(const char *path);
bool fv_bmp_open(const void *bytes,u32 size,fv_bitmap_t *bmp);
u32 fv_bmp_pixel(const void *bytes,const fv_bitmap_t *bmp,u32 x,u32 y);
#endif
