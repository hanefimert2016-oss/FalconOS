/* FalconOS DHCPv4 client: bounded DISCOVER/OFFER/REQUEST/ACK.
 * Uses the real RTL8139 Ethernet broadcast path; leases validated against
 * transaction ID, MAC, server ID and requested address. Replaces the fake
 * DHCP "success" of the former virtio placeholder, no bridge involved.
 *
 * No lease renewal / persistence yet. A lease is not a production DHCP
 * implementation until T1/T2 timers and link changes are handled.
 */
#include "falcon.h"
extern const u8 *rtl8139_mac(void);
extern bool native_net_dhcp_broadcast(const u8 *payload,u16 length);
extern void native_net_dns_server(const u8 ip[4]);
extern void native_net_config(u8 ip[4],u8 mask[4],u8 gw[4]);

#define BOOTP_FIXED 240u
#define DHCP_MAGIC 0x63825363u
static u32 tx_id;
static u8 received_type;
static u8 server_id[4],offered_ip[4];
static u8 offer_mask[4],offer_router[4],offer_dns[4];
static bool in_progress;

static u32 be32(const u8 *p){
    return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3];
}
static void put32(u8 *p,u32 n){
    p[0]=(u8)(n>>24);p[1]=(u8)(n>>16);p[2]=(u8)(n>>8);p[3]=(u8)n;
}
static bool eq(const u8 *a,const u8 *b,u32 n){
    for(u32 i=0;i<n;i++)if(a[i]!=b[i])return false;
    return true;
}
static bool valid_ip(const u8 *p){
    return p[0]!=0 && p[0]!=127 && p[0]!=224 && p[0]!=255 &&
           !(p[0]==169 && p[1]==254);
}
static bool nonzero(const u8 *p){
    return p[0]||p[1]||p[2]||p[3];
}
static void send_message(u8 code){
    static u8 req[320];
    k_memset(req,0,sizeof req);
    req[0]=1;req[1]=1;req[2]=6; /* BOOTREQUEST Ethernet MAC-48 */
    put32(req+4,tx_id);
    req[10]=0x80;               /* broadcast flag */
    k_memcpy(req+28,rtl8139_mac(),6);
    put32(req+236,DHCP_MAGIC);
    u32 i=240;
    req[i++]=53;req[i++]=1;req[i++]=code;
    if(code==3){
        req[i++]=50;req[i++]=4;
        k_memcpy(req+i,offered_ip,4);i+=4;
        req[i++]=54;req[i++]=4;
        k_memcpy(req+i,server_id,4);i+=4;
    }
    req[i++]=55;req[i++]=3;req[i++]=1;req[i++]=3;req[i++]=6;
    req[i++]=61;req[i++]=7;req[i++]=1;
    k_memcpy(req+i,rtl8139_mac(),6);i+=6;
    req[i++]=255;
    if(i<300)i=300;
    (void)native_net_dhcp_broadcast(req,(u16)i);
}
/* Invoked only for UDP source 67, dest 68, after IPv4 bounds/checksum.
 * Returns no decision to the generic network stack.
 */
void native_dhcp_receive(const u8 *payload,u32 length){
    if(!in_progress||!payload||length<BOOTP_FIXED+4u||length>1500u ||
       payload[0]!=2||payload[1]!=1||payload[2]!=6||
       be32(payload+4)!=tx_id||
       !eq(payload+28,rtl8139_mac(),6) ||
       be32(payload+236)!=DHCP_MAGIC ||
       !valid_ip(payload+16))return;
    u8 type=0,srv[4]={0},mask[4]={0},router[4]={0},dns[4]={0};
    for(u32 pos=BOOTP_FIXED;pos<length;){
        u8 key=payload[pos++];
        if(key==255)break;
        if(key==0)continue;
        if(pos>=length)return;
        u32 n=payload[pos++];
        if(n>length-pos)return;
        if(key==53 && n==1)type=payload[pos];
        if(key==54 && n==4)k_memcpy(srv,payload+pos,4);
        if(key==1 && n==4)k_memcpy(mask,payload+pos,4);
        if(key==3 && n>=4)k_memcpy(router,payload+pos,4);
        if(key==6 && n>=4)k_memcpy(dns,payload+pos,4);
        pos+=n;
    }
    if(!nonzero(srv) || !valid_ip(srv))return;
    if(type==2 && received_type==0){
        if(!nonzero(mask)||!nonzero(router)||!nonzero(dns))return;
        k_memcpy(offered_ip,payload+16,4);
        k_memcpy(server_id,srv,4);
        k_memcpy(offer_mask,mask,4);
        k_memcpy(offer_router,router,4);
        k_memcpy(offer_dns,dns,4);
        received_type=2;
    }else if(type==5 && received_type==3 &&
              eq(srv,server_id,4) &&
              eq(payload+16,offered_ip,4)) {
        /* ACK must preserve subnet/gateway/DNS chosen in trusted offer. */
        if(nonzero(mask))k_memcpy(offer_mask,mask,4);
        if(nonzero(router))k_memcpy(offer_router,router,4);
        if(nonzero(dns))k_memcpy(offer_dns,dns,4);
        received_type=5;
    }else if(type==6){
        received_type=6; /* NAK */
    }
}
bool native_dhcp_acquire(u8 ip[4],u8 mask[4],u8 router[4],u8 dns[4]){
    if(in_progress || !ip||!mask||!router||!dns)return false;
    tx_id=(u32)rdtsc()^g_ticks^0xF41C0C01u;
    if(!tx_id)tx_id=1;
    received_type=0;in_progress=true;
    bool done=false;
    for(u32 attempt=0;attempt<3 && received_type==0;attempt++){
        send_message(1); /* DHCP DISCOVER */
        u32 t=g_ticks;
        while(received_type==0 && g_ticks-t<180u)native_net_poll();
    }
    if(received_type==2){
        received_type=3;
        for(u32 attempt=0;attempt<3 && received_type==3;attempt++){
            send_message(3); /* DHCP REQUEST */
            u32 t=g_ticks;
            while(received_type==3 && g_ticks-t<180u)native_net_poll();
        }
        if(received_type==5){
            k_memcpy(ip,offered_ip,4);
            k_memcpy(mask,offer_mask,4);
            k_memcpy(router,offer_router,4);
            k_memcpy(dns,offer_dns,4);
            native_net_config(ip,mask,router);
            native_net_dns_server(dns);
            done=true;
        }
    }
    in_progress=false;
    return done;
}
