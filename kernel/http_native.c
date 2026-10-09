/* FalconOS minimal native HTTP/1.0 client over native TCP.
 * Explicitly plaintext. Never use for system/app/package updates.
 */
#include "falcon.h"
#define HTTP_LIMIT 512u
bool native_http_get_port(const char *host,u16 port,const char *path,
                          char *response,u32 capacity){
    if(!host||!path||!response||capacity<32 || capacity>4096 ||
       path[0]!='/' || !port)return false;
    u8 remote[4];
    if(!native_net_parse_ipv4(host,remote) &&
       !native_net_dns_query(host,remote))return false;
    char request[HTTP_LIMIT];
    u32 used=0;
    static const char *prefix="GET ";
    for(u32 i=0;prefix[i];i++)request[used++]=prefix[i];
    for(u32 i=0;path[i];i++){
        if(used>=HTTP_LIMIT-80 || path[i]<'!' || path[i]>'~')return false;
        request[used++]=path[i];
    }
    static const char *middle=" HTTP/1.0\r\nHost: ";
    for(u32 i=0;middle[i];i++)request[used++]=middle[i];
    for(u32 i=0;host[i];i++){
        if(used>=HTTP_LIMIT-30)return false;
        char c=host[i];
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
             (c>='0'&&c<='9')||c=='-'||c=='.'))return false;
        request[used++]=c;
    }
    static const char *suffix="\r\nConnection: close\r\n\r\n";
    for(u32 i=0;suffix[i];i++)request[used++]=suffix[i];
    if(!native_tcp_connect(remote,port))return false;
    bool ok=false;
    if(native_tcp_write((const u8*)request,used)!=(i32)used)goto done;
    u32 received=0;
    for(u32 chunk=0;chunk<16;chunk++){
        if(received+1>=capacity)break;
        i32 n=native_tcp_read((u8*)response+received,
                             capacity-received-1,200u);
        if(n==0){ok=true;break;}
        if(n<0)break;
        received+=(u32)n;
    }
    response[received]=0;
    if(received<12 || response[0]!='H'||response[1]!='T'||
       response[2]!='T'||response[3]!='P'||response[4]!='/'||
       response[8]!=' '||response[9]!='2'){
        ok=false;
    }
done:
    native_tcp_close();
    return ok;
}
bool native_http_get(const char *hostname,const char *path,
                     char *response,u32 capacity){
    return native_http_get_port(hostname,80,path,response,capacity);
}
/* Fail closed until authenticated TLS can run inside the guest.
 * An HTTPS URL is NEVER downgraded to plaintext HTTP or COM1 here.
 */
#ifndef FALCON_BEARSSL
bool native_https_get(const char *hostname,const char *path,
                      char *response,u32 capacity){
    (void)hostname;(void)path;
    if(response&&capacity)response[0]=0;
    return false;
}

#endif /* !FALCON_BEARSSL: secure backend is kernel/https_bearssl.c */
