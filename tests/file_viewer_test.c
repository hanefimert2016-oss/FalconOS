#include "file_viewer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
    assert(fv_kind("/home/falcon/notes.MD")==FV_TEXT);
    assert(fv_kind("/home/falcon/app.app.pkg")==FV_PACKAGE);
    assert(fv_kind("/home/falcon/logo.bmp")==FV_BITMAP);
    assert(fv_kind("document.Css")==FV_TEXT);
    assert(fv_kind("script.fpy")==FV_TEXT);
    assert(fv_kind("unknown.mp4")==FV_UNKNOWN);
    u8 data[70]={0};fv_bitmap_t bmp={0};
    data[0]='B';data[1]='M';data[10]=54;data[14]=40;
    data[18]=2;data[22]=2;data[26]=1;data[28]=24;
    /* Bottom row two pixels BGR: blue, green; top row red, white. */
    data[54]=255;data[58]=255;
    data[62]=0;data[63]=0;data[64]=255;
    data[65]=255;data[66]=255;data[67]=255;
    assert(fv_bmp_open(data,sizeof data,&bmp));
    assert(bmp.width==2&&bmp.height==2&&bmp.stride==8);
    assert(fv_bmp_pixel(data,&bmp,0,0)==0xFF0000);
    assert(fv_bmp_pixel(data,&bmp,1,0)==0xFFFFFF);
    assert(fv_bmp_pixel(data,&bmp,0,1)==0x0000FF);
    assert(fv_bmp_pixel(data,&bmp,1,1)==0x00FF00);
    data[28]=32;assert(!fv_bmp_open(data,sizeof data,&bmp));
    data[28]=24;data[22]=255;assert(!fv_bmp_open(data,sizeof data,&bmp));
    assert(!fv_bmp_open(data,20,&bmp));
    /* PPM/6 must decode actual 24-bit raster, not treat newline pixels
       as header whitespace. Reject truncated images. */
    assert(fv_kind("/home/falcon/image.PPM")==FV_PPM);
    assert(fv_kind("recording.WaV")==FV_WAVE);
    assert(fv_kind("source.java")==FV_TEXT);
    const u8 ppm[]={'P','6','\\n','2',' ','1','\\n',
                    '2','5','5','\\n',10,20,30,255,0,128};
    fv_bitmap_t pm={0};
    assert(fv_ppm_open(ppm,sizeof ppm,&pm));
    assert(pm.width==2&&pm.height==1);
    assert(fv_ppm_pixel(ppm,&pm,0,0)==0x0A141E);
    assert(fv_ppm_pixel(ppm,&pm,1,0)==0xFF0080);
    assert(!fv_ppm_open(ppm,sizeof ppm-1u,&pm));
    const u8 malformed_ppm[]="P6\\n2 1\\n65535\\n000000";
    assert(!fv_ppm_open(malformed_ppm,sizeof malformed_ppm-1u,&pm));

    /* Real RIFF parser: 8-bit mono 8 kHz PCM with four samples.
       Explicitly require fmt/data chunks and trustworthy lengths. */
    u8 wav[48]={0};fv_wave_t wave={0};
    memcpy(wav,"RIFF",4); wav[4]=40;memcpy(wav+8,"WAVE",4);
    memcpy(wav+12,"fmt ",4);wav[16]=16;wav[20]=1;wav[22]=1;
    wav[24]=0x40;wav[25]=0x1f; /* sample rate 8000 */
    wav[28]=0x40;wav[29]=0x1f; /* byte rate */
    wav[32]=1;wav[34]=8;
    memcpy(wav+36,"data",4);wav[40]=4;
    wav[44]=127;wav[45]=0;wav[46]=255;wav[47]=127;
    assert(fv_wave_open(wav,sizeof wav,&wave));
    assert(wave.sample_rate==8000u&&wave.channels==1u);
    assert(wave.frames==4u&&wave.bits_per_sample==8u);
    assert(!fv_wave_open(wav,46,&wave));
    wav[20]=3;assert(!fv_wave_open(wav,sizeof wav,&wave));
    wav[20]=1;wav[40]=255;assert(!fv_wave_open(wav,sizeof wav,&wave));
    puts("PASS: native BMP/PPM image viewer, WAV PCM metadata, safe association");

    return 0;
}
