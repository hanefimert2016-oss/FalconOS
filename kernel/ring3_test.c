/* Real hardware privilege transition proof, QEMU-only opt-in build.
 *
 * Implements GDT user segments, a TSS/RSP0 kernel stack, a guarded physical
 * user page, U/S page table flags and an actual INT 0x80 syscall from CPL3.
 *
 * NOT a general process scheduler or ELF executor; this test only launches
 * pre-reviewed probe machine code, and restores the original kernel stack.
 */
#include "falcon.h"
#ifdef FALCON_RING3_TEST
#define USER_PAGE 0x20000000ull
#define USER_STACK (USER_PAGE+0x2000ull)
typedef struct __attribute__((packed)) {
    u32 reserved0;
    u64 rsp0,rsp1,rsp2;
    u64 reserved1;
    u64 ist[7];
    u64 reserved2;
    u16 reserved3,io_map;
} x64_tss;
typedef struct __attribute__((packed)) {u16 limit;u64 base;} gdt_pointer;
static u64 ring3_gdt[7] __attribute__((aligned(16)));
static x64_tss TSS;
static u8 trap_stack[32768] __attribute__((aligned(16)));
static u64 user_page_table[512] __attribute__((aligned(4096)));
extern void ring3_enter(u64 instruction_pointer,u64 stack_pointer);
void ring3_gdt_install(void){
    k_memset(ring3_gdt,0,sizeof ring3_gdt);
    k_memset(&TSS,0,sizeof TSS);
    ring3_gdt[1]=0x00209A0000000000ull; /* 0x08 kernel code */
    ring3_gdt[2]=0x0000920000000000ull; /* 0x10 kernel data */
    ring3_gdt[3]=0x0020FA0000000000ull; /* 0x18 user code (+RPL3) */
    ring3_gdt[4]=0x0000F20000000000ull; /* 0x20 user data (+RPL3) */
    TSS.rsp0=(u64)(uintptr_t)(trap_stack+sizeof trap_stack);
    TSS.io_map=(u16)sizeof TSS; /* no user-space I/O port access */
    u64 base=(u64)(uintptr_t)&TSS;
    u64 limit=sizeof(TSS)-1u;
    ring3_gdt[5]=(limit&0xFFFFu)|((base&0xFFFFFFu)<<16)|
        ((u64)0x89<<40)|(((limit>>16)&15u)<<48)|
        (((base>>24)&255u)<<56);
    ring3_gdt[6]=base>>32;
    gdt_pointer ptr={sizeof(ring3_gdt)-1u,(u64)(uintptr_t)ring3_gdt};
    __asm__ volatile(
        "lgdt %0\n\t"
        "pushq $0x08\n\t"
        "leaq 1f(%%rip),%%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        "movw $0x10,%%ax\n\t"
        "movw %%ax,%%ds\n\t"
        "movw %%ax,%%es\n\t"
        "movw %%ax,%%fs\n\t"
        "movw %%ax,%%gs\n\t"
        "movw %%ax,%%ss\n\t"
        "movw $0x28,%%ax\n\t"
        "ltr %%ax\n\t"
        :: "m"(ptr) : "rax","memory");
}
static bool free_ram(u64 start,u64 bytes){
    for(i32 i=0;i<MMAP_N;i++){
        if(MMAP[i].type!=1)continue;
        u64 low=MMAP[i].base,length=MMAP[i].length;
        if(start>=low && length>=bytes && start-low<=length-bytes)return true;
    }
    return false;
}
static bool map_guarded_user(const u8 *code,u32 code_bytes){
    if(!code || !code_bytes || code_bytes>4096)return false;
    if(!free_ram(USER_PAGE,0x3000))return false;
    u64 cr3=0;
    __asm__ volatile("mov %%cr3,%0":"=r"(cr3));
    u64 *root=(u64 *)(uintptr_t)(cr3&~0xfffull);
    if(!(root[0]&1u))return false;
    u64 *pdpt=(u64 *)(uintptr_t)(root[0]&~0xfffull);
    if(!(pdpt[0]&1u))return false;
    u64 *pd=(u64 *)(uintptr_t)(pdpt[0]&~0xfffull);
    u32 index=(u32)(USER_PAGE>>21)&511u;
    /* Require the original identity-mapped supervisor 2MiB page. */
    bool old_huge=(pd[index]&0x83u)==0x83u &&
                   (pd[index]&~0x1FFFFFull)==USER_PAGE;
    bool already=(pd[index]&~0xfffull)==(u64)(uintptr_t)user_page_table &&
                 (pd[index]&7u)==7u;
    if(!old_huge&&!already)return false;
    if(already){
        /* Make user code page writable only while no CPL3 task is running. */
        user_page_table[0]=USER_PAGE|0x07ull;
        __asm__ volatile("invlpg (%0)"::"r"((void *)(uintptr_t)USER_PAGE):"memory");
    }
    k_memset((void *)(uintptr_t)USER_PAGE,0,4096);
    k_memcpy((void *)(uintptr_t)USER_PAGE,code,code_bytes);
    k_memset((void *)(uintptr_t)USER_STACK,0,4096);
    k_memset(user_page_table,0,sizeof user_page_table);
    /* User executable page: present + user, not writable. */
    user_page_table[0]=USER_PAGE|0x05ull;
    /* Page 1 unmapped guard, page 2 writable user stack. */
    user_page_table[2]=USER_STACK|0x07ull;
    root[0]|=4u;pdpt[0]|=4u;
    pd[index]=(u64)(uintptr_t)user_page_table|0x07ull;
    __asm__ volatile("mov %0,%%cr3"::"r"(cr3):"memory");
    return true;
}
/* A tiny reviewed regression binary, never exposed as general capability. */
static const u8 kernel_probe[]={
      0xB8,0x01,0x00,0x00,0x00, /* mov eax, 1 - write diagnostic */
      0xCD,0x80,               /* int 0x80 */
      0xB8,0x02,0x00,0x00,0x00, /* mov eax, 2 - exit */
      0xCD,0x80,               /* int 0x80 */
      0x0F,0x0B                /* ud2: impossible to return into user code */
    };
};
static bool ring3_exited;
u64 ring3_syscall_dispatch(u64 number){
    u16 cs;
    __asm__ volatile("mov %%cs,%0":"=r"(cs));
    if((cs&3u)!=0) return (u64)-1;
    if(number==1){outb(0xE9,'U');return 0;}
    if(number==2){ring3_exited=true;return 1;} /* exit -> saved Ring0 stack */
    if(number==3)return 42; /* getpid: experiment has one user process */
    if(number==4)return g_ticks; /* time ticks, no user pointer */
    return (u64)-38; /* ENOSYS */
}
bool ring3_probe(void){
    if(!map_guarded_user(kernel_probe,sizeof kernel_probe))return false;
    ring3_exited=false;
    ring3_enter(USER_PAGE,USER_STACK+4096-16);
    return ring3_exited;
}

