#!/usr/bin/env python3
"""Independent visual reference only: never used to render FalconOS/Falco."""
import json
from pathlib import Path
from playwright.sync_api import sync_playwright
root=Path("build");root.mkdir(exist_ok=True)
with sync_playwright() as p:
    browser=p.chromium.launch(headless=True,args=["--disable-dev-shm-usage","--no-sandbox"])
    page=browser.new_page(viewport={"width":1600,"height":900},device_scale_factor=1)
    try:
        response=page.goto("https://falconos.tech/",wait_until="domcontentloaded",timeout=45000)
        page.wait_for_timeout(2500)
        if not response or response.status!=200:raise RuntimeError("Site did not return HTTP 200")
        if "Sadece çalışmaz." not in page.locator("body").inner_text():
            raise AssertionError("Actual site's hero headline missing")
        page.screenshot(path=str(root/"Falco-Reference-Real-Chromium-1600x900.png"),full_page=False)
        page.screenshot(path=str(root/"Falco-Reference-Full-Page.png"),full_page=True)
        data=page.evaluate("""() => {
          let s=getComputedStyle(document.body), hero=document.querySelector('h1');
          let h=hero?getComputedStyle(hero):null;
          return {title:document.title,bodyFont:s.fontFamily,bodyColor:s.color,bodyBackground:s.backgroundColor,
           heroFont:h&&h.fontFamily,heroSize:h&&h.fontSize,heroWeight:h&&h.fontWeight,
           heroBox:hero&&hero.getBoundingClientRect().toJSON(),
           images:[...document.images].slice(0,20).map(x=>({src:x.currentSrc,w:x.naturalWidth,h:x.naturalHeight})),
           sheets:[...document.styleSheets].map(x=>x.href||"inline")};
        }""")
        (root/"falco-reference-visual.json").write_text(json.dumps(data,indent=2,ensure_ascii=False))
        print(json.dumps(data,ensure_ascii=False),flush=True)
    finally:
        browser.close()
