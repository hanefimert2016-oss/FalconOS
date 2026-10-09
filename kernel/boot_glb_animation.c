/* FalconOS animated boot from the user's real glTF 2.0 geometry.
 * Frames are baked during preview build, without relying on a GPU driver.
 * Display-only asset: no binary model parsing or disk writes in Ring0.
 */
#include "falcon.h"
#ifdef FALCON_BOOT_GLB
#include "boot_model_frames.inc"
void boot_glb_splash(void){
    const u32 duration_ticks=300u; /* play source GLB 6-second track at 2x speed */
    u32 started=g_ticks;
    i32 frame_previous=-1;
    while(g_ticks-started<duration_ticks){
        u32 elapsed=g_ticks-started;
        u32 frame=(elapsed*BOOT_MODEL_FRAMES)/duration_ticks;
        if(frame>=BOOT_MODEL_FRAMES)frame=BOOT_MODEL_FRAMES-1u;
        if((i32)frame==frame_previous){
            __asm__ volatile("hlt");
            continue;
        }
        frame_previous=(i32)frame;
        gfx_gradient_v(0x060A17u,0x101D37u);
        i32 scale=(FB.width>=1000 && FB.height>=760)?2:1;
        i32 draw_w=(i32)BOOT_MODEL_W*scale;
        i32 draw_h=(i32)BOOT_MODEL_H*scale;
        i32 x0=((i32)FB.width-draw_w)/2;
        i32 y0=((i32)FB.height-draw_h)/2-28;
        if(y0<0)y0=0;
        /* Decode verified build-generated RLE: [u8 run, u8 palette index].
         * Bounds checks guard both the source stream and the framebuffer. */
        u32 start=boot_model_offsets[frame];
        u32 end=boot_model_offsets[frame+1u];
        u32 pixel=0;
        for(u32 pos=start;pos+1u<end;pos+=2u){
            u32 run=boot_model_rle[pos];
            u32 color_index=boot_model_rle[pos+1u];
            if(!run || color_index>=192u || pixel+run>BOOT_MODEL_W*BOOT_MODEL_H)
                break;
            if(color_index){
                u32 rgb=boot_model_palette[color_index];
                for(u32 i=0;i<run;i++){
                    u32 index=pixel+i;
                    i32 x=x0+(i32)(index%BOOT_MODEL_W)*scale;
                    i32 y=y0+(i32)(index/BOOT_MODEL_W)*scale;
                    if(scale==2){
                        gfx_pixel(x,y,rgb);gfx_pixel(x+1,y,rgb);
                        gfx_pixel(x,y+1,rgb);gfx_pixel(x+1,y+1,rgb);
                    }else gfx_pixel(x,y,rgb);
                }
            }
            pixel+=run;
        }
        i32 center=(i32)FB.width/2;
        i32 bar_y=y0+draw_h+24;
        gfx_text_centered(center,bar_y,
            "FALCONOS  /  BLUE DRAGON EDITION",0xC7DCFFu);
        gfx_text_centered(center,bar_y+26,
            "Preparing your desktop",0x7193C3u);
        i32 w=(i32)FB.width/3;
        if(w>360)w=360;
        i32 bx=center-w/2,by=bar_y+52;
        gfx_rect(bx,by,w,3,0x223453u);
        gfx_rect(bx,by,(i32)(elapsed*(u32)w/duration_ticks),3,0x4D9BFFu);
        gfx_present();
    }
}
#endif
