/* FalconOS PCI capability discovery, 64-bit BAR-safe read-only probes.
 *
 * NVMe / xHCI / VGA are separate hardware classes. Nothing here declares
 * operational NVMe namespace I/O, USB transfers or GPU acceleration.
 * BAR bounds checked against the bootloader's 0..4 GiB identity mapping;
 * no arbitrary unverified physical pointer is dereferenced.
 */
#include "falcon.h"
typedef struct {
    u16 vendor,device;
    u8 bus,dev,fun,revision;
    u32 class_code;
    u64 bar0;
    u32 cap0,cap1,cap2;
    bool mmio_readable;
} pci_capability_t;
static pci_capability_t nvme,usb3,gpu;
static u32 found_nvme,found_usb3,found_gpu;
/* Read-only PCI configuration space inventory. This is actual bus discovery,
 * not a declaration that arbitrary controllers have functional drivers. */
#define PCI_INVENTORY_LIMIT 48u
typedef struct {
    u32 id,cls,bdf,bar0,revision,caps;
} pci_inventory_entry_t;
static pci_inventory_entry_t inventory[PCI_INVENTORY_LIMIT];
static u32 inventory_used;
u32 pci_inventory_count(void){return inventory_used;}
u32 pci_inventory_field(u32 index,u32 field){
    if(index>=inventory_used)return 0;
    const pci_inventory_entry_t *e=&inventory[index];
    if(field==0)return e->id; /* Device<<16 | Vendor */
    if(field==1)return e->cls; /* Base class / subclass / prog IF */
    if(field==2)return e->bdf; /* bus<<16 | device<<8 | function */
    if(field==3)return e->bar0; /* read only, never enable bus mastering */
    if(field==4)return e->revision;
    if(field==5)return e->caps;
    /* Running driver != detected PCI device. Do not claim NVMe/xHCI/
     * NVIDIA GPU drivers until actual I/O support is implemented. */
    if(field==6){
        if((e->id&65535u)==0x10ECu&&(e->id>>16)==0x8139u)
            return net_present()?2u:1u; /* ACTIVE : supported-not-active */
        return 0u; /* no bound native driver */
    }
    return 0;
}

static u32 in32(u16 port){
    u32 v;
    __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(port));
    return v;
}
static void out32(u16 port,u32 value){
    __asm__ volatile("outl %0,%1"::"a"(value),"Nd"(port));
}
static u32 config(u32 bus,u32 dev,u32 fn,u32 off){
    out32(0xCF8,0x80000000u|(bus<<16)|(dev<<11)|(fn<<8)|(off&0xFCu));
    return in32(0xCFC);
}
/* PCI conventional capability walker. Pure config-space reads, hard
 * bounded against invalid pointers and cyclic next links. Bits:
 * 1=power-management, 2=MSI, 4=PCIe, 8=MSI-X. Detection only. */
