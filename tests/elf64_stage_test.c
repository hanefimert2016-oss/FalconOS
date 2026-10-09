#include "falcon.h"
#include <stdio.h>
#include <string.h>
static u8 elf[256];
static u8 user_arena[2*1024*1024];
void k_memset(void *p,u8 c,u32 n){memset(p,c,n);}
void k_memcpy(void *a,const void *b,u32 n){memcpy(a,b,n);}
static void put16(u32 p,u16 x){elf[p]=(u8)x;elf[p+1]=(u8)(x>>8);}
static void put32(u32 p,u32 x){for(u32 i=0;i<4;i++)elf[p+i]=(u8)(x>>(8*i));}
static void put64(u32 p,u64 x){for(u32 i=0;i<8;i++)elf[p+i]=(u8)(x>>(8*i));}
#define ASSERT(x) do{if(!(x)){fprintf(stderr,"ELF test fail line %d\n",__LINE__);return 1;}}while(0)
int main(void){
 memset(elf,0,sizeof elf);
 elf[0]=0x7f;elf[1]='E';elf[2]='L';elf[3]='F';elf[4]=2;elf[5]=1;elf[6]=1;
 put16(16,2);put16(18,62);put32(20,1);
 put64(24,0x400000);put64(32,64);
 put16(52,64);put16(54,56);put16(56,1);
 put32(64,1);put32(68,5); /* PT_LOAD: R|X only */
 put64(72,120);put64(80,0x400000);
 put64(96,4);put64(104,4096);put64(112,1);
 elf[120]=0x48;elf[121]=0x31;elf[122]=0xC0;elf[123]=0xC3;
 u64 entry=0;u32 n=0;
 ASSERT(elf64_inspect(elf,124,&entry,&n)&&entry==0x400000&&n==1);
 ASSERT(elf64_stage(elf,124,user_arena,sizeof user_arena,&entry));
 ASSERT(user_arena[0]==0x48&&user_arena[4095]==0);
 ASSERT(!elf64_stage(elf,124,user_arena,128,&entry));
 put32(68,7);ASSERT(!elf64_inspect(elf,124,&entry,&n)); /* W|X */
 put32(68,5);
 put64(96,5000);ASSERT(!elf64_inspect(elf,124,&entry,&n));
 put64(96,4);
 put64(24,0x700000);ASSERT(!elf64_inspect(elf,124,&entry,&n));
 put64(24,0x400000);
 put64(104,0x400000);ASSERT(!elf64_inspect(elf,124,&entry,&n));
 put64(104,4096);
 elf[4]=1;ASSERT(!elf64_inspect(elf,124,&entry,&n)); /* ELF32 */
 puts("PASS safe ELF64 validation: segment bounds, W^X, entry, staging, corrupted sizes");
 return 0;
}
