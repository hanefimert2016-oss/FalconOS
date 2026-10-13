/* FalconOS V2 native System Updater. Separate COM2 channel from
 * Marketplace's COM1. Only QEMU host updater can replace the guest ISO.
 * No direct production disk writes. Auto-CHECK by default, optional
 * auto-STAGE, ALWAYS reboot via normal Power menu. Never fake success.
 */
#include "falcon.h"
#include "version.h"
#define UPDATE_PORT 0x2F8
#define UPDATE_LINE 136
static char line[UPDATE_LINE],label[48],details[120];
static i32 line_len;
static u32 last_check,launch_ms;
static bool auto_stage,has_update,staged,checking,installing,boot_started;
static char next_version[28];
static void send_char(char c){
    u32 watchdog=40000;
    while(watchdog-- && !(inb(UPDATE_PORT+5)&0x20)){}
    if(watchdog)outb(UPDATE_PORT,(u8)c);
}
static void send(const char *message){
    while(*message)send_char(*message++);
}
static bool token_version(const char *p,i32 max){
    i32 n=0;bool digit=false;
    if(!p||!p[0])return false;
    while(p[n]){
        char c=p[n];
        if((c>='0'&&c<='9'))digit=true;
        else if(c!='.'&&c!='-'&&!(c>='a'&&c<='z'))return false;
        if(++n>=max)return false;
    }
    return digit;
}
static void copy(char *dst,i32 cap,const char *src){
    i32 n=0;
    if(!cap)return;
    while(src[n]&&n<cap-1){dst[n]=src[n];n++;}
    dst[n]=0;
}
static void status(const char *s,const char *d){
    copy(label,sizeof label,s);
    copy(details,sizeof details,d);
}
static void accept(const char *packet){
    if(k_strncmp(packet,"AVAILABLE|",10)==0){
        const char *v=packet+10;
        if(!token_version(v,sizeof next_version))return;
        copy(next_version,sizeof next_version,v);
        has_update=true;checking=false;
        status("Update available","Verified update candidate found. Select Install.");
        outb(0xE9,'u');outb(0xE9,'A');
        if(auto_stage)os_update_apply();
    }else if(k_strncmp(packet,"CURRENT|",8)==0){
        checking=false;has_update=false;
        status("Up to date","No newer approved FalconOS ISO available.");
        outb(0xE9,'u');outb(0xE9,'C');
    }else if(k_strncmp(packet,"STAGED|",7)==0){
        if(token_version(packet+7,sizeof next_version)){
            checking=false;installing=false;staged=true;
            copy(next_version,sizeof next_version,packet+7);
            status("Restart required","Verified ISO inserted into QEMU virtual CD. Restart to boot new OS.");
            outb(0xE9,'u');outb(0xE9,'S');
        }
    }else if(k_strncmp(packet,"BUSY|",5)==0){
        status("Update downloading","Fetching and verifying ISO on host. Keep VM running.");
    }else if(k_strncmp(packet,"ERROR|",6)==0){
        installing=false;checking=false;
        status("Update failed",packet+6);
        outb(0xE9,'u');outb(0xE9,'E');
    }
}
void os_update_init(void){
    outb(UPDATE_PORT+1,0);
    outb(UPDATE_PORT+3,0x80);
    outb(UPDATE_PORT+0,1);outb(UPDATE_PORT+1,0);
    outb(UPDATE_PORT+3,3);outb(UPDATE_PORT+2,0xC7);
    outb(UPDATE_PORT+4,3);
    auto_stage=SET.update_auto_install;
    last_check=launch_ms=pit_ms();
    status("System Updates","Checking automatically after desktop opens. Manual check: C.");
}
void os_update_check(void){
    if(installing)return;
    send("CHECK|" FALCON_RELEASE_SEMVER "\n");
    checking=true;
    last_check=pit_ms();
    status("Checking for updates","Request sent to read-only QEMU update bridge.");
    outb(0xE9,'u');outb(0xE9,'Q');
}
void os_update_apply(void){
    if(!has_update||installing||staged)return;
    send("INSTALL|");send(next_version);send("\n");
    installing=true;status("Update downloading",
       "New ISO will be verified on host and loaded into virtual CD-ROM.");
    outb(0xE9,'u');outb(0xE9,'I');
}
void os_update_poll(void){
    i32 budget=192;
    while(budget-- >0 && (inb(UPDATE_PORT+5)&1)){
        u8 c=inb(UPDATE_PORT);
        if(c=='\r')continue;
        if(c=='\n'){
            line[line_len]=0;
            accept(line);line_len=0;
        }else if(c>=32&&c<=126){
            if(line_len<UPDATE_LINE-1)line[line_len++]=(char)c;
            else line_len=0;
        }
    }
    u32 now=pit_ms();
    /* Timed automatic checks are non-privileged. Existing work is never
     * discarded by a surprise reboot. Auto-stage is explicitly opt-in. */
    if(!boot_started){
        if((u32)(now-launch_ms)>15000u){
            boot_started=true;os_update_check();
        }
    }else if((u32)(now-last_check)>21600000u&&!checking&&!installing)
        os_update_check();
    if(checking&&(u32)(now-last_check)>65000u){
        checking=false;status("Updater offline",
            "Start make run to enable HTTPS update bridge.");
    }
}
const char *os_update_status(void){return label;}
const char *os_update_details(void){return details;}
const char *os_update_version(void){return next_version;}
bool os_update_available(void){return has_update;}
bool os_update_staged(void){return staged;}
bool os_update_checking(void){return checking;}
bool os_update_installing(void){return installing;}
bool os_update_auto_stage(void){return auto_stage;}
void os_update_toggle_auto(void){
    auto_stage=!auto_stage;
    status(auto_stage?"Automatic install ON":"Automatic install OFF",
        "Checks happen automatically. Auto-install is opt-in. Restart is manual.");
    if(auto_stage&&has_update&&!staged)os_update_apply();
}
