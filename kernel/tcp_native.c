/* FalconOS native TCP/IPv4: single, polled, bounded client connection.
 *
 * Implements SYN/ACK 3-way handshake, checksums, in-order data, window,
 * ACK, FIN/RST, retransmission of SYN and data, timeouts. It is intentionally
 * not yet a general POSIX socket layer (no SACK, reassembly, simultaneous
 * sessions, PMTU or congestion-control beyond stop-and-wait).
 *
 * No TLS on this interface: never use plain TCP to fetch trusted packages.
 */
#include "falcon.h"

extern bool native_net_ipv4_send(const u8 remote[4],u8 protocol,
                                 const u8 *payload,u16 bytes);
extern void native_net_local_ipv4(u8 out[4]);

#define TCP_RX_LIMIT 4096u
#define TCP_MSS 1000u
#define TCP_LOCAL_PORT 43001u
#define TCP_TIMEOUT 120u
#define TCP_RETRIES 3u
#define TCP_FIN 0x01u
#define TCP_SYN 0x02u
#define TCP_RST 0x04u
#define TCP_PSH 0x08u
#define TCP_ACK 0x10u
typedef enum {TCP_CLOSED,TCP_SYN_SENT,TCP_ESTABLISHED,
              TCP_CLOSE_WAIT,TCP_ERROR} tcp_phase_t;
