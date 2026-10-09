#include "falcon.h"
#include <stdio.h>
#include <string.h>
volatile u32 g_ticks;
u64 rdtsc(void){return 0x12345678ULL;}
void native_tcp_receive(const u8 *ip,u32 total){(void)ip;(void)total;}
static u8 last_tx[1600],mac[6]={0x52,0x54,0,0x12,0x34,0x56},peer[6]={0x52,0x54,0,0xab,0xcd,0xef};
static u32 tx_len; static bool drop;
void k_memcpy(void *a,const void *b,u32 n){memcpy(a,b,n);}
void k_memset(void *a,u8 v,u32 n){memset(a,v,n);}
bool rtl8139_init(void){return true;}
bool rtl8139_ready(void){return true;}
const u8 *rtl8139_mac(void){return mac;}
bool rtl8139_send(const u8 *frame,u32 length){
 if(length>sizeof last_tx)return false;
 memcpy(last_tx,frame,length);tx_len=length;return true;
}
static void wr16(u8 *p,u16 n){p[0]=(u8)(n>>8);p[1]=(u8)n;}
static u16 checksum(const u8 *p,u32 n){
 u32 s=0;for(u32 i=0;i+1<n;i+=2)s+=((u32)p[i]<<8)|p[i+1];
 if(n&1)s+=(u32)p[n-1]<<8;while(s>>16)s=(s&65535u)+(s>>16);return (u16)~s;
}
void rtl8139_poll(void(*consume)(const u8 *,u32)){
 g_ticks++;
 if(drop || tx_len<14)return;
 static u8 response[80];memset(response,0,sizeof response);
 memcpy(response,mac,6);memcpy(response+6,peer,6);
 if(last_tx[12]==8 && last_tx[13]==6){
   wr16(response+12,0x0806);wr16(response+14,1);wr16(response+16,0x0800);
   response[18]=6;response[19]=4;wr16(response+20,2);
   memcpy(response+22,peer,6);
   response[28]=10;response[29]=0;response[30]=2;response[31]=2;
   memcpy(response+32,mac,6);
   response[38]=10;response[39]=0;response[40]=2;response[41]=15;
   consume(response,42);
 } else if(last_tx[12]==8 && last_tx[13]==0){
   memcpy(response+12,last_tx+12,2);
   memcpy(response+14,last_tx+14,36);
   u8 *ip=response+14,*icmp=ip+20;
   memcpy(ip+12,last_tx+14+16,4);
   memcpy(ip+16,last_tx+14+12,4);
   ip[10]=ip[11]=0;wr16(ip+10,checksum(ip,20));
   icmp[0]=0;icmp[2]=icmp[3]=0;wr16(icmp+2,checksum(icmp,16));
   consume(response,50);
 }
 tx_len=0;
}
#define CHECK(cond,n) do{if(!(cond)){fprintf(stderr,"net test failed %d\n",__LINE__);return n;}}while(0)
int main(void){
 u8 ip[4]={10,0,2,2},parsed[4];
 CHECK(native_net_parse_ipv4("10.0.2.2",parsed),1);
 CHECK(!memcmp(ip,parsed,4),2);
 CHECK(!native_net_parse_ipv4("999.1.2.3",parsed),3);
 CHECK(!native_net_parse_ipv4("1.2.3.4junk",parsed),4);
 CHECK(native_net_ping(ip),5);
 CHECK(native_net_arp_known()&&native_net_rx_count()>=2,6);
 drop=true;CHECK(!native_net_ping(ip),7);
 static const u8 dns_sample[]={
 0x12,0x34,0x81,0x80,0,1,0,1,0,0,0,0,
 7,'e','x','a','m','p','l','e',
 3,'c','o','m',0,0,1,0,1,
 0xc0,0x0c,0,1,0,1,0,0,0,60,0,4,93,184,216,34};
 u8 resolved[4]={0};
 CHECK(native_net_dns_parse(dns_sample,sizeof dns_sample,0x1234,resolved),8);
 CHECK(resolved[0]==93 && resolved[1]==184 && resolved[2]==216 && resolved[3]==34,9);
 CHECK(!native_net_dns_parse(dns_sample,sizeof dns_sample,0x9999,resolved),10);
 CHECK(!native_net_dns_parse(dns_sample,sizeof dns_sample-3,0x1234,resolved),11);
 puts("PASS native ARP+ICMP+DNS wire-format checks and real reply timeout semantics");
 return 0;
}
