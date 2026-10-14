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
    puts("PASS FalconOS native file associations and bounded BMP decoder");
    return 0;
}
