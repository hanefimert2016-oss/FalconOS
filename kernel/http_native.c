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

static bool visual_header_has(const char *haystack,const char *needle){
    for(u32 i=0;haystack[i];i++){
        u32 j=0;
        while(needle[j] && haystack[i+j] && needle[j]==haystack[i+j])j++;
        if(!needle[j])return true;
    }
    return false;
}
/* Opt-in visual browser transport: 320x180 RGB332 from a trusted LOCAL host
 * Chromium renderer. This is explicitly not native guest HTTPS or code exec.
 * Strong 200/status, content-type, provenance and exact-length checks are
 * mandatory before drawing any pixel. Binary data may include NUL bytes. */
bool native_http_get_visual_port(const char *host,u16 port,const char *path,
                                 u8 *pixels,u32 len){
    if(!host||!path||!pixels||len!=57600u||port!=18445u ||
       (k_strcmp(path,"/visual/falconos.tech/")!=0 &&
        k_strcmp(path,"/visual/reload/falconos.tech/")!=0))return false;
    u8 addr[4];
    if(!native_net_parse_ipv4(host,addr))return false;
    /* A public, DNS-overridden host must never impersonate the private visual
     * companion. The gateway must be the VM's private NAT gateway. */
    u8 gateway[4];
    if(!native_net_parse_ipv4(net_gateway(),gateway))return false;
    for(u32 i=0;i<4;i++)if(addr[i]!=gateway[i])return false;
    if(!native_tcp_connect(addr,port))return false;
    bool ok=false;
    char req[156];
    k_strcpy(req,"GET ");
    k_strcat(req,path);
    k_strcat(req," HTTP/1.0\r\nHost: falco-visual.local\r\nConnection: close\r\n\r\n");
    if(native_tcp_write((const u8*)req,(u32)k_strlen(req))!=k_strlen(req))goto done;
    char header[900];
    u32 header_used=0,received=0;
    bool body=false;
    u8 block[2048];
    for(u32 iteration=0;iteration<160u && received<len;iteration++){
        i32 n=native_tcp_read(block,sizeof block,1800);
        if(n<=0)break;
        for(i32 i=0;i<n;i++){
            u8 c=block[i];
            if(!body){
                if(header_used+1>=sizeof header)goto done;
                header[header_used++]=(char)c;
                header[header_used]=0;
                if(header_used>=4u &&
                   header[header_used-4]=='\r'&&header[header_used-3]=='\n'&&
                   header[header_used-2]=='\r'&&header[header_used-1]=='\n'){
                    /* The frame is inert pixels, but never display response
                     * data from a 502 challenge or a spoofed HTTP endpoint. */
                    if(k_strncmp(header,"HTTP/1.0 200",12)!=0 &&
                       k_strncmp(header,"HTTP/1.1 200",12)!=0)goto done;
                    if(!visual_header_has(header,"X-Falcon-Visual: chromium-rgb332") ||
                       !visual_header_has(header,"X-Falcon-Content-Verified: FalconOS-Contour") ||
                       !visual_header_has(header,"Content-Length: 57600"))goto done;
                    body=true;
                }
            }else{
                if(received>=len)goto done;
                pixels[received++]=c;
            }
        }
    }
    ok=body&&received==len;
done:
    native_tcp_close();
    return ok;
}
