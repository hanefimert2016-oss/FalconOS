#!/usr/bin/env python3
"""FalconOS optional host-assisted HTTPS text gateway for Virt-Manager.

Python HTTPS validates hostname and certificate with system CA bundle.
FalconOS connects to its *trusted VM host gateway* using local plaintext TCP.
This is NOT end-to-end TLS inside the guest and must never be used for
app updates, passwords, banking, or downloads. Read-only text prototype.
The browser explicitly labels host-verified HTTPS mode (F6).

Example Arch:
  python3 tools/falcon_https_gateway.py --bind 192.168.122.1
  # where 192.168.122.1 is the virbr0 host address (ip -4 addr show virbr0)
For QEMU's -netdev user:
  python3 tools/falcon_https_gateway.py --bind 127.0.0.1
"""
import argparse,http.server,ipaddress,socket,ssl,urllib.error,urllib.request
import urllib.parse,json,html,re
class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self,*args,**kwargs):
        return None

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self,fmt,*args):
        print("%s %s"%(self.client_address[0],fmt%args))
    def send_text(self,code,text):
        body=text.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type","text/plain; charset=utf-8")
        self.send_header("Content-Length",str(len(body)))
        self.send_header("Connection","close")
        self.end_headers()
        self.wfile.write(body)
    def safe_tls_fetch(self,url):
        parsed=urllib.parse.urlparse(url)
        if parsed.scheme!="https" or not parsed.hostname:return None
        # Only public destinations. Never follow redirects to private networks.
        for answer in socket.getaddrinfo(parsed.hostname,443,type=socket.SOCK_STREAM):
            if not ipaddress.ip_address(answer[4][0]).is_global:
                raise ValueError("Destination refused (not globally routable)")
        handler=urllib.request.build_opener(
            urllib.request.HTTPSHandler(context=ssl.create_default_context()),
            NoRedirect())
        req=urllib.request.Request(url,headers={
            "User-Agent":"FalconOS-Search/0.5 (education)",
            "Accept-Encoding":"identity",
            "Connection":"close"})
        with handler.open(req,timeout=12) as response:
            if response.status!=200:
                raise ValueError("Upstream status not 200")
            return response.read(120000)
    def verified_text(self,body):
        output=body.encode("utf-8") if isinstance(body,str) else body
        if len(output)>2700:output=output[:2700]
        self.send_response(200)
        self.send_header("Content-Type","text/plain; charset=utf-8")
        self.send_header("Content-Length",str(len(output)))
        self.send_header("X-Falcon-Host-HTTPS-Verified","yes")
        self.send_header("Connection","close")
        self.end_headers()
        self.wfile.write(output)
    def wiki_search(self,query):
        if not query or len(query)>110:
            return self.send_text(400,"Search query length invalid")
        api="https://en.wikipedia.org/w/api.php?action=opensearch&namespace=0&limit=6&format=json&search="+urllib.parse.quote(query,safe="")
        raw=self.safe_tls_fetch(api)
        entries=json.loads(raw)
        if not isinstance(entries,list) or len(entries)!=4:
            return self.send_text(502,"Unexpected Wikipedia API response")
        out=["Search: "+query,"Source: Wikipedia (certificate + hostname checked)",""]
        for i,name in enumerate(entries[1][:6]):
            summary=entries[2][i] if i<len(entries[2]) else ""
            link=entries[3][i] if i<len(entries[3]) else ""
            out.append(f"{i+1}. {name}")
            if summary:out.append(re.sub(r"\s+"," ",html.unescape(re.sub(r"<[^>]*>","",summary)))[:130])
            if link.startswith("https://en.wikipedia.org/"):out.append(link[:145])
            out.append("")
        if len(out)<4:out.append("No results found for this query.")
        return self.verified_text("\n".join(out)[:2000])
    def do_GET(self):
        # No open Internet proxy on a public network.
        try:
            client=ipaddress.ip_address(self.client_address[0])
            if not (client.is_private or client.is_loopback):
                return self.send_text(403,"Denied outside trusted VM NAT.")
        except ValueError:return self.send_text(403,"Unknown client.")
        if self.path.startswith("/search/") and len(self.path)<=390:
            try:
                return self.wiki_search(urllib.parse.unquote(self.path[8:]))
            except (OSError,ssl.SSLError,ValueError,KeyError,json.JSONDecodeError,
                    urllib.error.HTTPError) as e:
                return self.send_text(502,"Search/TLS error: "+str(e)[:160])
        if not self.path.startswith("/fetch/") or len(self.path)>380:
            return self.send_text(400,"Expect /fetch/example.com/path")
        original=self.path[7:]
        host,sep,tail=original.partition("/")
        if not host or len(host)>190 or not all(c.isascii() and
           (c.isalnum() or c in "-.") for c in host) or host[0] in "-.":
            return self.send_text(400,"Invalid hostname")
        try:
            # Block local/private targets, including IPv4/IPv6 DNS answers.
            for answer in socket.getaddrinfo(host,443,type=socket.SOCK_STREAM):
                ip=ipaddress.ip_address(answer[4][0])
                if not ip.is_global:
                    return self.send_text(403,"Private/reserved destination denied")
            url="https://"+host+"/"+tail
            opener=urllib.request.build_opener(
                urllib.request.HTTPSHandler(context=ssl.create_default_context()),
                NoRedirect())
            request=urllib.request.Request(url,headers={
                "User-Agent":"FalconOS-VerifiedText/0.1",
                "Accept-Encoding":"identity","Accept":"text/html,text/plain",
                "Connection":"close"})
            try:
                with opener.open(request,timeout=12) as response:
                    status=response.status
                    data=response.read(2401)
            except urllib.error.HTTPError as e:
                return self.send_text(502,
                    "Upstream HTTP error (redirects not followed): "+str(e.code))
            if status!=200:
                return self.send_text(502,"Invalid HTTPS status")
            # Compact bounded text is more useful than rejecting common pages.
            # No JS/CSS executes on the guest.
            if len(data)>2350:
                page=data.decode("utf-8",errors="replace")
                page=re.sub(r"(?is)<(script|style)[^>]*>.*?</\1>","",page)
                page=html.unescape(re.sub(r"(?s)<[^>]+>"," ",page))
                page=re.sub(r"\s+"," ",page).strip()
                data=page[:1800].encode("utf-8")
            # No script evaluation; data rendered as inert text in FalconOS.
            self.send_response(200)
            self.send_header("Content-Type","text/html; charset=utf-8")
            self.send_header("Content-Length",str(len(data)))
            self.send_header("X-Falcon-Host-HTTPS-Verified","yes")
            self.send_header("Connection","close")
            self.end_headers()
            self.wfile.write(data)
        except (OSError,ssl.SSLError,ValueError) as e:
            self.send_text(502,"TLS verification or HTTPS network failed: "+str(e)[:160])
def main():
    p=argparse.ArgumentParser()
    p.add_argument("--bind",required=True,help="127.0.0.1 (QEMU) or virbr0 IPv4 (libvirt)")
    p.add_argument("--port",type=int,default=18444)
    opts=p.parse_args()
    addr=ipaddress.ip_address(opts.bind)
    if not (addr.is_private or addr.is_loopback):
        raise SystemExit("Refusing public-network bind: use 127.0.0.1 or private virbr0 address")
    s=http.server.ThreadingHTTPServer((opts.bind,opts.port),Handler)
    print(f"FalconOS verified host-assisted HTTPS on {opts.bind}:{opts.port} (Ctrl+C stops)")
    s.serve_forever()
if __name__=="__main__":main()
