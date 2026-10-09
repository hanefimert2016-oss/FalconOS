#include "falcon.h"
#include "shfs.h"
#include <stdio.h>
#include <string.h>
volatile u32 g_ticks=0;
i32 k_strlen(const char *s){return (i32)strlen(s);}
i32 k_strcmp(const char *a,const char *b){return strcmp(a,b);}
char *k_strcat(char *a,const char *b){return strcat(a,b);}
char *k_strcpy(char *a,const char *b){return strcpy(a,b);}
void k_memset(void *a,u8 v,u32 n){memset(a,v,n);}
void k_memcpy(void *a,const void *b,u32 n){memcpy(a,b,n);}
void k_itoa(u32 x,char *p,i32 b){(void)b;sprintf(p,"%u",x);}
char shfs_cwd[SHFS_PATH]="/home/falcon";
shfs_ent_t *shfs_lookup_rel(const char *cwd,const char *rel){(void)cwd;(void)rel;return NULL;}
#define FAIL(n) do{fprintf(stderr,"fvm failed line %d\n",__LINE__);return n;}while(0)
int main(void){
 const char *p="FVM/1\nPUSH 40\nPUSH 2\nADD\nPRINT\nHALT\n";
 i32 slot=fvm_spawn_source("arithmetic",p,(u32)strlen(p));
 if(slot!=0)FAIL(1);
 fvm_tick();
 if(fvm_state(slot)!=3 || fvm_last_print(slot)!=42)FAIL(2);
 const char *loop="FVM/1\nPUSH 0\nSTORE 0\nLOAD 0\nPUSH 1\nADD\nDUP\nSTORE 0\nPRINT\nSLEEP 5\nJMP 2\n";
 slot=fvm_spawn_source("counter",loop,(u32)strlen(loop));
 if(slot<0)FAIL(3);
 fvm_tick();if(fvm_last_print(slot)!=1 || fvm_state(slot)!=2)FAIL(4);
 g_ticks+=4;fvm_tick();if(fvm_last_print(slot)!=1)FAIL(5);
 g_ticks++;fvm_tick();if(fvm_last_print(slot)!=2)FAIL(6);
 const char *invalid="FVM/1\nPUSH 1\nJMP 250\n";
 if(fvm_spawn_source("bad",invalid,(u32)strlen(invalid))>=0)FAIL(7);
 const char *hostile="FVM/1\nPUSH 0\nLOAD 65\n";
 if(fvm_spawn_source("bad",hostile,(u32)strlen(hostile))>=0)FAIL(8);
 char info[512];fvm_status(info,sizeof info);
 if(!strstr(info,"counter"))FAIL(9);
 if(!fvm_kill(slot)||fvm_state(slot)!=3)FAIL(10);
 puts("PASS bounded multi-instance FVM arithmetic, scheduling, sleep, validation");
 return 0;
}
