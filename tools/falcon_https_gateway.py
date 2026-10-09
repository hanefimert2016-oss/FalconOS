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
    def do_GET(self):
        # No open Internet proxy on a public network.
        try:
            client=ipaddress.ip_address(self.client_address[0])
            if not (client.is_private or client.is_loopback):
                return self.send_text(403,"Denied outside trusted VM NAT.")
        except ValueError:return self.send_text(403,"Unknown client.")
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
            if status!=200 or len(data)>2400:
                return self.send_text(502,
                    "Page bigger than 2.4 KiB prototype limit or invalid status")
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
