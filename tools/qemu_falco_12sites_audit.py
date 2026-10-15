#!/usr/bin/env python3
"""Authentic 12-site Falco Browser evaluation in OVMF/QEMU.

No Chrome/Playwright screenshots: screendump PPM images are taken directly
from QEMU's native FalconOS framebuffer. The host provides *only* CA-checked
HTTPS through the repository's existing companion proxy, which reduces remote
HTML to semantic text. Output evaluates presentation fidelity, not TLS alone.
Every attempted site gets an actual VM screenshot even on navigation failure.
"""
import json
import math
import random
import socket
import subprocess
import sys
import time
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
from qemu_ui_smoke import command, screenshot, wait_for_marker
from qemu_smoke import ppm_to_png

ROOT=Path("build").absolute()
OUT=ROOT/"falco-sites-audit"
OUT.mkdir(parents=True,exist_ok=True)
DEBUG=OUT/"falco-guest-debug.log"
MONITOR=OUT/"falco-monitor.sock"
GATEWAY_LOG=OUT/"falco-gateway.log"
REPORT=OUT/"falco-12site-results.json"
COMPOSITE=OUT/"Falco-12-Gercek-Site-Testi.png"
CANDIDATES=[
 "example.com","www.wikipedia.org","www.python.org","www.kernel.org",
 "www.gnu.org","www.w3.org","archive.org","github.com","www.mozilla.org",
 "www.cloudflare.com","www.debian.org","stackoverflow.com","www.ietf.org",
 "www.wikimedia.org","www.rust-lang.org","www.nasa.gov","www.bbc.com",
 "www.openstreetmap.org","www.freebsd.org","www.kde.org"
]
# Stable random sample so the exact set can be audited and repeated.
SITES=random.Random(20261015).sample(CANDIDATES,12)
COLORS={"Basarili":"#228a6f","Yarim":"#c48f30","Berbat":"#b54f54"}
NOTES={
 "success":"Sayfa metni geldi fakat Falco, orijinal CSS/gorsel/JS sayfasini yeniden cizemiyor.",
 "failed":"Gercek HTTPS istegi veya FalconOS sayfa yuklemesi basarisiz; icerik gorunmuyor.",
 "no_marker":"Yukleme olayi netlesenmedi; ekran goruntusu yine de kaydedildi.",
 "emulator":"FalconOS icinde gezinme gosterilemedi; sahte site ekrani uretilmedi.",
}
def dbg():return DEBUG.read_bytes() if DEBUG.exists() else b""
def font(sz):
 for f in ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
           "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf"):
  if Path(f).exists():return ImageFont.truetype(f,sz)
 return ImageFont.load_default()
def wrap(draw,text,width,f):
 out=[];line=""
 for part in text.split(" "):
  nextpart=(line+" "+part).strip()
  if draw.textbbox((0,0),nextpart,font=f)[2]>width and line:
   out.append(line);line=part
  else:line=nextpart
 if line:out.append(line)
 return out[:3]
def render(results,technical_error=None):
 counts={v:sum(row["category"]==v for row in results) for v in COLORS}
 total=len(results)
 W=2360; cols=3;CW=760; gap=20;top=178; card_h=490
 rows=math.ceil(total/cols) if total else 1
 im=Image.new("RGB",(W,top+rows*(card_h+gap)+65),"#0b1220")
 dr=ImageDraw.Draw(im)
 title=font(48);normal=font(24);small=font(20);sitefont=font(26)
 dr.text((35,21),"FALCO WEB  /  GERCEK QEMU SITE TESTI",font=title,fill="#eaf1fd")
 dr.text((36,90),f"{total} adres  |  Basarili: {counts['Basarili']}  |  Yarim: {counts['Yarim']}  |  Berbat: {counts['Berbat']}",
         font=normal,fill="#d5e1f3")
 dr.text((36,132),"Kaynak: FalconOS OVMF/QEMU framebuffer; host dogrulanmis HTTPS gecidi (sayfalari yalnizca metne indirger).",
         font=small,fill="#9babc4")
 for i,row in enumerate(results):
  col=i%cols;rr=i//cols;x=35+col*(CW+gap);y=top+rr*(card_h+gap)
  dr.rounded_rectangle((x,y,x+CW,y+card_h),radius=14,fill="#16243a")
  dr.rectangle((x,y,x+8,y+card_h),fill=COLORS[row["category"]])
  dr.text((x+22,y+12),f"{i+1:02d}  {row['site']}",font=sitefont,fill="#ffffff")
  dr.text((x+24,y+49),row["category"].upper(),font=small,fill=COLORS[row["category"]])
  shot=Path(row.get("screenshot") or "")
  if shot.is_file():
   try:
    src=Image.open(shot).convert("RGB")
    src.thumbnail((CW-48,340))
    ox=x+(CW-src.width)//2;oy=y+80+(340-src.height)//2
    im.paste(src,(ox,oy))
    dr.rectangle((ox-1,oy-1,ox+src.width,oy+src.height),outline="#4c668b",width=2)
   except Exception as exc:
    row["image_error"]=str(exc)[:80]
  else:
   dr.text((x+25,y+160),"EKRAN GORUNTUSU ALINAMADI",font=normal,fill="#d86d6d")
  for k,line in enumerate(wrap(dr,row["critique"],CW-50,small)):
   dr.text((x+22,y+425+k*22),line,font=small,fill="#cfdaed")
 if technical_error:
  dr.text((40,top+rows*(card_h+gap)+13),str(technical_error)[:140],font=small,fill="#d77c84")
 else:
  dr.text((40,top+rows*(card_h+gap)+13),
      "Basarili=tutarlilik + sayfa gorunumu; Yarim=dogrulanmis metin ama tasarim kaybi; Berbat=yuklenemedi.",font=small,fill="#aab9ce")
 im.save(COMPOSITE,optimize=True)
 return counts