/* Constrained ELF64 loader: at most one executable PT_LOAD segment of one
 * page. Fails closed; no dynamic linking, imports, relocation, writable code
 * or arbitrary kernel addresses. A future VM manager must own CR3/TSS.
 */
bool ring3_run_elf(const u8 *elf,u32 size){
    static u8 arena[2u*1024u*1024u];
    u64 entry=0;
    u32 segments=0;
    if(!elf64_inspect(elf,size,&entry,&segments) || segments!=1 ||
       entry<0x400000ull || entry>=0x401000ull)return false;
    /* An ELF program may only contain one executable segment starting at
     * the base and fitting in a 4KiB user code page. */
    if(size<120u)return false;
    const u8 *p=elf+64;
    u32 phoff=(u32)elf[32]|((u32)elf[33]<<8)|
              ((u32)elf[34]<<16)|((u32)elf[35]<<24);
    if(phoff>size-56u)return false;
    p=elf+phoff;
    u32 typ=(u32)p[0]|((u32)p[1]<<8);
    u32 flags=(u32)p[4];
    u32 memsz=(u32)p[40]|((u32)p[41]<<8)|((u32)p[42]<<16)|((u32)p[43]<<24);
    u64 vaddr=(u64)p[16]|((u64)p[17]<<8)|((u64)p[18]<<16)|((u64)p[19]<<24);
    if(typ!=1||flags!=5||vaddr!=0x400000ull||memsz>4096u)return false;
    u64 staged_entry=0;
    if(!elf64_stage(elf,size,arena,sizeof arena,&staged_entry))return false;
    if(!map_guarded_user(arena,4096u))return false;
    ring3_exited=false;
    ring3_enter(USER_PAGE+(staged_entry-0x400000ull),USER_STACK+4096-16);
    return ring3_exited;
}
#ifdef FALCON_QEMU_ELF_TEST
static void set16(u8 *p,u16 n){p[0]=(u8)n;p[1]=(u8)(n>>8);}
static void set64(u8 *p,u64 n){
    for(u32 i=0;i<8;i++)p[i]=(u8)(n>>(i*8));
}
bool ring3_elf_demo(void){
    u8 image[192];k_memset(image,0,sizeof image);
    image[0]=0x7F;image[1]='E';image[2]='L';image[3]='F';
    image[4]=2;image[5]=1;image[6]=1;
    set16(image+16,2);set16(image+18,62);
    image[20]=1;set64(image+24,0x400000ull);
    set64(image+32,64);set16(image+52,64);
    set16(image+54,56);set16(image+56,1);
    image[64]=1;image[68]=5;set64(image+72,128);
    set64(image+80,0x400000ull);set64(image+96,30);
    set64(image+104,4096);set64(image+112,1);
    /* mov eax, SYS_getpid; int 0x80; cmp eax, 42; jne UD2;
     * mov eax, SYS_diag; int 0x80; mov eax, SYS_exit; int 0x80 */
    static const u8 program[]={
        0xB8,3,0,0,0,0xCD,0x80,
        0x3D,42,0,0,0,0x75,0x0E,
        0xB8,1,0,0,0,0xCD,0x80,
        0xB8,2,0,0,0,0xCD,0x80,0x0F,0x0B
    };
    k_memcpy(image+128,program,sizeof program);
    return ring3_run_elf(image,128u+sizeof program);
}
#endif

#endif
