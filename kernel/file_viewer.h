/* FalconOS: bounded, non-executable file association + media decoding.
 * All decoders operate on already-loaded, length-checked SHFS bytes. */
#ifndef FALCON_FILE_VIEWER_H
#define FALCON_FILE_VIEWER_H
#include "falcon.h"
typedef enum {
    FV_UNKNOWN,FV_TEXT,FV_BITMAP,FV_PACKAGE,FV_PPM,FV_WAVE
} fv_kind_t;
typedef struct {u32 width,height,offset,stride;} fv_bitmap_t;
typedef struct {
    u32 data_offset,data_size,sample_rate,frames;
    u16 channels,bits_per_sample;
} fv_wave_t;
fv_kind_t fv_kind(const char *path);
bool fv_bmp_open(const void *bytes,u32 size,fv_bitmap_t *bmp);
u32 fv_bmp_pixel(const void *bytes,const fv_bitmap_t *bmp,u32 x,u32 y);
bool fv_ppm_open(const void *bytes,u32 size,fv_bitmap_t *ppm);
u32 fv_ppm_pixel(const void *bytes,const fv_bitmap_t *ppm,u32 x,u32 y);
bool fv_wave_open(const void *bytes,u32 size,fv_wave_t *wav);
#endif
