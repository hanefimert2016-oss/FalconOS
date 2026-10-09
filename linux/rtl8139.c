/* Native RTL8139 Ethernet driver for QEMU (-device rtl8139).
 * The previous virtio_net.c only displayed simulated addresses, not packets.
 * This driver uses PCI I/O BAR0 and polling, with identity-mapped DMA buffers.
 * QEMU mode only: no MSI, no physical laptop NIC support.
 */
#include "falcon.h"
#define RX_SIZE 8192u
#define RX_ALLOC (RX_SIZE+16u+2048u)
static u8 rx_mem[RX_ALLOC] __attribute__((aligned(256)));
static u8 tx_mem[4][2048] __attribute__((aligned(256)));
static u16 io_base, rx_position;
static u8 mac[6];
static u32 tx_count,rx_count,tx_bytes,rx_bytes,tx_errors,rx_errors,tx_next;
static bool ready;
static u32 inl32(u16 p){u32 a;__asm__ volatile("inl %1,%0":"=a"(a):"Nd"(p));return a;}
static void outl32(u16 p,u32 a){__asm__ volatile("outl %0,%1"::"a"(a),"Nd"(p));}
static u32 pci_read(u32 a){
    outl32(0xCF8,0x80000000u|(a&~3u));
    return inl32(0xCFC);
}
static void pci_write(u32 a,u32 v){
    outl32(0xCF8,0x80000000u|(a&~3u));
    outl32(0xCFC,v);
}
bool rtl8139_init(void){
    if(ready) return true;
    for(u32 bus=0;bus<256u;bus++) for(u32 dev=0;dev<32u;dev++){
        u32 a=(bus<<16)|(dev<<11);
        u32 ident=pci_read(a);
        if(ident!=0x813910ECu)continue;
        u32 bar=pci_read(a+0x10);
        if(!(bar&1u) || (bar&~3u)>0xFFFFu)continue;
        io_base=(u16)(bar&~3u);
        if(io_base<0x100)continue;
        u32 command=pci_read(a+4);
        pci_write(a+4,command|0x5u); /* PCI I/O enable + bus mastering */
        outb(io_base+0x52,0);
        outb(io_base+0x37,0x10); /* software reset */
        u32 guard=1000000;
        while((inb(io_base+0x37)&0x10u) && --guard){}
        if(!guard)return false;
        for(i32 i=0;i<6;i++)mac[i]=inb((u16)(io_base+i));
        /* These buffers reside in kernel BSS identity-mapped below 4 GiB. */
        if(((u64)(uintptr_t)rx_mem)>>32)return false;
        for(i32 i=0;i<4;i++)if(((u64)(uintptr_t)tx_mem[i])>>32)return false;
        rx_position=0;tx_next=0;
        outl32(io_base+0x30,(u32)(uintptr_t)rx_mem);
        outw(io_base+0x3C,0); /* poll ISR; no IRQ handler needed */
        outl32(io_base+0x44,0x0000000Eu); /* physical, broadcast, multicast */
        outl32(io_base+0x40,0x03000700u); /* recommended TX DMA threshold */
        outb(io_base+0x37,0x0Cu); /* RX + TX enable */
        outw(io_base+0x38,0);
        ready=true;
        return true;
    }
    return false;
}
bool rtl8139_ready(void){return ready;}
const u8 *rtl8139_mac(void){return mac;}
bool rtl8139_send(const u8 *payload,u32 length){
    if(!ready||!payload||length<14||length>1514)return false;
    u32 slot=tx_next&3u;
    u32 status=inl32((u16)(io_base+0x10+slot*4u));
    if(!(status&0x2000u)) {tx_errors++;return false;} /* buffer still busy */
    k_memcpy(tx_mem[slot],payload,length);
    if(length<60){k_memset(tx_mem[slot]+length,0,60u-length);length=60;}
    outl32((u16)(io_base+0x20+slot*4u),(u32)(uintptr_t)tx_mem[slot]);
    outl32((u16)(io_base+0x10+slot*4u),length);
    tx_next++;tx_count++;tx_bytes+=length;
    return true;
}
static u8 rx_byte(u32 offset){return rx_mem[offset%RX_SIZE];}
static u16 rx_word(u32 offset){return (u16)(rx_byte(offset)|((u16)rx_byte(offset+1)<<8));}
void rtl8139_poll(void (*consume)(const u8*,u32)){
    if(!ready)return;
    static u8 packet[1514];
    for(u32 budget=0;budget<16 && !(inb(io_base+0x37)&1u);budget++){
        u16 status=rx_word(rx_position);
        u16 length=rx_word(rx_position+2);
        if(length<18||length>1518||!(status&1u)){
            rx_errors++;
            outb(io_base+0x37,0x10);
            for(u32 guard=100000;inb(io_base+0x37)&0x10u && guard;guard--){}
            outl32(io_base+0x30,(u32)(uintptr_t)rx_mem);
            rx_position=0;
            outb(io_base+0x37,0x0Cu);
            break;
        }
        u32 copied=(u32)length-4u; /* strip Ethernet CRC */
        if(copied<=sizeof packet){
            for(u32 j=0;j<copied;j++)packet[j]=rx_byte(rx_position+4u+j);
            rx_count++;rx_bytes+=copied;
            if(consume)consume(packet,copied);
        } else rx_errors++;
        rx_position=(u16)((rx_position+(u32)length+4u+3u)&~3u);
        rx_position=(u16)(rx_position%RX_SIZE);
        outw(io_base+0x38,(u16)(rx_position-16u));
        outw(io_base+0x3E,0xFFFFu); /* acknowledge ISR */
    }
}
void rtl8139_stats(u32 *tx,u32 *rx,u32 *txb,u32 *rxb,u32 *txe,u32 *rxe){
    if(tx)*tx=tx_count;if(rx)*rx=rx_count;
    if(txb)*txb=tx_bytes;if(rxb)*rxb=rx_bytes;
    if(txe)*txe=tx_errors;if(rxe)*rxe=rx_errors;
}

#ifdef FALCON_QEMU_NET_TEST
static void debug_hex(u8 n) {
    outb(0xE9,(u8)"0123456789ABCDEF"[n>>4]);
    outb(0xE9,(u8)"0123456789ABCDEF"[n&15]);
}
void rtl8139_debug_dump(void) {
    outb(0xE9,'[');
    debug_hex(inb(io_base+0x37)); /* CR */
    debug_hex((u8)inw(io_base+0x3E)); /* ISR */
    debug_hex((u8)inw(io_base+0x3A)); /* CBR */
    debug_hex((u8)inw(io_base+0x38)); /* CAPR */
    debug_hex(rx_mem[0]);debug_hex(rx_mem[1]);
    debug_hex(rx_mem[2]);debug_hex(rx_mem[3]);
    outb(0xE9,']');
}
#endif
