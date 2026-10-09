/* Optional guest-native HTTPS over BearSSL TLS 1.2 and FalconOS TCP.
 *
 * Fail closed unless:
 * - BearSSL is explicitly compiled and an audited CA bundle is embedded;
 * - CPU RDRAND provides 256 bits of entropy, else NO TLS attempt;
 * - RTC time is plausibly valid, and BearSSL validates X.509 + hostname;
 * - Complete Content-Length authenticated HTTP response fits output.
 *
 * Neither plaintext HTTP nor COM1 host bridge is an HTTPS substitute.
 */
#include "falcon.h"
#ifdef FALCON_BEARSSL
#include "bearssl.h"
/* No Linux /dev/urandom exists inside FalconOS. Randomness is injected
 * explicitly after CPU RDRAND health/capability checks, before TLS reset. */
br_prng_seeder br_prng_seeder_system(const char **name){
    if(name)*name="falcon-rdrand-explicit";
    return (br_prng_seeder)0;
}


extern const br_x509_trust_anchor falcon_tls_anchors[];
extern const size_t falcon_tls_anchor_count;
static br_ssl_client_context CLIENT;
static br_x509_minimal_context X509;
static br_sslio_context SSLIO;
static unsigned char IO_BUFFER[BR_SSL_BUFSIZE_BIDI];