static u32 cap_inventory(u32 bus,u32 dev,u32 fn){
    u32 status_command=config(bus,dev,fn,0x04);
    if(!(status_command&0x00100000u))return 0;
    u32 ptr=config(bus,dev,fn,0x34)&0xFFu;
    u32 seen[8]={0};u32 mask=0;
    for(u32 budget=0;budget<32;budget++){
        ptr&=0xFCu;
        if(ptr<0x40u||ptr>0xFCu)break;
        u32 slot=ptr>>2;
        if(seen[slot>>5]&(1u<<(slot&31)))break;
        seen[slot>>5]|=1u<<(slot&31);
        u32 header=config(bus,dev,fn,ptr);
        u32 id=header&255u;
        if(id==0x01u)mask|=1u;
        else if(id==0x05u)mask|=2u;
        else if(id==0x10u)mask|=4u;
        else if(id==0x11u)mask|=8u;
        ptr=(header>>8)&255u;
    }
    return mask;
}
static u64 memory_bar(u32 bus,u32 dev,u32 fn){
    u32 low=config(bus,dev,fn,0x10);
    if(!low||low==0xFFFFFFFFu||(low&1u))return 0;
    u64 address=(u64)(low&~15u);
    if((low&6u)==4u)address|=(u64)config(bus,dev,fn,0x14)<<32;
    return address;
}
static bool mapped_reg(u64 base,u32 off){
    if(base<0x100000ull || base>=0x100000000ull)return false;
    if(base+(u64)off+4u>0x100000000ull)return false;
    if((base&3u)!=0)return false;
    return true;
}
static u32 mmio_read32(u64 base,u32 off){
    volatile u32 *p=(volatile u32 *)(uintptr_t)(base+(u64)off);
    return *p;
}
static void fill(pci_capability_t *out,u32 bus,u32 dev,u32 fn,u32 identity,u32 cls){
    out->vendor=(u16)identity;
    out->device=(u16)(identity>>16);
    out->bus=(u8)bus;out->dev=(u8)dev;out->fun=(u8)fn;
    out->revision=(u8)cls;
    out->class_code=cls>>8;
    out->bar0=memory_bar(bus,dev,fn);
    out->mmio_readable=false;
}
void pci_extended_probe(void){
    k_memset(&nvme,0,sizeof nvme);
    k_memset(&usb3,0,sizeof usb3);
    k_memset(&gpu,0,sizeof gpu);
    found_nvme=found_usb3=found_gpu=0;
    inventory_used=0;
    for(u32 bus=0;bus<256;bus++)for(u32 dev=0;dev<32;dev++){
        u32 head=config(bus,dev,0,0x0C);
        for(u32 fn=0;fn<8;fn++){
            if(fn && !((head>>16)&0x80u))break;
            u32 identity=config(bus,dev,fn,0);
            if((identity&65535u)==65535u || identity==0)continue;
            u32 cls=config(bus,dev,fn,8);
            u32 category=cls>>8;
            if(inventory_used<PCI_INVENTORY_LIMIT){
                pci_inventory_entry_t *entry=&inventory[inventory_used++];
                entry->id=identity;
                entry->cls=category;
                entry->bdf=(bus<<16)|(dev<<8)|fn;
                entry->revision=cls&255u;
                entry->bar0=config(bus,dev,fn,0x10);
                entry->caps=cap_inventory(bus,dev,fn);
            }
            pci_capability_t *target=NULL;
            if(category==0x010802u){
                if(found_nvme++==0)target=&nvme;
            }else if(category==0x0C0330u){
                if(found_usb3++==0)target=&usb3;
            }else if((category>>16)==3){
                if(found_gpu++==0)target=&gpu;
            }
            if(!target)continue;
            fill(target,bus,dev,fn,identity,cls);
            /* These are read-only, safe fixed hardware register offsets.
             * Do not enable a controller, bus master or map DMA yet. */
            if(target==&nvme && mapped_reg(target->bar0,0x20)){
                target->cap0=mmio_read32(target->bar0,0);     /* CAP low */
                target->cap1=mmio_read32(target->bar0,8);     /* VS */
                target->cap2=mmio_read32(target->bar0,0x1C);  /* CSTS */
                target->mmio_readable=true;
            }else if(target==&usb3 && mapped_reg(target->bar0,0x20)){
                target->cap0=mmio_read32(target->bar0,0);     /* CAPLENGTH/HCI */
                target->cap1=mmio_read32(target->bar0,4);     /* HCSPARAMS1 */
                target->cap2=mmio_read32(target->bar0,0x10);  /* HCCPARAMS1 */
                target->mmio_readable=true;
            }
        }
    }
}
u32 pci_extended_count(u32 kind){
    return kind==1?found_nvme:kind==2?found_usb3:kind==3?found_gpu:0u;
}
bool pci_extended_mmio(u32 kind){
    const pci_capability_t *c=kind==1?&nvme:kind==2?&usb3:&gpu;
    return c->mmio_readable;
}
u32 pci_extended_info(u32 kind,u32 field){
    const pci_capability_t *c=kind==1?&nvme:kind==2?&usb3:&gpu;
    if(field==0)return (u32)c->vendor|((u32)c->device<<16);
    if(field==1)return c->class_code;
    if(field==2)return (u32)c->bar0;
    if(field==3)return c->cap0;
    if(field==4)return c->cap1;
    if(field==5)return c->cap2;
    if(field==6)return ((u32)c->bus<<16)|((u32)c->dev<<8)|c->fun;
    return 0;
}
