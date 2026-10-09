/* Experimental NVMe admin Identify implementation. QEMU test mode ONLY.
 *
 * This supports discovery, controller RESET/ENABLE, ASQ/ACQ setup and
 * Identify Controller + Identify Namespace, all via a real NVMe MMIO queue.
 * There are NO namespace reads/writes, no DMA memory allocator, no IRQ,
 * no namespace filesystem integration. Do not run on physical SSDs.
 */
#include "falcon.h"
#ifdef FALCON_QEMU_NVME_ID_TEST
static u8 sq[4096] __attribute__((aligned(4096)));
static u8 cq[4096] __attribute__((aligned(4096)));
static u8 identify[4096] __attribute__((aligned(4096)));
static u64 base;
static u32 tail,head;
static bool ready;
static u64 namespace_lbas;

static u32 read32(u32 off){
    return *(volatile u32 *)(uintptr_t)(base+(u64)off);
}
static void write32(u32 off,u32 n){
    *(volatile u32 *)(uintptr_t)(base+(u64)off)=n;
}
static void put16(u8 *p,u16 n){p[0]=(u8)n;p[1]=(u8)(n>>8);}
static void put32(u8 *p,u32 n){
    p[0]=(u8)n;p[1]=(u8)(n>>8);p[2]=(u8)(n>>16);p[3]=(u8)(n>>24);
}
static void put64(u8 *p,u64 n){put32(p,(u32)n);put32(p+4,(u32)(n>>32));}
static u64 get64(const u8 *p){
    u64 v=0;for(u32 i=0;i<8;i++)v|=(u64)p[i]<<(i*8);
    return v;
}
static u32 pci_read(u32 bdf,u32 off){
    u32 bus=(bdf>>16)&255u,device=(bdf>>8)&31u,fun=bdf&7u;
    u32 a=0x80000000u|(bus<<16)|(device<<11)|(fun<<8)|(off&0xfcu);
    __asm__ volatile("outl %0,%1"::"a"(a),"Nd"((u16)0xcf8));
    u32 v;
    __asm__ volatile("inl %1,%0":"=a"(v):"Nd"((u16)0xcfc));
    return v;
}
static void pci_write(u32 bdf,u32 off,u32 n){
    u32 bus=(bdf>>16)&255u,device=(bdf>>8)&31u,fun=bdf&7u;
    u32 a=0x80000000u|(bus<<16)|(device<<11)|(fun<<8)|(off&0xfcu);
    __asm__ volatile("outl %0,%1"::"a"(a),"Nd"((u16)0xcf8));
    __asm__ volatile("outl %0,%1"::"a"(n),"Nd"((u16)0xcfc));
}
static bool wait_ready(bool expected){
    for(u32 spin=0;spin<5000000u;spin++){
        u32 status=read32(0x1c);
        if(status&2u)return false; /* CFS fatal controller error */
        if(((status&1u)!=0)==expected)return true;
        __asm__ volatile("pause");
    }
    return false;
}
static bool admin_identify(u32 namespace_id,u32 cns,u32 cid){
    k_memset(identify,0,sizeof identify);
    u8 *cmd=sq+tail*64u;
    k_memset(cmd,0,64u);
    cmd[0]=0x06u; /* Identify */
    put16(cmd+2,(u16)cid);
    put32(cmd+4,namespace_id);
    put64(cmd+24,(u64)(uintptr_t)identify); /* PRP1 physically contiguous */
    put32(cmd+40,cns);
    /* Publish SQ entry to QEMU before tail doorbell. */
    __asm__ volatile("mfence":::"memory");
    tail=(tail+1u)&15u;
    write32(0x1000,tail);
    u8 *completion=cq+head*16u;
    for(u32 spin=0;spin<5000000u;spin++){
        u16 status=(u16)completion[14]|((u16)completion[15]<<8);
        if((status&1u)!=1u){
            __asm__ volatile("pause");
            continue;
        }
        u16 command_id=(u16)completion[12]|((u16)completion[13]<<8);
        if(command_id!=(u16)cid || (status>>1)!=0u)return false;
        head=(head+1u)&15u;
        __asm__ volatile("mfence":::"memory");
        write32(0x1004,head); /* CQ head doorbell */
        return true;
    }
    return false;
}
bool nvme_qemu_identify(void){
    ready=false;namespace_lbas=0;
    if(!pci_extended_count(1) || !pci_extended_mmio(1))return false;
    base=(u64)pci_extended_info(1,2);
    if(base<0x100000u || base>=0xffff0000u)return false;
    u32 command_address=pci_extended_info(1,6);
    u32 cmd=pci_read(command_address,4);
    pci_write(command_address,4,cmd|6u); /* PCI Memory + DMA mastering */
    u32 cap_lo=read32(0x00),cap_hi=read32(0x04);
    if((cap_lo&65535u)<15u || ((cap_hi>>16)&15u)>0 ||
       ((cap_hi&15u)>7u))return false; /* require 4K MPS + safe DSTRD */
    u32 cc=read32(0x14);
    write32(0x14,cc&~1u);
    if(!wait_ready(false))return false;
    k_memset(sq,0,sizeof sq);k_memset(cq,0,sizeof cq);
    tail=head=0;
    write32(0x24,15u|(15u<<16)); /* 16-entry admin SQ/CQ */
    write32(0x28,(u32)(uintptr_t)sq);
    write32(0x2c,0);
    write32(0x30,(u32)(uintptr_t)cq);
    write32(0x34,0);
    write32(0x14,(6u<<16)|(4u<<20)|1u); /* IOSQES=6, IOCQES=4, EN */
    if(!wait_ready(true))return false;
    if(!admin_identify(0,1,1))return false;  /* controller identification */
    /* Mandatory ID fields are nonzero in the actual controller payload. */
    if(!identify[4] && !identify[5] && !identify[6])return false;
    if(!admin_identify(1,0,2))return false;  /* namespace identification */
    namespace_lbas=get64(identify);
    ready=namespace_lbas!=0;
    return ready;
}
bool nvme_qemu_ready(void){return ready;}
u64 nvme_qemu_namespace_lbas(void){return namespace_lbas;}
#endif