static bool cpu_random(unsigned char out[32]){
    u32 eax=1,ebx,ecx,edx;
    __asm__ volatile("cpuid":"+a"(eax),"=b"(ebx),"=c"(ecx),"=d"(edx));
    if(!(ecx&(1u<<30)))return false; /* RDRAND unsupported: refuse TLS */
    for(u32 i=0;i<8;i++){
        u32 value=0;
        bool got=false;
        for(u32 attempt=0;attempt<16;attempt++){
            unsigned char carry;
            __asm__ volatile("rdrand %0; setc %1":"=r"(value),"=qm"(carry));
            if(carry){got=true;break;}
        }
        if(!got)return false;
        out[i*4]=(u8)value;
        out[i*4+1]=(u8)(value>>8);
        out[i*4+2]=(u8)(value>>16);
        out[i*4+3]=(u8)(value>>24);
    }
    return true;
}
static bool cert_time(u32 *day,u32 *sec){
    rtc_time_t t;rtc_now(&t);
    if(t.year<2024||t.year>2099||t.month<1||t.month>12||
       t.day<1||t.day>31||t.hour>23||t.min>59||t.sec>59)return false;
    u32 y=t.year;
    u32 days=365u*y+(y+3)/4-(y+99)/100+(y+399)/400;
    const u8 lengths[12]={31,28,31,30,31,30,31,31,30,31,30,31};
    for(u32 i=1;i<t.month;i++){
        days+=lengths[i-1];
        if(i==2 && (y%4==0&&(y%100!=0||y%400==0)))days++;
    }
    days+=(u32)t.day-1u;
    *day=days;
    *sec=(u32)t.hour*3600u+(u32)t.min*60u+t.sec;
    return true;
}
static int tcp_read_cb(void *ctx,unsigned char *dest,size_t len){
    (void)ctx;
    if(!len)return -1;
    u32 n=(u32)(len>4096?4096:len);
    int got=native_tcp_read(dest,n,1000u);
#if defined(FALCON_QEMU_TLS_TEST) || defined(FALCON_QEMU_TLS_PUBLIC_TEST)
    outb(0xE9,got>0?'r':got==0?'0':'!');
#endif
    return got;
}
static int tcp_write_cb(void *ctx,const unsigned char *data,size_t len){
    (void)ctx;
    if(!len)return -1;
    u32 n=(u32)(len>4096?4096:len);
    int written=native_tcp_write(data,n);
#if defined(FALCON_QEMU_TLS_TEST) || defined(FALCON_QEMU_TLS_PUBLIC_TEST)
    outb(0xE9,written>0?'w':'?');
#endif
    return written;
}
static bool valid_host(const char *host){
    if(!host)return false;
    u32 count=0;
    for(;host[count];count++){
        char c=host[count];
        if(count>=200 ||
           !((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
             (c>='0'&&c<='9')||c=='-'||c=='.'))return false;
    }
    return count>0 && host[0]!='.' && host[count-1]!='.';
}
static bool valid_path(const char *path) {
    if(!path || path[0]!='/')return false;
    for(u32 i=0;path[i];i++){
        if(i>=300 || path[i]<33 || path[i]>126)return false;
    }
    return true;
}
static bool secure_response_complete(const char *response,u32 n){
    /* Require Content-Length to prevent accepting a truncated TLS stream. */
    if(n<20 || response[0]!='H'||response[1]!='T'||
       response[2]!='T'||response[3]!='P'||response[8]!=' '||
       response[9]!='2')return false;
    u32 header=0;
    for(u32 i=0;i+3<n;i++){
        if(response[i]=='\r' && response[i+1]=='\n' &&
           response[i+2]=='\r' && response[i+3]=='\n'){
            header=i+4;break;
        }
    }
    if(!header)return false;
    u32 expected=0;
    bool found=false;
    for(u32 i=0;i+16<header;i++){
        const char *key="content-length:";
        bool equal=true;
        for(u32 k=0;k<15;k++){
            char c=response[i+k];
            if(c>='A'&&c<='Z')c=(char)(c+32);
            if(c!=key[k]){equal=false;break;}
        }
        if(!equal)continue;
        u32 j=i+15;
        while(j<header && response[j]==' ')j++;
        u32 digits=0;
        while(j<header && response[j]>='0'&&response[j]<='9'){
            if(++digits>8)return false;
            expected=expected*10u+(u32)(response[j++]-'0');
        }
        if(digits)found=true;
    }
    if(!found || expected>4096u || (u64)header+expected!=(u64)n)return false;
    return true;
}
static bool https_request(const char *host,const char *path,char *result,u32 cap,
                          const u8 *override_addr,u16 dest_port){
    if(result && cap)result[0]=0;
    if(!result||cap<128||cap>4096||!valid_host(host)||!valid_path(path) ||
       falcon_tls_anchor_count==0){
#ifdef FALCON_QEMU_TLS_PUBLIC_TEST
        outb(0xE9,'v');
#endif
        return false;
    }
    unsigned char entropy[32];
    u32 days=0,seconds=0;
    if(!cpu_random(entropy) || !cert_time(&days,&seconds)){
#ifdef FALCON_QEMU_TLS_PUBLIC_TEST
        outb(0xE9,'t');
#endif
        return false;
    }
#if defined(FALCON_QEMU_TLS_TEST) || defined(FALCON_QEMU_TLS_PUBLIC_TEST)
    outb(0xE9,'R'); /* RNG + RTC */
#endif
    u8 addr[4];
    if(override_addr)k_memcpy(addr,override_addr,4);
    else if(!native_net_dns_query(host,addr)){
#ifdef FALCON_QEMU_TLS_PUBLIC_TEST
        outb(0xE9,'d');
#endif
        return false;
    }
    if(!native_tcp_connect(addr,dest_port)){
#ifdef FALCON_QEMU_TLS_PUBLIC_TEST
        outb(0xE9,'c');
#endif
        return false;
    }
#if defined(FALCON_QEMU_TLS_TEST) || defined(FALCON_QEMU_TLS_PUBLIC_TEST)
    outb(0xE9,'C'); /* native TCP connected */
#endif
    bool result_ok=false;
    br_ssl_client_init_full(&CLIENT,&X509,falcon_tls_anchors,falcon_tls_anchor_count);
    /* FalconOS has no saved/restored SSE/AVX state or full FPU initialization.
     * BearSSL's init_full() may auto-detect host AES-NI / PCLMUL / SSE2 and
     * then raise #UD when the TLS Finished record activates encryption.
     * Pin every hardware-selected record cipher to an audited constant-time
     * portable implementation until the kernel has correct FPU context and
     * interrupt-safe SIMD handling. NEVER weaken certificate verification.
     */
    br_ssl_engine_set_aes_cbc(&CLIENT.eng,
        &br_aes_ct64_cbcenc_vtable,&br_aes_ct64_cbcdec_vtable);
    br_ssl_engine_set_aes_ctr(&CLIENT.eng,&br_aes_ct64_ctr_vtable);
    br_ssl_engine_set_ghash(&CLIENT.eng,br_ghash_ctmul64);
    br_ssl_engine_set_chacha20(&CLIENT.eng,br_chacha20_ct_run);
    br_ssl_engine_set_poly1305(&CLIENT.eng,br_poly1305_ctmul_run);
    br_x509_minimal_set_time(&X509,days,seconds);
    br_ssl_engine_inject_entropy(&CLIENT.eng,entropy,sizeof entropy);
    k_memset(entropy,0,sizeof entropy);
    br_ssl_engine_set_buffer(&CLIENT.eng,IO_BUFFER,sizeof IO_BUFFER,1);
    if(!br_ssl_client_reset(&CLIENT,host,0))goto end;
#if defined(FALCON_QEMU_TLS_TEST) || defined(FALCON_QEMU_TLS_PUBLIC_TEST)
    outb(0xE9,'S'); /* TLS context ready */
#endif
    br_sslio_init(&SSLIO,&CLIENT.eng,tcp_read_cb,NULL,tcp_write_cb,NULL);
    char request[512];u32 at=0;
    const char *p="GET ";
    for(u32 i=0;p[i];i++)request[at++]=p[i];
    for(u32 i=0;path[i];i++){
        if(at+80>=sizeof request)goto end;
        request[at++]=path[i];
    }
    p=" HTTP/1.0\r\nHost: ";
    for(u32 i=0;p[i];i++)request[at++]=p[i];
    for(u32 i=0;host[i];i++){
        if(at+30>=sizeof request)goto end;
        request[at++]=host[i];
    }
    p="\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n";
    for(u32 i=0;p[i];i++)request[at++]=p[i];
#if defined(FALCON_QEMU_TLS_TEST) || defined(FALCON_QEMU_TLS_PUBLIC_TEST)
    outb(0xE9,'W'); /* initiating HTTPS request / TLS handshake */
#endif
    if(br_sslio_write_all(&SSLIO,request,at)<0 ||
       br_sslio_flush(&SSLIO)<0)goto end;
#if defined(FALCON_QEMU_TLS_TEST) || defined(FALCON_QEMU_TLS_PUBLIC_TEST)
    outb(0xE9,'X'); /* TLS write+flush succeeded */
#endif
    u32 received=0;
    for(u32 i=0;i<16 && received+1<cap;i++){
        int n=br_sslio_read(&SSLIO,result+received,cap-received-1u);
        if(n<1)break;
        received+=(u32)n;
        result[received]=0;
        if(secure_response_complete(result,received)){
            result_ok=true;break;
        }
    }
end:
#if defined(FALCON_QEMU_TLS_TEST) || defined(FALCON_QEMU_TLS_PUBLIC_TEST)
    {
       unsigned e=(unsigned)br_ssl_engine_last_error(&CLIENT.eng);
       outb(0xE9,'e');outb(0xE9,(u8)('A'+(e&15u)));
       outb(0xE9,(u8)('A'+((e>>4)&15u)));
    }
#endif
    /* All TLS records are verified by BearSSL prior to becoming plaintext. */
#ifdef FALCON_QEMU_TLS_PUBLIC_TEST
    outb(0xE9,result_ok?'Y':'F');
#endif
    if(!result_ok)result[0]=0;
    native_tcp_close();
    return result_ok;
}
bool native_https_get(const char *host,const char *path,char *response,u32 capacity){
    return https_request(host,path,response,capacity,NULL,443);
}
#if defined(FALCON_QEMU_TLS_TEST) || defined(FALCON_QEMU_TLS_PUBLIC_TEST)
bool native_https_ci_smoke(void){
    /* Override ONLY transport destination in CI. TLS SNI + X.509 hostname
     * validation still requires the leaf certificate for falcon.test. */
    const u8 qemu_host[4]={10,0,2,2};
    char response[256];
    bool ok=https_request("falcon.test","/falcon-test",response,sizeof response,
                          qemu_host,18443);
    return ok;
}
#endif
#endif /* FALCON_BEARSSL */
