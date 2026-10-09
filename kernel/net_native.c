/* Ethernet/ARP/IPv4/ICMP stack (QEMU rtl8139 + user-net static address).
 * Unlike the previous placeholder, an echo reply must be received to pass.
 * TCP, DHCP and TLS are intentionally NOT implemented by this module.
 */
#include "falcon.h"
extern bool rtl8139_init(void);
extern bool rtl8139_ready(void);
extern const u8 *rtl8139_mac(void);
extern bool rtl8139_send(const u8 *,u32);
extern void rtl8139_poll(void (*)(const u8 *,u32));

static u8 guest_ip[4]={10,0,2,15};
static u8 guest_mask[4]={255,255,255,0};
static u8 gateway_ip[4]={10,0,2,2};
static u8 peer_mac[6];
static u8 peer_ip[4];
static bool peer_known,echo_seen;
static u16 echo_sequence;
static u32 packets_seen;

static u16 rd16(const u8 *p){return (u16)(((u16)p[0]<<8)|p[1]);}
static void wr16(u8 *p,u16 n){p[0]=(u8)(n>>8);p[1]=(u8)n;}
static bool equal(const u8 *a,const u8 *b,u32 n){
    for(u32 i=0;i<n;i++)if(a[i]!=b[i])return false;
    return true;
}
static u16 checksum(const u8 *p,u32 n){
    u32 sum=0;
    for(u32 i=0;i+1<n;i+=2)sum+=(u16)(((u16)p[i]<<8)|p[i+1]);
    if(n&1)sum+=(u16)((u16)p[n-1]<<8);
    while(sum>>16)sum=(sum&0xFFFFu)+(sum>>16);
    return (u16)~sum;
}
static void ethernet(u8 *out,const u8 *dest,u16 protocol){
    k_memcpy(out,dest,6);
    k_memcpy(out+6,rtl8139_mac(),6);
    wr16(out+12,protocol);
}
static void arp_request(const u8 target[4]){
    u8 p[60];
    k_memset(p,0,sizeof p);
    for(i32 i=0;i<6;i++)p[i]=0xFF;
    ethernet(p,p,0x0806); /* broadcast destination is in first six bytes */
    wr16(p+14,1);wr16(p+16,0x0800);
    p[18]=6;p[19]=4;wr16(p+20,1);
    k_memcpy(p+22,rtl8139_mac(),6);
    k_memcpy(p+28,guest_ip,4);
    k_memcpy(p+38,target,4);
    (void)rtl8139_send(p,60);
}
static void arp_reply(const u8 incoming[42]){
    u8 p[60];k_memset(p,0,sizeof p);
    ethernet(p,incoming+22,0x0806);
    wr16(p+14,1);wr16(p+16,0x0800);
    p[18]=6;p[19]=4;wr16(p+20,2);
    k_memcpy(p+22,rtl8139_mac(),6);
    k_memcpy(p+28,guest_ip,4);
    k_memcpy(p+32,incoming+22,6);
    k_memcpy(p+38,incoming+28,4);
    (void)rtl8139_send(p,60);
}
static void native_receive(const u8 *frame,u32 n){
    if(n<14)return;
    packets_seen++;
    u16 type=rd16(frame+12);
    if(type==0x0806 && n>=42 && rd16(frame+14)==1 &&
       rd16(frame+16)==0x0800 && frame[18]==6 && frame[19]==4 &&
       equal(frame+38,guest_ip,4)){
        u16 op=rd16(frame+20);
        if(op==2){
            k_memcpy(peer_mac,frame+22,6);
            k_memcpy(peer_ip,frame+28,4);
            peer_known=true;
        }else if(op==1)arp_reply(frame);
        return;
    }
    if(type!=0x0800 || n<14+20)return;
    const u8 *ip=frame+14;
    u32 ihl=(u32)(ip[0]&15u)*4u;
    if((ip[0]>>4)!=4 || ihl<20 || ihl>60 || n<14+ihl)return;
    u16 total=rd16(ip+2);
    if(total<ihl||total>n-14 || checksum(ip,ihl)!=0)return;
    if(!equal(ip+16,guest_ip,4))return;
    /* No fragmented IP reassembly in this first native-stack milestone. */
    if(rd16(ip+6)&0x3FFFu)return;
    if(ip[9]==1 && total>=ihl+8){
        const u8 *icmp=ip+ihl;
        u32 bytes=total-ihl;
        if(checksum(icmp,bytes)!=0)return;
        if(icmp[0]==0 && icmp[1]==0 && rd16(icmp+4)==0xFA1Cu &&
           rd16(icmp+6)==echo_sequence)echo_seen=true;
    }
}
bool native_net_init(void){return rtl8139_init();}
void native_net_poll(void){if(rtl8139_ready())rtl8139_poll(native_receive);}
void native_net_config(u8 ip[4],u8 mask[4],u8 gw[4]){
    k_memcpy(guest_ip,ip,4);k_memcpy(guest_mask,mask,4);k_memcpy(gateway_ip,gw,4);
    peer_known=false;
}
bool native_net_arp_known(void){return peer_known;}
void native_net_arp_mac(u8 out[6]){if(peer_known)k_memcpy(out,peer_mac,6);}
u32 native_net_rx_count(void){return packets_seen;}
bool native_net_ping(const u8 ip[4]){
    if(!rtl8139_ready())return false;
    /* All addresses outside subnet are resolved through gateway. */
    const u8 *nexthop=ip;
    for(i32 i=0;i<4;i++)if((ip[i]&guest_mask[i])!=(guest_ip[i]&guest_mask[i])){
        nexthop=gateway_ip;break;
    }
    if(!peer_known || !equal(peer_ip,nexthop,4)){
        peer_known=false;
        arp_request(nexthop);
#ifdef FALCON_QEMU_NET_TEST
        outb(0xE9,'a');
#endif
        u32 start=g_ticks;
        while(!peer_known && g_ticks-start<150u)native_net_poll();
        if(!peer_known)return false;
#ifdef FALCON_QEMU_NET_TEST
        outb(0xE9,'A');
#endif
    }
    u8 packet[14+20+16];
    k_memset(packet,0,sizeof packet);
    ethernet(packet,peer_mac,0x0800);
    u8 *v=packet+14;
    v[0]=0x45;wr16(v+2,36);wr16(v+4,0xF01Cu);
    wr16(v+6,0x4000);v[8]=64;v[9]=1;
    k_memcpy(v+12,guest_ip,4);k_memcpy(v+16,ip,4);
    wr16(v+10,checksum(v,20));
    u8 *ic=v+20;ic[0]=8;ic[1]=0;
    wr16(ic+4,0xFA1C);echo_sequence++;
    wr16(ic+6,echo_sequence);
    ic[8]='F';ic[9]='a';ic[10]='l';ic[11]='c';
    ic[12]='o';ic[13]='n';ic[14]='O';ic[15]='S';
    wr16(ic+2,checksum(ic,16));
    echo_seen=false;
    if(!rtl8139_send(packet,sizeof packet))return false;
#ifdef FALCON_QEMU_NET_TEST
    outb(0xE9,'t');
#endif
    u32 start=g_ticks;
    while(!echo_seen && g_ticks-start<200u)native_net_poll();
    if(echo_seen) {
#ifdef FALCON_QEMU_NET_TEST
        outb(0xE9,'E');
#endif
    }
    return echo_seen;
}
bool native_net_parse_ipv4(const char *text,u8 out[4]){
    if(!text||!out)return false;
    i32 index=0;
    for(i32 part=0;part<4;part++){
        if(text[index]<'0'||text[index]>'9')return false;
        u32 val=0,digits=0;
        while(text[index]>='0'&&text[index]<='9'){
            if(++digits>3)return false;
            val=val*10u+(u32)(text[index++]-'0');
        }
        if(val>255)return false;
        out[part]=(u8)val;
        if(part<3){if(text[index++]!='.')return false;}
    }
    return text[index]==0;
}
