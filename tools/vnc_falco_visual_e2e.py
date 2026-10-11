#!/usr/bin/env python3
"""Falco visual Chromium: VNC real keyboard -> QEMU native TCP -> host
Chromium CSS/JS render -> RGB332 image decoded by guest framebuffer.

No fake page or template; host must first prove HTTPS origin and title/body.
No QMP or HMP keyboard injection, no CAPTCHA or human-verification bypass.
"""
import io,json,os,socket,subprocess,sys,time,traceback,urllib.request
from pathlib import Path
from PIL import Image
from vncdotool import api
from vnc_real_input_audit import k,t,pause,shot
import vnc_real_input_audit as v
ROOT=Path("build").absolute()
ROOT.mkdir(exist_ok=True)
LOG=ROOT/"visual-guest.log"
REPORT=ROOT/"visual-results.json"
RESULT={"status":"RUNNING","site":"https://falconos.tech/",
        "rendering":"real Chromium CSS/JS (host), RGB332 framebuffer (guest)",
        "human_verification":"NOT claimed","steps":[]}

def trace():return LOG.read_bytes() if LOG.exists() else b""
def wait(marker,off=0,seconds=70):
    limit=time.monotonic()+seconds
    while time.monotonic()<limit:
        if marker in trace()[off:]:return
        time.sleep(.15)
    raise AssertionError(f"Guest {marker!r} not seen in {trace()[-220:]!r}")
def check_companion():
    stop=time.monotonic()+18
    while time.monotonic()<stop:
        try:
            with socket.create_connection(("127.0.0.1",18445),timeout=.5):
                return
        except OSError:time.sleep(.15)
    raise RuntimeError("Chromium companion did not bind")
def main():
    v.LOG=LOG
    if LOG.exists():LOG.unlink()
    gateway_log=(ROOT/"visual-host.log").open("wb")
    gateway=subprocess.Popen([sys.executable,"-u","tools/falco_visual_bridge.py","--bind","127.0.0.1"],
            stdout=gateway_log,stderr=subprocess.STDOUT)
    guest=None
    try:
        check_companion()
        with urllib.request.urlopen("http://127.0.0.1:18445/visual/falconos.tech/",timeout=65) as response:
            pixels=response.read()
            assert response.status==200
            assert response.headers.get("X-Falcon-Content-Verified")=="FalconOS-Contour"
            assert response.headers.get("X-Falcon-TLS")=="host-browser"
            assert len(pixels)==57600 and len(set(pixels))>45, (
                "Chromium must produce real diverse image pixels")
            RESULT["unique_rgb332_colors"]=len(set(pixels))
            RESULT["steps"].append("Real Chromium fetched, executed and rendered Contour HTTPS page")
            im=Image.frombytes("P",(320,180),pixels)
            # RGB332 palette from the exact guest pixel format.
            pal=[]
            for n in range(256):
                pal.extend([((n>>5)&7)*255//7,((n>>2)&7)*255//7,(n&3)*85])
            im.putpalette(pal)
            im.convert("RGB").save(ROOT/"Falco-Host-Chromium-Source.png")
        cmd=["qemu-system-x86_64","-accel","tcg","-cpu","max","-m","1024",
             "-smp","1","-cdrom","build/FalconOS.iso","-boot","d",
             "-display","none","-vga","std","-global","VGA.vgamem_mb=256",
             "-netdev","user,id=network","-device","rtl8139,netdev=network",
             "-vnc","127.0.0.1:9","-monitor","none","-serial","none",
             "-debugcon",f"file:{LOG}","-global","isa-debugcon.iobase=0xe9","-no-reboot"]
        with (ROOT/"visual-qemu-stderr.log").open("wb") as error:
            guest=subprocess.Popen(cmd,stderr=error,stdout=subprocess.DEVNULL)
            end=time.monotonic()+20
            while time.monotonic()<end:
                try:
                    with socket.create_connection(("127.0.0.1",5909),timeout=.3):
                        break
                except OSError:time.sleep(.15)
            else:raise RuntimeError("QEMU VNC not reachable")
            with api.connect("127.0.0.1::5909",timeout=25) as c:
                wait(b"L",seconds=80)
                for m in (b"T",b"A",b"K",b"D",b"U"):
                    off=len(trace());k(c,"enter");wait(m,off)
                t(c,"falcon")
                for m in (b"P",b"Q",b"O"):
                    off=len(trace());k(c,"enter");wait(m,off)
                k(c,"right")
                off=len(trace());k(c,"enter");wait(b"W",off)
                off=len(trace());k(c,"enter");wait(b"H",off)
                k(c,"esc")
                k(c,"f2");t(c,"Falco")
                off=len(trace());k(c,"enter");wait(b"zNn1",off)
                pause(.8);shot(c,"Falco-Before-Visual")
                off=len(trace());k(c,"f9")
                wait(b"vY",off,seconds=110)
                pause(1.7)
                shot(c,"Falco-Real-Chromium-Visual-Site")
                RESULT["steps"].append("Falco guest received 57,600 authenticated raster bytes and displayed them")
                # Objective framebuffer check: region containing guest raster
                # should have >45 colors and not be a flat text-only screen.
                with Image.open(ROOT/"Falco-Real-Chromium-Visual-Site.png") as picture:
                    if picture.width<1280:raise AssertionError("Unexpected guest framebuffer width")
                    colors=picture.convert("RGB").resize((160,90)).getcolors(160*90)
                    count=len(colors) if colors else 160*90
                    RESULT["guest_screen_colors"]=count
                    if count<55:raise AssertionError("Flat guest framebuffer - visual render not present")
                RESULT["status"]="PASS"
                print("PASS: Real Chromium HTTPS/CSS/JS page visible IN Falco QEMU framebuffer",flush=True)
    finally:
        if guest:
            guest.terminate()
            try:guest.wait(timeout=4)
            except subprocess.TimeoutExpired:guest.kill()
        gateway.terminate()
        try:gateway.wait(timeout=4)
        except subprocess.TimeoutExpired:gateway.kill()
        gateway_log.close()
        REPORT.write_text(json.dumps(RESULT,indent=2,ensure_ascii=False))
if __name__=="__main__":
    try:main()
    except Exception as e:
        RESULT["status"]="FAIL";RESULT["error"]=str(e)
        REPORT.write_text(json.dumps(RESULT,indent=2,ensure_ascii=False))
        traceback.print_exc()
        raise
