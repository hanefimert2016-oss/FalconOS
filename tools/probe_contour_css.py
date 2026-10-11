#!/usr/bin/env python3
"""Extract live public Contour stylesheet design tokens for manual review."""
import json, re, ssl, urllib.request, urllib.parse, collections
from pathlib import Path
url="https://falconos.tech/"
req=urllib.request.Request(url,headers={"User-Agent":"Mozilla/5.0 (compatible; FalconOS visual audit)","Accept":"text/html"})
with urllib.request.urlopen(req,context=ssl.create_default_context(),timeout=30) as r:
    html=r.read(600000).decode("utf-8","replace")
print("SITE HTML bytes",len(html))
styles=re.findall(r'<link\b[^>]*href=["\']([^"\']+\.css[^"\']*)["\'][^>]*>',html,re.I)
print("CSS LINKS",styles)
with open("build/contour-reference-source.html","w") as f:f.write(html)
css=" ".join(re.findall(r'<style[^>]*>(.*?)</style>',html,re.I|re.S))
for href in styles[:6]:
    u=urllib.parse.urljoin(url,href)
    if urllib.parse.urlsplit(u).hostname!="falconos.tech":continue
    try:
        with urllib.request.urlopen(urllib.request.Request(u,headers={"User-Agent":"Mozilla/5.0"}),timeout=20) as r:
            css+=r.read(250000).decode("utf-8","replace")
    except Exception as ex:print("ERROR fetching CSS",repr(ex))
Path("build/contour-reference.css").write_text(css)
print("CSS LENGTH",len(css))
print("COMMON HEX",collections.Counter("#"+s for s in re.findall(r"#(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{3})\b",css)).most_common(30))
for token in ["font-family","--bg","--accent","--ink","--surface","background:","linear-gradient","radial-gradient",".hero",".mockup",".browser"]:
    i=css.find(token)
    if i>=0:print("CSS SNIPPET",token,repr(css[max(0,i-110):i+340]))
print("HTML HEAD",html[:6000].replace("\n"," ")[:6000])

# visual reference recorded for independent FalconOS native renderer
