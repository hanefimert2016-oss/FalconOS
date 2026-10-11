#!/usr/bin/env python3
"""Falco 50-site honest compatibility audit, real HTTPS network requests.

50 randomly selected sites from a reproducible curated public seed pool.
Checks TLS hostname, status, HTML title, CSS & JavaScript dependencies and
whether native FalconOS renderer ACTUALLY supports the page. A real GET is NOT
claimed to be a successfully rendered browser frame.
No Chromium, Playwright, WebKit, screenshot mocks, CAPTCHA or bot bypass.
"""
import argparse,concurrent.futures,datetime,html.parser,json,random,re,ssl,time
import urllib.error,urllib.request
from pathlib import Path

HOSTS="""example.com wikipedia.org wikimedia.org w3.org python.org docs.python.org
kernel.org gnu.org debian.org archlinux.org fedoraproject.org ubuntu.com
freebsd.org openbsd.org llvm.org rust-lang.org golang.org ziglang.org
git-scm.com github.com gitlab.com codeberg.org sourcehut.org
mozilla.org developer.mozilla.org web.dev whatwg.org caniuse.com
stackoverflow.com stackexchange.com superuser.com serverfault.com
npmjs.com pypi.org crates.io rubygems.org php.net perl.org
sqlite.org postgresql.org mariadb.org redis.io mongodb.com
docker.com kubernetes.io helm.sh terraform.io ansible.com
nginx.org apache.org httpd.apache.org tomcat.apache.org
cloudflare.com letsencrypt.org eff.org torproject.org
openstreetmap.org naturalearthdata.com nasa.gov esa.int noaa.gov
nationalgeographic.com britannica.com archive.org gutenberg.org
projecteuclid.org arxiv.org nature.com sciencedirect.com
researchgate.net sciencemag.org mit.edu harvard.edu stanford.edu
berkeley.edu cmu.edu cam.ac.uk ox.ac.uk ethz.ch epfl.ch
bbc.com reuters.com theguardian.com nytimes.com
time.com medium.com dev.to hashnode.com
hackernews.ycombinator.com news.ycombinator.com producthunt.com
duckduckgo.com brave.com startpage.com
unsplash.com pexels.com pixabay.com
figma.com blender.org godotengine.org unity.com
kde.org gnome.org freedesktop.org wayland.freedesktop.org
www.tensorflow.org pytorch.org huggingface.co
falconos.tech
""".split()
class Tags(html.parser.HTMLParser):
    def __init__(self):
        super().__init__();self.title="";self.title_on=False
        self.css=0;self.js=0;self.images=0;self.forms=0;self.tags=0
    def handle_starttag(self,t,a):
        self.tags+=1
        attrs=dict(a)
        if t=="title":self.title_on=True
        if t=="script":self.js+=1
        if t=="link" and "stylesheet" in attrs.get("rel",""):self.css+=1
        if t=="img":self.images+=1
        if t=="form":self.forms+=1
    def handle_endtag(self,t):
        if t=="title":self.title_on=False
    def handle_data(self,data):
        if self.title_on:self.title+=data
def visit(host,timeout=10):
    url="https://"+host+"/"
    entry={"site":host,"url":url,"tls_verified":False,"html_ok":False,
           "native_visual_rendered":False,"full_css_js_supported":False}
    try:
        request=urllib.request.Request(url,headers={
            "User-Agent":"FalcoCompatibilityAudit/1.0 (+https://falconos.tech/)",
            "Accept":"text/html","Accept-Encoding":"identity"})
        opener=urllib.request.build_opener(
            urllib.request.HTTPSHandler(context=ssl.create_default_context()))
        with opener.open(request,timeout=timeout) as response:
            final=response.geturl()
            if not final.startswith("https://"):
                raise ValueError("unexpected HTTPS downgrade")
            entry["tls_verified"]=True
            entry["status"]=response.status
            entry["final_url"]=final
            entry["content_type"]=response.headers.get("Content-Type","")
            raw=response.read(180000)
        decoded=raw.decode("utf-8","replace")
        if "html" in entry["content_type"].lower() and ("<html" in decoded.lower() or "<!doctype html" in decoded.lower()):
            parser=Tags();parser.feed(decoded)
            entry.update({"html_ok":True,"title":parser.title[:100],
                 "css_links":parser.css,"script_elements":parser.js,
                 "images":parser.images,"forms":parser.forms,
                 "html_tags_seen":parser.tags})
        else:
            entry["error"]="Response is not confirmed HTML"
    except Exception as ex:
        entry["error"]=str(ex)[:180]
    entry["native_visual_reason"]=(
        "Falco currently has an owner-site-specific Contour painter; this audit "
        "does NOT execute the guest renderer or its JS/CSS cascade."
    )
    return entry
def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--seed",type=int,default=20261011)
    ap.add_argument("--count",type=int,default=50)
    ap.add_argument("--output",default="build/falco-50-site-results.json")
    args=ap.parse_args()
    if args.count!=50:raise SystemExit("This CI audit must sample exactly 50 sites")
    selected=random.Random(args.seed).sample(HOSTS,args.count)
    started=time.monotonic()
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        results=list(pool.map(visit,selected))
    verified=sum(x["tls_verified"] for x in results)
    html=sum(x["html_ok"] for x in results)
    native=sum(x["native_visual_rendered"] for x in results)
    payload={"sample_seed":args.seed,"date_utc":datetime.datetime.now(datetime.timezone.utc).isoformat(),
             "site_count":len(results),"https_verified_count":verified,
             "html_received_count":html,"guest_native_visual_proven_count":native,
             "elapsed_seconds":round(time.monotonic()-started,2),
             "honesty":"HTTPS GET and HTML parsing are NOT proof that FalconOS painted the site; no JS/CSS engine is claimed.",
             "sites":results}
    out=Path(args.output);out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text(json.dumps(payload,indent=2,ensure_ascii=False),encoding="utf-8")
    print(f"50_SITE_AUDIT: HTTPS={verified}/50 HTML={html}/50 "
          f"GUEST_VISUAL_PROVEN={native}/50 in {payload['elapsed_seconds']}s")
    print("REPORT",out)
if __name__=="__main__":main()
