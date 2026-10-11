#!/usr/bin/env python3
"""Falco Native Layout Gateway v1: NO Chromium, WebKit, browser automation or JS.

Only this public HTTPS origin is fetched. Python stdlib's HTMLParser extracts
the real document's semantic heading/paragraph data (not a screenshot) into a
bounded text-only FALCO/1 packet. FalconOS's OWN C layout/painting engine draws
native-resolution interface elements, text and cards in its framebuffer.

This is NOT a general modern browser: CSS cascade, image decoding, dynamic
JS-generated DOM, cross-origin navigation and cookies are not implemented.
TLS is verified by the HOST and must be labeled accordingly in the guest.
"""
import argparse
import html
import http.server
import ipaddress
import re
import socket
import ssl
import urllib.request
from html.parser import HTMLParser

SITE = "https://falconos.tech/"
FETCH_LIMIT = 180_000
OUTPUT_LIMIT = 2750
HEADERS = {
    "User-Agent": "FalcoNativeLayout/1.0 (+https://falconos.tech/)",
    "Accept": "text/html",
    "Accept-Encoding": "identity",
}

class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, msg, headers, newurl):
        # Never redirect to arbitrary hosts. No implicit HTML fallback.
        if newurl not in (SITE, "https://falconos.tech"):
            raise ValueError("Unexpected redirect destination")
        return super().redirect_request(request, fp, code, msg, headers, newurl)

class Extract(HTMLParser):
    CONTAINER = {"title":"T","h1":"H","h2":"S","h3":"S",
                 "p":"P","li":"L","a":"A","button":"B"}
    SKIP = {"script","style","svg","noscript","template","head"}
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.suppression = 0
        self.active = []
        self.output = []
    def handle_starttag(self, tag, attrs):
        if tag in self.SKIP:
            # permit title despite head; track tag nesting more precisely
            self.suppression += 1
        if tag in self.CONTAINER:
            self.active.append([tag, []])
    def handle_endtag(self, tag):
        if self.active and self.active[-1][0] == tag:
            entry, words = self.active.pop()
            content = self.clean(" ".join(words))
            if content and len(content)>=2:
                self.output.append((self.CONTAINER[entry],content))
        if tag in self.SKIP and self.suppression:
            self.suppression -= 1
    @staticmethod
    def clean(s):
        s = html.unescape(re.sub(r"\s+"," ",s)).strip()
        # Font atlas supports ASCII and Turkish glyphs. Normalize unsupported
        # punctuation rather than painting a literal '?' for every em dash.
        for a,b in (("—"," - "),("–"," - "),("…","..."),("“",'"'),
                    ("”",'"'),("’","'"),("•"," * "),("→"," -> ")):
            s=s.replace(a,b)
        s = "".join(c for c in s if c.isprintable() and c not in "|\r\n")
        return s[:162]
    def handle_data(self, data):
        # Inline nested tags e.g. strong are kept as text. Skip script/style
        # but preserve title even in the head element.
        if self.suppression and not (self.active and self.active[-1][0]=="title"):
            return
        for _,buffer in self.active:buffer.append(data)

def build_document(raw):
    decoded = raw.decode("utf-8", errors="replace")
    parser = Extract()
    parser.feed(decoded)
    title = re.search(r"(?is)<title[^>]*>(.*?)</title>",decoded)
    real_title = Extract.clean(title.group(1)) if title else ""
    # An upstream challenge/error page or fake "FalconOS" mention is not
    # accepted as the requested actual site.
    visible = " ".join(t for _,t in parser.output)
    if "FalconOS" not in real_title+visible or "Contour" not in real_title+visible:
        raise ValueError("FalconOS Contour identity markers absent in real HTML")
    elements = [("T",real_title or "FalconOS Contour")]
    # Keep ordering with no fabricated headings. Distinct first occurrence.
    seen=set()
    for role,txt in parser.output:
        if role=="T" or txt in seen:continue
        seen.add(txt)
        if len(elements)>=22:break
        elements.append((role,txt))
    lines=["FALCO/1","SOURCE|falconos.tech"]
    for role,txt in elements:
        candidate=f"{role}|{txt}"
        if len(("\n".join(lines+[candidate])+"\n").encode())>OUTPUT_LIMIT:break
        lines.append(candidate)
    return ("\n".join(lines)+"\n").encode("utf-8")

def fetch():
    req=urllib.request.Request(SITE,headers=HEADERS)
    opener=urllib.request.build_opener(
        urllib.request.HTTPSHandler(context=ssl.create_default_context()),
        NoRedirect())
    # Strict public-only DNS check; do not accidentally access private IP.
    addresses=socket.getaddrinfo("falconos.tech",443,type=socket.SOCK_STREAM)
    if not addresses or any(not ipaddress.ip_address(x[4][0]).is_global for x in addresses):
        raise ValueError("Unsafe upstream network address")
    with opener.open(req,timeout=15) as response:
        if response.status!=200:raise ValueError(f"Upstream HTTP {response.status}")
        if "text/html" not in response.headers.get("Content-Type",""):
            raise ValueError("Upstream was not HTML")
        raw=response.read(FETCH_LIMIT+1)
        if len(raw)>FETCH_LIMIT:raise ValueError("Page HTML too large")
    return build_document(raw)

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self,fmt,*args):print(fmt%args,flush=True)
    def do_GET(self):
        remote=ipaddress.ip_address(self.client_address[0])
        if not (remote.is_private or remote.is_loopback):
            return self.reply(403,b"Private VM only",False)
        if self.path!="/falcon/native/":
            return self.reply(404,b"Only falconos.tech native layout allowed",False)
        try:
            body=fetch()
        except Exception as ex:
            print("NATIVE_LAYOUT_FETCH_ERROR",repr(ex),flush=True)
            return self.reply(502,repr(ex).encode()[:160],False)
        print("NATIVE_LAYOUT_FROM_REAL_HTML",len(body),flush=True)
        self.reply(200,body,True)
    def reply(self,status,data,verified):
        self.send_response(status)
        self.send_header("Content-Type","text/plain; charset=utf-8")
        self.send_header("Content-Length",str(len(data)))
        if verified:
            self.send_header("X-Falco-Native-Verified","yes")
            self.send_header("X-Falco-Source","falconos.tech")
        self.send_header("Connection","close")
        self.end_headers()
        self.wfile.write(data)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--bind",default="127.0.0.1")
    ap.add_argument("--port",type=int,default=18446)
    args=ap.parse_args()
    addr=ipaddress.ip_address(args.bind)
    if not (addr.is_private or addr.is_loopback):
        raise SystemExit("Bind limited to loopback/private VM network")
    http.server.HTTPServer((args.bind,args.port),Handler).serve_forever()
if __name__=="__main__":
    main()