typedef struct {
    tcp_phase_t phase;
    u8 remote[4];
    u16 port;
    u32 snd_una,snd_nxt,rcv_nxt;
    u16 peer_window;
    u8 rx[TCP_RX_LIMIT];
    u32 used;
    bool peer_fin;
} tcp_conn_t;
static tcp_conn_t C;
static u16 get16(const u8 *p){return (u16)(((u16)p[0]<<8)|p[1]);}
static u32 get32(const u8 *p){return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3];}
static void put16(u8 *p,u16 n){p[0]=(u8)(n>>8);p[1]=(u8)n;}
static void put32(u8 *p,u32 n){p[0]=(u8)(n>>24);p[1]=(u8)(n>>16);p[2]=(u8)(n>>8);p[3]=(u8)n;}
static bool same_ip(const u8 *a,const u8 *b){
    for(i32 i=0;i<4;i++)if(a[i]!=b[i])return false;
    return true;
}
static u32 sum_words(u32 total,const u8 *p,u32 bytes){
    u32 i=0;
    while(i+1<bytes){total+=((u32)p[i]<<8)|p[i+1];i+=2;}
    if(i<bytes)total+=(u32)p[i]<<8;
    return total;
}
static u16 finalize(u32 total){
    while(total>>16)total=(total&0xFFFFu)+(total>>16);
    return (u16)~total;
}
static u16 tcp_checksum(const u8 source[4],const u8 dest[4],
                        const u8 *segment,u16 bytes){
    u32 sum=sum_words(0,source,4);
    sum=sum_words(sum,dest,4);
    sum+=6u+bytes;
    return finalize(sum_words(sum,segment,bytes));
}
static bool emit(u32 sequence,u8 flags,const u8 *data,u16 count){
    if(count>TCP_MSS)return false;
    static u8 segment[20+TCP_MSS];
    k_memset(segment,0,(u32)20+count);
    put16(segment,TCP_LOCAL_PORT);put16(segment+2,C.port);
    put32(segment+4,sequence);
    if(flags&TCP_ACK)put32(segment+8,C.rcv_nxt);
    segment[12]=0x50;
    segment[13]=flags;
    put16(segment+14,(u16)(TCP_RX_LIMIT-C.used));
    if(count&&data)k_memcpy(segment+20,data,count);
    u8 source[4];native_net_local_ipv4(source);
    put16(segment+16,tcp_checksum(source,C.remote,segment,(u16)(20u+count)));
    return native_net_ipv4_send(C.remote,6,segment,(u16)(20u+count));
}
/* RX is invoked from RTL8139 polling, synchronous with the caller. */
void native_tcp_receive(const u8 *ip,u32 total) {
    if(C.phase==TCP_CLOSED||C.phase==TCP_ERROR||!ip||total<40)return;
    u32 ihl=(u32)(ip[0]&15u)*4u;
    if(ihl<20 || total<ihl+20 || !same_ip(ip+12,C.remote))return;
    const u8 *segment=ip+ihl;
    u16 bytes=(u16)(total-ihl);
    u32 header=(u32)(segment[12]>>4)*4u;
    if(header<20||header>60||header>bytes||
       get16(segment)!=C.port||get16(segment+2)!=TCP_LOCAL_PORT)return;
    u8 local[4];native_net_local_ipv4(local);
    if(tcp_checksum(C.remote,local,segment,bytes)!=0)return;
    u8 flags=segment[13];
    u32 sequence=get32(segment+4),acked=get32(segment+8);
    if(flags&TCP_RST){C.phase=TCP_ERROR;return;}
    if(C.phase==TCP_SYN_SENT){
        if((flags&(TCP_SYN|TCP_ACK))!=(TCP_SYN|TCP_ACK) ||
           acked!=C.snd_nxt)return;
        C.snd_una=acked;
        C.rcv_nxt=sequence+1;
        C.peer_window=get16(segment+14);
        C.phase=TCP_ESTABLISHED;
        (void)emit(C.snd_nxt,TCP_ACK,NULL,0);
        return;
    }
    if(flags&TCP_ACK && (i32)(acked-C.snd_una)>=0 &&
       (i32)(C.snd_nxt-acked)>=0)C.snd_una=acked;
    if(sequence!=C.rcv_nxt) {
        /* Reject unordered/duplicate payload instead of corrupting stream. */
        if(bytes>header || (flags&TCP_FIN)) (void)emit(C.snd_nxt,TCP_ACK,NULL,0);
        return;
    }
    u32 payload=bytes-header;
    if(payload){
        if(payload>TCP_RX_LIMIT-C.used){
            (void)emit(C.snd_nxt,TCP_ACK,NULL,0); /* advertise zero window */
            return;
        }
        k_memcpy(C.rx+C.used,segment+header,payload);
        C.used+=payload;
        C.rcv_nxt+=payload;
        (void)emit(C.snd_nxt,TCP_ACK,NULL,0);
    }
    if(flags&TCP_FIN){
        C.rcv_nxt++;
        C.peer_fin=true;
        C.phase=TCP_CLOSE_WAIT;
        (void)emit(C.snd_nxt,TCP_ACK,NULL,0);
    }
}
static bool wait_until_ack(u32 target,u32 deadline_ticks) {
    u32 start=g_ticks;
    while((i32)(C.snd_una-target)<0 && g_ticks-start<deadline_ticks) {
        if(C.phase==TCP_ERROR)return false;
        native_net_poll();
    }
    return (i32)(C.snd_una-target)>=0;
}
bool native_tcp_connect(const u8 remote[4],u16 port){
    if(!remote||!port||!net_present())return false;
    k_memset(&C,0,sizeof C);
    k_memcpy(C.remote,remote,4);
    C.port=port;C.phase=TCP_SYN_SENT;
    C.snd_nxt=(u32)rdtsc();
    C.snd_una=C.snd_nxt;
    /* Prevent a zero or repeatable fixed initial SYN sequence. */
    C.snd_nxt^=g_ticks*0x9E3779B9u;
    C.snd_una=C.snd_nxt;
    u32 first=C.snd_nxt;
    C.snd_nxt++;
    for(u32 retry=0;retry<TCP_RETRIES;retry++){
        if(!emit(first,TCP_SYN,NULL,0))continue;
        u32 start=g_ticks;
        while(C.phase==TCP_SYN_SENT && g_ticks-start<TCP_TIMEOUT)
            native_net_poll();
        if(C.phase==TCP_ESTABLISHED)return true;
        if(C.phase==TCP_ERROR)break;
    }
    C.phase=TCP_CLOSED;
    return false;
}
i32 native_tcp_write(const u8 *data,u32 length){
    if(C.phase!=TCP_ESTABLISHED||(!data&&length))return -1;
    u32 sent=0;
    while(sent<length){
        u32 remain=length-sent;
        u16 size=(u16)(remain>TCP_MSS?TCP_MSS:remain);
        if(C.peer_window && size>C.peer_window)size=C.peer_window;
        if(!size)return sent ? (i32)sent : -1;
        u32 sequence=C.snd_nxt;
        u32 target=sequence+size;
        C.snd_nxt=target;
        bool acked=false;
        for(u32 retry=0;retry<TCP_RETRIES;retry++){
            if(!emit(sequence,TCP_ACK|TCP_PSH,data+sent,size))continue;
            if(wait_until_ack(target,TCP_TIMEOUT)){acked=true;break;}
        }
        if(!acked){C.phase=TCP_ERROR;return -1;}
        sent+=size;
    }
    return (i32)sent;
}
i32 native_tcp_read(u8 *out,u32 cap,u32 timeout_ticks){
    if(!out||!cap||C.phase==TCP_CLOSED||C.phase==TCP_ERROR)return -1;
    u32 started=g_ticks;
    while(!C.used && !C.peer_fin && g_ticks-started<timeout_ticks)
        native_net_poll();
    if(!C.used)return C.peer_fin?0:-1;
    u32 bytes=C.used<cap?C.used:cap;
    k_memcpy(out,C.rx,bytes);
    for(u32 i=bytes;i<C.used;i++)C.rx[i-bytes]=C.rx[i];
    C.used-=bytes;
    /* Reopen the receiver window after the consumer drained buffered data. */
    (void)emit(C.snd_nxt,TCP_ACK,NULL,0);
    return (i32)bytes;
}
void native_tcp_close(void){
    if(C.phase==TCP_ESTABLISHED||C.phase==TCP_CLOSE_WAIT) {
        /* Best effort FIN; no unbounded waiting during shutdown. */
        (void)emit(C.snd_nxt,TCP_FIN|TCP_ACK,NULL,0);
        C.snd_nxt++;
    }
    C.phase=TCP_CLOSED;
}
i32 native_tcp_state(void){return (i32)C.phase;}
