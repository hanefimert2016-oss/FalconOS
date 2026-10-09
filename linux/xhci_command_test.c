/* FalconOS experimental QEMU xHCI hardware initialization / No-op TRB.
 * Executes a real command-ring DMA completion event on qemu-xhci.
 * DOES NOT enumerate HID/USB storage devices or expose transfer endpoints.
 * Not enabled in release builds; never reset a live laptop controller.
 */
#include "falcon.h"
#ifdef FALCON_QEMU_XHCI_CMD_TEST
static u64 dcbaa[256] __attribute__((aligned(64)));
static u32 command_ring[1024] __attribute__((aligned(64)));
static u32 event_ring[1024] __attribute__((aligned(64)));
static u64 erst[2] __attribute__((aligned(64)));
static u64 xhci_base;
static bool succeeded;

static u32 read32(u32 off){return *(volatile u32 *)(uintptr_t)(xhci_base+off);}
static void write32(u32 off,u32 n){
    *(volatile u32 *)(uintptr_t)(xhci_base+off)=n;
}
static void write64(u32 off,u64 n){
    write32(off,(u32)n);write32(off+4,(u32)(n>>32));
}
static u32 pci_read(u32 bdf,u32 off){
    u32 bus=(bdf>>16)&255u,dev=(bdf>>8)&31u,fn=bdf&7u;
    u32 address=0x80000000u|(bus<<16)|(dev<<11)|(fn<<8)|(off&0xfcu);
    __asm__ volatile("outl %0,%1"::"a"(address),"Nd"((u16)0xcf8));
    u32 result;
    __asm__ volatile("inl %1,%0":"=a"(result):"Nd"((u16)0xcfc));
    return result;
}
static void pci_write(u32 bdf,u32 off,u32 value){
    u32 bus=(bdf>>16)&255u,dev=(bdf>>8)&31u,fn=bdf&7u;
    u32 address=0x80000000u|(bus<<16)|(dev<<11)|(fn<<8)|(off&0xfcu);
    __asm__ volatile("outl %0,%1"::"a"(address),"Nd"((u16)0xcf8));
    __asm__ volatile("outl %0,%1"::"a"(value),"Nd"((u16)0xcfc));
}
static bool wait_for(u32 off,u32 mask,u32 target){
    for(u32 tries=0;tries<5000000;tries++){
        if((read32(off)&mask)==target)return true;
        __asm__ volatile("pause");
    }
    return false;
}
bool xhci_qemu_noop(void){
    succeeded=false;
    if(!pci_extended_count(2)||!pci_extended_mmio(2))return false;
    xhci_base=(u64)pci_extended_info(2,2);
    if(xhci_base<0x100000ull || xhci_base>0xfff00000ull)return false;
    u32 bdf=pci_extended_info(2,6);
    pci_write(bdf,4,pci_read(bdf,4)|6u);
    u32 cap=read32(0);
    u32 op=cap&255u;
    u32 slots=read32(4)&255u;
    u32 db=read32(0x14)&~3u;
    u32 runtime=read32(0x18)&~31u;
    if(op<0x20||op>0x100||slots<1||db<0x100||runtime<0x100 ||
       (u64)db+4>0x100000000ull-xhci_base ||
       (u64)runtime+0x80>0x100000000ull-xhci_base)return false;
    write32(op,read32(op)&~1u); /* halt host controller */
    if(!wait_for(op+4,1u,1u))return false; /* USBSTS.HCH */
    write32(op,read32(op)|2u);  /* Host Controller Reset */
    if(!wait_for(op,2u,0u)||!wait_for(op+4,1u<<11,0u))return false;
    if(!(read32(op+8)&1u))return false; /* 4KiB supported */
    k_memset(dcbaa,0,sizeof dcbaa);
    k_memset(command_ring,0,sizeof command_ring);
    k_memset(event_ring,0,sizeof event_ring);
    erst[0]=(u64)(uintptr_t)event_ring;
    erst[1]=256u;
    write32(op+0x38,slots>8?8:slots);
    write64(op+0x30,(u64)(uintptr_t)dcbaa);
    write64(op+0x18,(u64)(uintptr_t)command_ring|1u); /* CRCR.RCS=1 */
    u32 ir0=runtime+0x20u;
    write32(ir0,2u); /* clear pending interrupt, no IRQ enable */
    write32(ir0+8,1u); /* ERSTSZ = 1 */
    write64(ir0+0x10,(u64)(uintptr_t)erst);
    write64(ir0+0x18,(u64)(uintptr_t)event_ring);
    write32(op,read32(op)|1u); /* run, no interrupts */
    if(!wait_for(op+4,1u,0u))return false;
    /* No-op Command TRB, Type 23, Cycle=1. */
    command_ring[0]=0;
    command_ring[1]=0;
    command_ring[2]=0;
    command_ring[3]=(23u<<10)|1u;
    __asm__ volatile("mfence":::"memory");
    write32(db,0); /* doorbell[0] rings command queue */
    for(u32 tries=0;tries<5000000u;tries++){
        u32 control=event_ring[3];
        if(!(control&1u))continue;
        u32 type=(control>>10)&63u;
        u32 result=(event_ring[2]>>24)&255u;
        u64 pointer=(u64)event_ring[0]|((u64)event_ring[1]<<32);
        if(type!=33 || result!=1u || pointer!=(u64)(uintptr_t)command_ring)break;
        /* Dequeue event and update EHB in ERDP, no busy interrupts. */
        write64(ir0+0x18,(u64)(uintptr_t)(event_ring+4)|8u);
        succeeded=true;
        break;
    }
    write32(op,read32(op)&~1u);
    return succeeded;
}
bool xhci_qemu_ready(void){return succeeded;}
#endif
