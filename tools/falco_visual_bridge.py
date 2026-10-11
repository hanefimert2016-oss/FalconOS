#!/usr/bin/env python3
"""Opt-in REAL Chromium visual renderer for the FalconOS Falco QEMU guest.

This is a HOST-RENDERED BROWSER, not native FalconOS HTML/CSS/JS execution.
Chromium performs real HTTPS and page layout; read-only RGB332 screenshots go
over the guest-to-host QEMU NAT link. No artificial 'human verification',
Cloudflare challenge solver, CAPTCHA automation, or stealth fingerprints.
Only the configured public site is served; never an open HTTP/HTTPS proxy.

Install: python -m pip install playwright pillow
         python -m playwright install chromium
Start:   python tools/falco_visual_bridge.py --bind 127.0.0.1
Guest:   Launch Falco -> F9 visual Chromium -> F5 refresh
"""
import argparse
import http.server
import ipaddress
import io
import socket
import threading
from pathlib import Path
from urllib.parse import urlparse

SCREEN_W,SCREEN_H=320,180
SITE="https://falconos.tech/"
ALLOWED_HOST="falconos.tech"
FRAME_LENGTH=SCREEN_W*SCREEN_H

class Browser:
    def __init__(self):
        self.guard=threading.RLock()
        self.playwright=None
        self.browser=None
        self.context=None
        self.page=None
        self.ready=False
        self.last_error=""
        self.frame=None
    def open(self):
        from playwright.sync_api import sync_playwright
        self.playwright=sync_playwright().start()
        self.browser=self.playwright.chromium.launch(headless=True,args=["--no-sandbox"])
        self.context=self.browser.new_context(viewport={"width":960,"height":540},
                                               device_scale_factor=1,locale="tr-TR")
        self.page=self.context.new_page()
        # Real user-facing browser engine; no spoofed captcha clearance.
        self.page.goto(SITE,wait_until="domcontentloaded",timeout=30000)
        self.page.wait_for_timeout(1200)
        u=urlparse(self.page.url)
        title=self.page.title()
        content=self.page.locator("body").inner_text(timeout=8000)[:4000]
        # Reject Cloudflare access denied / challenge pages, or a redirect to
        # another domain. Never show a false 'site loaded' status.
        if u.scheme!="https" or u.hostname!=ALLOWED_HOST:
            raise RuntimeError("Browser redirected away from allowed HTTPS host")
        if "FalconOS" not in title+content or "Contour" not in title+content:
            raise RuntimeError("Authentic FalconOS Contour content not confirmed")
        self.ready=True
        print("VISUAL_SITE_VERIFIED",repr(title),self.page.url,flush=True)
    def capture(self):
        from PIL import Image
        # Screenshots come FROM Chromium rendering HTML+CSS+images+JS.
        shot=self.page.screenshot(type="png",animations="disabled",timeout=15000)
        im=Image.open(io.BytesIO(shot)).convert("RGB").resize((SCREEN_W,SCREEN_H),Image.Resampling.BILINEAR)
        raw=im.tobytes()
        packed=bytearray(FRAME_LENGTH)
        for i in range(FRAME_LENGTH):
            r,g,b=raw[i*3:i*3+3]
            packed[i]=((r&0xe0)|((g>>3)&0x1c)|(b>>6))
        self.frame=bytes(packed)
        return self.frame
    def frame_bytes(self,reload=False):
        with self.guard:
            if not self.ready:self.open()
            if reload:
                self.page.reload(wait_until="domcontentloaded",timeout=25000)
                self.page.wait_for_timeout(1000)
                # Recheck actual page; don't reuse a stale image after an error.
                self.ready=False
                self.open_check()
            return self.capture()
    def open_check(self):
        u=urlparse(self.page.url)
        both=self.page.title()+self.page.locator("body").inner_text(timeout=8000)[:4000]
        if u.scheme!="https" or u.hostname!=ALLOWED_HOST or "FalconOS" not in both or "Contour" not in both:
            raise RuntimeError("Reload returned unverified or challenged content")
        self.ready=True

BROWSER=Browser()
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self,fmt,*args):
        print("VISUAL_HTTP",self.client_address[0],fmt%args,flush=True)
    def do_GET(self):
        try:
            caller=ipaddress.ip_address(self.client_address[0])
            if not (caller.is_loopback or caller.is_private):
                return self.fail(403,"Only QEMU private NAT is allowed")
        except ValueError:return self.fail(403,"Invalid source IP")
        if self.path not in ("/visual/falconos.tech/","/visual/reload/falconos.tech/"):
            return self.fail(404,"Only the configured FalconOS site is available")
        try:
            frame=BROWSER.frame_bytes(reload=self.path.startswith("/visual/reload/"))
        except Exception as exc:
            BROWSER.ready=False
            BROWSER.last_error=str(exc)
            print("VISUAL_RENDER_FAILED",repr(exc),flush=True)
            return self.fail(502,"Live Chromium page not verified: "+str(exc)[:200])
        self.send_response(200)
        self.send_header("X-Falcon-Visual","chromium-rgb332")
        self.send_header("X-Falcon-Content-Verified","FalconOS-Contour")
        self.send_header("X-Falcon-TLS","host-browser")
        self.send_header("Content-Type","application/octet-stream")
        self.send_header("Content-Length",str(FRAME_LENGTH))
        self.send_header("Connection","close")
        self.end_headers()
        self.wfile.write(frame)
    def fail(self,code,msg):
        content=msg.encode("utf-8",errors="replace")
        self.send_response(code)
        self.send_header("Content-Type","text/plain")
        self.send_header("Content-Length",str(len(content)))
        self.send_header("Connection","close")
        self.end_headers()
        self.wfile.write(content)
def main():
    p=argparse.ArgumentParser()
    p.add_argument("--bind",default="127.0.0.1")
    p.add_argument("--port",type=int,default=18445)
    args=p.parse_args()
    ip=ipaddress.ip_address(args.bind)
    if not (ip.is_loopback or ip.is_private):
        raise SystemExit("Only loopback / private addresses permitted")
    httpd=http.server.ThreadingHTTPServer((args.bind,args.port),Handler)
    print(f"Falco visual Chromium renderer on {args.bind}:{args.port} - real {SITE}",flush=True)
    httpd.serve_forever()
if __name__=="__main__":main()