def type_url(sock,site):
 # Ctrl+L clears guest Browser omnibox; physical PS/2 scan codes are injected.
 command(sock,"sendkey ctrl-l",.16)
 for ch in site:
  if ch.islower() or ch.isdigit(): key=ch
  elif ch==".":key="dot"
  elif ch=="-":key="minus"
  else:raise ValueError("Unsupported QEMU key "+repr(ch))
  command(sock,"sendkey "+key,.045)

def main():
 for f in (DEBUG,MONITOR,GATEWAY_LOG):
  if f.exists():f.unlink()
 results=[]
 gateway=None;guest=None;logfile=None
 try:
  logfile=GATEWAY_LOG.open("wb")
  gateway=subprocess.Popen([sys.executable,"-u","tools/falcon_https_gateway.py",
                            "--bind","127.0.0.1"],
      stdout=logfile,stderr=subprocess.STDOUT)
  for _ in range(100):
   try:
    with socket.create_connection(("127.0.0.1",18444),timeout=.4):break
   except OSError:time.sleep(.1)
  else:raise RuntimeError("Falco HTTPS gateway did not start")
  ovmf=[(Path("/usr/share/OVMF/OVMF_CODE_4M.fd"),Path("/usr/share/OVMF/OVMF_VARS_4M.fd")),
        (Path("/usr/share/OVMF/OVMF_CODE.fd"),Path("/usr/share/OVMF/OVMF_VARS.fd"))]
  pair=next(((a,b) for a,b in ovmf if a.is_file() and b.is_file()),None)
  if not pair:raise RuntimeError("OVMF firmware unavailable")
  import shutil
  nvram=OUT/"ovmf-vars.fd";shutil.copyfile(pair[1],nvram)
  cmd=["qemu-system-x86_64","-machine","q35","-accel","tcg",
       "-cpu","max","-m","2048","-smp","2",
       "-drive",f"if=pflash,unit=0,format=raw,readonly=on,file={pair[0]}",
       "-drive",f"if=pflash,unit=1,format=raw,file={nvram}",
       "-drive","file=build/FalconOS.iso,if=ide,index=2,media=cdrom",
       "-boot","order=d","-display","none","-vga","std",
       "-netdev","user,id=net","-device","rtl8139,netdev=net",
       "-serial","none","-monitor",f"unix:{MONITOR},server=on,wait=off",
       "-debugcon",f"file:{DEBUG}","-global","isa-debugcon.iobase=0xe9",
       "-no-reboot"]
  with (OUT/"qemu-stderr.log").open("wb") as errors:
   guest=subprocess.Popen(cmd,stdout=subprocess.DEVNULL,stderr=errors)
   deadline=time.monotonic()+24
   while not MONITOR.exists():
    if guest.poll() is not None or time.monotonic()>deadline:
     raise RuntimeError("QEMU monitor unavailable")
    time.sleep(.1)
   with socket.socket(socket.AF_UNIX) as sock:
    sock.settimeout(12);sock.connect(str(MONITOR))
    # The UEFI loader prints FALCONBOOT and MENU text containing the
    # letters L/T/A. Never mistake these firmware bytes for real setup
    # key transitions: wait until settings_init emits wB0 and installer L.
    wait_for_marker(DEBUG,b"wB0L",timeout=100)
    time.sleep(.7)
    for marker in (b"T",b"A",b"K",b"D",b"U"):
     off=len(dbg());command(sock,"sendkey ret",.25)
     wait_for_marker(DEBUG,marker,after=off,timeout=70)
    for ch in "falcon":command(sock,"sendkey "+ch,.20)
    for marker in (b"P",b"Q",b"O"):
     off=len(dbg());command(sock,"sendkey ret",.22)
     wait_for_marker(DEBUG,marker,after=off,timeout=70)
    # INST_USER_MORE defaults to NO (g_choice=1). Sending Right here
    # wraps to YES and starts a second username page. Keep the secure
    # default: finish with Enter without toggling the selection.
    off=len(dbg());command(sock,"sendkey ret",.28)
    wait_for_marker(DEBUG,b"Z",after=off,timeout=70)
    # Blank-password user created in this disposable guest is auto-logged in.
    # FalconOS emits aUH rather than lockscreen W; pressing Enter a second
    # time here is an unintended desktop action.
    wait_for_marker(DEBUG,b"aUH",after=off,timeout=70)
    command(sock,"sendkey esc",.15)
    command(sock,"sendkey f2",.55)
    command(sock,"sendkey right",.18)
    off=len(dbg());command(sock,"sendkey ret",.55)
    wait_for_marker(DEBUG,b"zO",after=off,timeout=65)  # app 14 Browser
    time.sleep(.6)
    startup=OUT/"Falco-browser-actual-start.ppm"
    screenshot(sock,startup)
    ppm_to_png(startup,OUT/"Falco-browser-actual-start.png")
    for idx,site in enumerate(SITES,1):
     if guest.poll() is not None:raise RuntimeError("QEMU terminated mid-test")
     target=OUT/f"{idx:02d}-{site.replace('.','_')}.ppm"
     print(f"TEST_SITE {idx:02d}/{len(SITES)} https://{site}/",flush=True)
     try:
      type_url(sock,site)
      off=len(dbg())
      command(sock,"sendkey ret",.1)
      # Browser emits b at load entry, Y at verified result completion.
      # Even a network error without terminal marker gets a screenshot.
      limit=time.monotonic()+32
      state="no_marker"
      while time.monotonic()<limit:
       recent=dbg()[off:]
       if b"bY" in recent:
        state="success";break
       if b"bN" in recent:
        state="failed";break
       # If the guest already rejected the HTTPS host, catch next iteration.
       if len(recent)>=2 and b"b" in recent and b"Y" not in recent and b"N" in recent:
        state="failed";break
       if guest.poll() is not None:
        state="emulator";break
       time.sleep(.18)
      time.sleep(1.0)
      screenshot(sock,target)
      ppm_to_png(target,OUT/(target.stem+".png"))
      # Host proxy strips CSS/JS/images and supplies semantic text;
      # a verified fetch is incomplete visual compatibility at best.
      category="Yarim" if state=="success" else "Berbat"
      critique=("Metin yuklendi; orijinal resimler, CSS ve JS kayip."
                if state=="success" else
                "HTTPS/yukleme basarisiz veya bos ekran; tasarim gorunmuyor.")
      results.append({"site":site,"category":category,"load":state,
         "critique":critique,"screenshot":str(OUT/(target.stem+".png"))})
      print(f"SITE_RESULT {idx} {site} {category} {state}",flush=True)
     except Exception as exc:
      # Continue with every requested domain; never omit failures.
      try:
       screenshot(sock,target);ppm_to_png(target,OUT/(target.stem+".png"))
       img=str(OUT/(target.stem+".png"))
      except Exception:img=""
      results.append({"site":site,"category":"Berbat","load":"exception",
             "critique":"QEMU gezintisinde hata: "+str(exc)[:85],
             "screenshot":img})
      print(f"SITE_ERROR {idx} {site} {exc!r}",flush=True)
 except Exception as exc:
  print("FALCO_AUDIT_ERROR",repr(exc),flush=True)
  # Even startup failure produces a record of what was NOT tested.
  for site in SITES[len(results):]:
   results.append({"site":site,"category":"Berbat","load":"not_tested",
      "critique":"VM baslatma/gezinti basarisiz; site gercekte test edilmedi.",
      "screenshot":""})
  raise
 finally:
  if guest:
   guest.terminate()
   try:guest.wait(timeout=4)
   except subprocess.TimeoutExpired:guest.kill();guest.wait()
  if gateway:
   gateway.terminate()
   try:gateway.wait(timeout=3)
   except subprocess.TimeoutExpired:gateway.kill();gateway.wait()
  if logfile:logfile.close()
  counts=render(results)
  REPORT.write_text(json.dumps({"mode":"real QEMU FalconOS native Browser","date":"2026-10-15",
       "seed":20261015,"sample":SITES,"counts":counts,"results":results},
       ensure_ascii=False,indent=2),encoding="utf8")
  print("FALCO_COUNTS",json.dumps(counts),flush=True)
  print("FALCO_COMPOSITE",COMPOSITE,flush=True)
if __name__=="__main__":main()
