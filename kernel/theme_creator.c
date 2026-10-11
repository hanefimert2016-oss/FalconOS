/* FalconOS Theme Creator — wholly native editable palettes and wallpaper.
 * Does not need browser, CSS engine, file dialog, or custom Linux runtime.
 * Presets are private/user data; file exports are inert FTHEME/1 manifests.
 */
#include "falcon.h"
#include "shfs.h"
static i32 channel=0,part=0;
static const char *state="Arrows: channel/color | P preset | Space apply | E export | I import";
static const u32 presets[][3]={
    {0x0B193Au,0x2B72A5u,0x72E1D0u},
    {0x16142Cu,0x6F42B4u,0xBD99FFu},
    {0x4A172Cu,0xE98263u,0xFFC190u},
    {0x061D20u,0x136D4Cu,0x80FFB1u},
    {0x19252Eu,0x8294AAu,0xE2E5E8u},
};
static i32 preset;
void theme_creator_load_defaults(void){
    SET.user_wallpaper_top=presets[0][0];
    SET.user_wallpaper_bottom=presets[0][1];
    SET.user_accent_color=presets[0][2];
    SET.user_theme_active=0;
}
static u32 *color_field(void){
    return part==0?&SET.user_wallpaper_top:
           (part==1?&SET.user_wallpaper_bottom:&SET.user_accent_color);
}
static void hex6(char *dest,u32 color){
    const char *hex="0123456789ABCDEF";
    dest[0]='#';
    for(i32 j=0;j<6;j++)dest[1+j]=hex[(color>>(20-4*j))&15u];
    dest[7]=0;
}
static u32 parse6(const char *text,bool *ok){
    if(!text||text[0]!='#'){*ok=false;return 0;}
    u32 n=0;
    for(i32 j=0;j<6;j++){
        char c=text[j+1];u32 v;
        if(c>='0'&&c<='9')v=(u32)(c-'0');
        else if(c>='a'&&c<='f')v=(u32)(c-'a'+10);
        else if(c>='A'&&c<='F')v=(u32)(c-'A'+10);
        else{*ok=false;return 0;}
        n=(n<<4)|v;
    }
    if(text[7] && text[7]!='\n'){*ok=false;return 0;}
    return n;
}
static bool save(void){
    bool d=diskdb_save();
    state=d?"Saved palette on dedicated FalconFS":"Applied in RAM; choose safe FalconFS disk to persist";
    return d;
}
static void write_ascii(char *dest,i32 *pos,const char *src){
    while(*src&&*pos<255)dest[(*pos)++]=*src++;
    dest[*pos]=0;
}
void theme_creator_export(void){
    char buffer[256];i32 pos=0;
    buffer[0]=0;
    char color[8];
    write_ascii(buffer,&pos,"FTHEME/1\nname=My FalconOS Theme\n");
    hex6(color,SET.user_wallpaper_top);write_ascii(buffer,&pos,"top=");
    write_ascii(buffer,&pos,color);write_ascii(buffer,&pos,"\n");
    hex6(color,SET.user_wallpaper_bottom);write_ascii(buffer,&pos,"bottom=");
    write_ascii(buffer,&pos,color);write_ascii(buffer,&pos,"\n");
    hex6(color,SET.user_accent_color);write_ascii(buffer,&pos,"accent=");
    write_ascii(buffer,&pos,color);write_ascii(buffer,&pos,"\n");
    shfs_ent_t *target=shfs_open_w_abs("/home/falcon/Desktop/my-theme.fth",false);
    if(!target){state="Desktop full. Could not export theme.";return;}
    k_memcpy(target->data,buffer,(u32)pos+1);target->len=(u32)pos;
    state="Saved Desktop/my-theme.fth";
#ifdef FALCON_QEMU_UI_GALLERY
    outb(0xE9,'t');outb(0xE9,'E');
#endif
}
static void import(void){
    shfs_ent_t *f=shfs_lookup("/home/falcon/Desktop/my-theme.fth");
    if(!f||f->is_dir||f->len>250 || k_strncmp(f->data,"FTHEME/1\n",9)!=0){
        state="No valid Desktop/my-theme.fth";return;
    }
    const char *keys[]={"top=","bottom=","accent="};
    u32 values[3]={0};
    for(i32 i=0;i<3;i++){
        bool found=false;
        for(u32 k=9;k+12<f->len;k++){
            if((k==0||f->data[k-1]=='\n')&&
               k_strncmp(f->data+k,keys[i],(i32)k_strlen(keys[i]))==0){
                bool ok=true;
                values[i]=parse6(f->data+k+k_strlen(keys[i]),&ok);
                if(!ok){state="Theme contains invalid hex color";return;}
                found=true;break;
            }
        }
        if(!found){state="Theme missing color field";return;}
    }
    SET.user_wallpaper_top=values[0];
    SET.user_wallpaper_bottom=values[1];
    SET.user_accent_color=values[2];
    SET.user_theme_active=1;
    (void)save();
}
void theme_creator_input(i32 key){
    if(key==KEY_LEFT)part=(part+2)%3;
    else if(key==KEY_RIGHT)part=(part+1)%3;
    else if(key==KEY_UP||key==KEY_DOWN){
        u32 *ptr=color_field();
        u32 shift=(u32)(2-channel)*8u;
        u32 value=(*ptr>>shift)&255u;
        i32 step=(kbd_mod_state()&KMOD_SHIFT)?1:8;
        i32 next=(i32)value+(key==KEY_UP?step:-step);
        if(next<0)next=0;
        if(next>255)next=255;
        *ptr=(*ptr&~(255u<<shift))|((u32)next<<shift);
        SET.user_theme_active=1;
    }else if(key==KEY_TAB)channel=(channel+1)%3;
    else if(key=='p'||key=='P'){
        preset=(preset+1)%5;
        SET.user_wallpaper_top=presets[preset][0];
        SET.user_wallpaper_bottom=presets[preset][1];
        SET.user_accent_color=presets[preset][2];
        SET.user_theme_active=1;
        state="Preset applied. Press S to persist.";
    }else if(key=='s'||key=='S'||key==KEY_ENTER)(void)save();
    else if(key=='e'||key=='E')theme_creator_export();
    else if(key=='i'||key=='I')import();
    else if(key=='d'||key=='D'){
        SET.user_theme_active=0;
        state="Builtin theme restored. Press S to persist.";
    }else if(key==' '){
        SET.user_theme_active=!SET.user_theme_active;
        state=SET.user_theme_active?"My theme ACTIVE":"Builtin theme ACTIVE";
    }
}
void theme_creator_render(i32 x,i32 y,i32 w,i32 h,u32 frame){
    (void)frame;
    if(w<340||h<290)return;
    gfx_round_rect(x+15,y+9,w-30,92,20,0x0D273Du);
    gfx_circle(x+60,y+56,25,SET.user_accent_color&0xFFFFFFu);
    gfx_circle_outline(x+60,y+56,27,0xFFFFFFu);
    gfx_text_lg(x+103,y+23,"Theme Creator",0xF1FAFFu);
    gfx_text(x+104,y+67,"Your own colors. Your own desktop.",0xA5CDDCu);
    i32 sx=x+27,sy=y+121,sw=w-54,preview=h/3;
    if(preview>315)preview=315;
    u32 a=SET.user_wallpaper_top,b=SET.user_wallpaper_bottom;
    for(i32 row=0;row<preview;row++){
        u32 top=(u32)(preview-row),bottom=(u32)row;
        u32 red=(((a>>16)&255u)*top+((b>>16)&255u)*bottom)/(u32)preview;
        u32 green=(((a>>8)&255u)*top+((b>>8)&255u)*bottom)/(u32)preview;
        u32 blue=((a&255u)*top+(b&255u)*bottom)/(u32)preview;
        gfx_rect(sx,sy+row,sw,1,(red<<16)|(green<<8)|blue);
    }
    gfx_round_outline(sx,sy,sw,preview,16,0xE1EDF4u);
    gfx_round_rect_a(sx+22,sy+32,sw*57/100,preview*48/100,15,0xF5F9FEu,214);
    gfx_text_lg(sx+49,sy+47,"FalconOS",0x173044u);
    gfx_text(sx+49,sy+98,"My custom workspace",0x547488u);
    gfx_round_rect(sx+49,sy+126,115,33,10,SET.user_accent_color&0xFFFFFFu);
    gfx_text(sx+66,sy+135,"Accent",0x102B39u);
    i32 columns_y=sy+preview+22;
    const char *labels[]={"BACKGROUND TOP","BACKGROUND BOTTOM","ACCENT"};
    for(i32 i=0;i<3;i++){
        i32 cw=(sw-24)/3,cx=sx+i*(cw+12);
        u32 color=i==0?SET.user_wallpaper_top:(i==1?SET.user_wallpaper_bottom:SET.user_accent_color);
        gfx_round_rect(cx,columns_y,cw,85,13,part==i?PAL_ACCENT_DIM:PAL_PANEL_DEEP);
        gfx_round_outline(cx,columns_y,cw,85,13,part==i?PAL_ACCENT:PAL_HAIRLINE);
        gfx_circle(cx+27,columns_y+37,13,color);
        gfx_text(cx+50,columns_y+14,labels[i],PAL_TEXT_DIM);
        char val[8];hex6(val,color);
        gfx_text(cx+50,columns_y+46,val,PAL_TEXT);
        if(part==i)gfx_rect(cx+10,columns_y+73,cw-20,3,PAL_ACCENT);
    }
    char selected[32];
    k_strcpy(selected,"CHANNEL: ");
    k_strcat(selected,channel==0?"R":(channel==1?"G":"B"));
    gfx_text(sx,columns_y+106,selected,PAL_ACCENT);
    gfx_text(sx,columns_y+130,"Left/Right color   Tab RGB   Up/Down change (+/- 8)",PAL_TEXT);
    gfx_text(sx,columns_y+155,"P presets   Space enable   S save   E export .fth   I import",PAL_TEXT_DIM);
    gfx_text(sx,columns_y+184,state,PAL_TEXT_DIM);
#ifdef FALCON_QEMU_UI_GALLERY
    outb(0xE9,'t');outb(0xE9,'R');
#endif
}
