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
    def validated_https_request(self,url,max_bytes,accept="text/html,text/plain"):
        """Follow at most four safe HTTPS redirects with CA/hostname checks.

        Refuse HTTP downgrade, credential-bearing URLs, non-public DNS answers,
        custom ports, and URL cycles. Never turn this into an open localhost proxy.
        """
        seen=set()
        redirects=(301,302,303,307,308)
        opener=urllib.request.build_opener(
            urllib.request.HTTPSHandler(context=ssl.create_default_context()),
            NoRedirect())
        for hop in range(5):
            parsed=urllib.parse.urlsplit(url)
            host=parsed.hostname or ""
            if (parsed.scheme!="https" or parsed.username is not None or
                parsed.password is not None or parsed.port not in (None,443) or
                not (1<=len(host)<=190) or
                not re.fullmatch(r"[a-zA-Z0-9.-]+",host) or
                host[0] in ".-" or host[-1] in ".-"):
                raise ValueError("Blocked unsafe redirect destination")
            if url in seen:
                raise ValueError("Redirect loop blocked")
            seen.add(url)
            answers=socket.getaddrinfo(host,443,type=socket.SOCK_STREAM)
            if not answers or any(not ipaddress.ip_address(a[4][0]).is_global
                                  for a in answers):
                raise ValueError("Blocked non-public HTTPS destination")
            request=urllib.request.Request(url,headers={
                "User-Agent":"FalconOS-VerifiedText/1.0",
                "Accept-Encoding":"identity","Accept":accept,
                "Connection":"close"})
            try:
                with opener.open(request,timeout=12) as response:
                    if response.status!=200:
                        raise ValueError("Unexpected upstream status "+str(response.status))
                    return response.read(max_bytes)
            except urllib.error.HTTPError as error:
                if error.code not in redirects:
                    raise ValueError("Upstream HTTP "+str(error.code)) from error
                location=error.headers.get("Location")
                if not location or hop>=4:
                    raise ValueError("Redirect missing target or exceeded limit")
                url=urllib.parse.urljoin(url,location)
        raise ValueError("Too many HTTPS redirects")
    def safe_tls_fetch(self,url):
        return self.validated_https_request(url,120000,accept="application/json")

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
    def github_repository_search(self,query):
        """Public read-only fallback only when Wikipedia cannot be reached.

        Actual results are returned by GitHub's HTTPS JSON API; never fake
        a Wikipedia result, and never silently present another source.
        """
        url="https://api.github.com/search/repositories?q="+urllib.parse.quote(query,safe="")+"&per_page=5"
        payload=json.loads(self.safe_tls_fetch(url))
        out=["Search: "+query,
             "Source: GitHub public repositories (Wikipedia unavailable)",
             "HTTPS certificate and hostname verified on host",""]
        for row in payload.get("items",[])[:5]:
            title=str(row.get("full_name",""))[:95]
            target=str(row.get("html_url",""))
            if not target.startswith("https://github.com/"):
                continue
            out.append(title)
            description=str(row.get("description") or "")[:130]
            if description:out.append(description)
            out.append(target)
            out.append("")
        if len(out)<=4:out.append("No public repositories found.")
        return self.verified_text("\n".join(out)[:2200])

    def do_GET(self):
        # No open Internet proxy on a public network.
        try:
            client=ipaddress.ip_address(self.client_address[0])
            if not (client.is_private or client.is_loopback):
                return self.send_text(403,"Denied outside trusted VM NAT.")
        except ValueError:return self.send_text(403,"Unknown client.")
        if self.path.startswith("/search/") and len(self.path)<=390:
            query=urllib.parse.unquote(self.path[8:])
            try:
                return self.wiki_search(query)
            except (OSError,ssl.SSLError,ValueError,KeyError,json.JSONDecodeError,
                    urllib.error.HTTPError) as primary:
                print("WIKI_SEARCH_UNAVAILABLE",repr(primary),flush=True)
                try:
                    return self.github_repository_search(query)
                except (OSError,ssl.SSLError,ValueError,KeyError,
                        json.JSONDecodeError,urllib.error.HTTPError) as fallback:
                    return self.send_text(502,"Both HTTPS search providers unavailable: "+str(fallback)[:135])
        if not self.path.startswith("/fetch/") or len(self.path)>380:
            return self.send_text(400,"Expect /fetch/example.com/path")
        original=self.path[7:]
        host,sep,tail=original.partition("/")
        if not host or len(host)>190 or not all(c.isascii() and
           (c.isalnum() or c in "-.") for c in host) or host[0] in "-.":
            return self.send_text(400,"Invalid hostname")
        try:
            # HTTPS-only redirects and every DNS hop are checked above.
            url="https://"+host+"/"+tail
            data=self.validated_https_request(url,2401)
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
            print("HTTPS_FETCH_FAILURE",host,repr(e),flush=True)
            self.send_text(502,"HTTPS source unavailable: "+str(e)[:180])
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
