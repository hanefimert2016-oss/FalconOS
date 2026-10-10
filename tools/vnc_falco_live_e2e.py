#!/usr/bin/env python3
"""Proof that Falco's own native guest framebuffer displays falconos.tech.
Never substitute GitHub docs, cached pages, fake HTML or a host Chrome render.
Native guest BearSSL and opt-in host CA-verified HTTPS are separate modes.
Host-assisted mode has a plaintext local VM link, explicitly labeled.
"""
import argparse,json,socket,subprocess,sys,time,traceback,urllib.request
from pathlib import Path
from vncdotool import api
from vnc_real_input_audit import k,t,pause,shot
import vnc_real_input_audit as v
B=Path("build").absolute();B.mkdir(exist_ok=True)
LOG=B/"falco-live-guest.log"
RESULT=B/"falco-live-results.json"
STATE={"result":"RUNNING","mode":None,"site":"https://falconos.tech/","checks":[]}
def events():return LOG.read_bytes() if LOG.exists() else b""
def wait(event,off=0,seconds=80):
    stop=time.monotonic()+seconds
    while time.monotonic()<stop:
        if event in events()[off:]:return
        time.sleep(.2)
    raise AssertionError(f"Guest marker {event!r} not seen; tail {events()[-220:]!r}")
def probe():
    with urllib.request.urlopen("http://127.0.0.1:18444/fetch/falconos.tech/",timeout=24) as resp:
        b=resp.read(2700).decode("utf-8","replace")
        if (resp.status!=200 or resp.headers.get("X-Falcon-Host-HTTPS-Verified")!="yes"
                or "FalconOS" not in b or "Contour" not in b):
            raise AssertionError("Host preflight returned non-site content")
        STATE["checks"].append("Host CA-verified HTTPS response contains authentic FalconOS+Contour")

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--via-host",action="store_true")
    args=ap.parse_args()
    STATE["mode"]="host-ca-verified with plain VM hop" if args.via_host else "guest-native BearSSL"
    v.LOG=LOG
    if LOG.exists():LOG.unlink()
    gateway=None
    if args.via_host:
        gf=(B/"falco-live-gateway.log").open("wb")
        gateway=subprocess.Popen([sys.executable,"-u","tools/falcon_https_gateway.py",
                                  "--bind","127.0.0.1"],stdout=gf,stderr=subprocess.STDOUT)
        for _ in range(60):
            try:
                with socket.create_connection(("127.0.0.1",18444),timeout=.3):break
            except OSError:time.sleep(.2)
        else:raise TimeoutError("Host CA gateway did not bind")
        probe()
    cmd=["qemu-system-x86_64","-accel","tcg","-cpu","max","-m","1024","-smp","1",
         "-boot","d","-cdrom","build/FalconOS.iso","-display","none","-vga","std",
         "-netdev","user,id=net","-device","rtl8139,netdev=net",
         "-vnc","127.0.0.1:9","-monitor","none","-serial","none",
         "-debugcon",f"file:{LOG}","-global","isa-debugcon.iobase=0xe9","-no-reboot"]
    with (B/"falco-live-qemu-stderr.log").open("wb") as err:
        guest=subprocess.Popen(cmd,stdout=subprocess.DEVNULL,stderr=err)
        try:
            for _ in range(120):
                try:
                    with socket.create_connection(("127.0.0.1",5909),timeout=.3):break
                except OSError:time.sleep(.25)
            else:raise RuntimeError("Falco VNC display unavailable")
            with api.connect("127.0.0.1::5909",timeout=24) as c:
                wait(b"L",seconds=100)
                for m in (b"T",b"A",b"K",b"D",b"U"):
                    off=len(events());k(c,"enter");wait(m,off)
                t(c,"falcon")
                for m in (b"P",b"Q",b"O"):
                    off=len(events());k(c,"enter");wait(m,off)
                k(c,"right")
                off=len(events());k(c,"enter");wait(b"W",off)
                off=len(events());k(c,"enter");wait(b"H",off)
                k(c,"esc");k(c,"f2")
                t(c,"Falco")
                off=len(events());k(c,"enter")
                wait(b"zNn1",off)
                STATE["checks"].append("Real Falco native application opened")
                pause(1)
                shot(c,"FalconOS-Falco-Before-Navigation")
                off=len(events())
                k(c,"f7" if args.via_host else "f6")
                wait(b"fS",off,seconds=115) # NOT merely a 200 or generic HTTP result
                wait(b"fY",off,seconds=115)
                pause(2)
                shot(c,"FalconOS-Falco-Live-falconos-tech")
                STATE["checks"].append("Real Falco rendered CA-verified FalconOS Contour HTML text")
                if args.via_host:
                    gateway_log=(B/"falco-live-gateway.log").read_text(errors="replace")
                    if "/fetch/falconos.tech/" not in gateway_log or " 200 " not in gateway_log:
                        raise AssertionError("Host gateway has no real falconos.tech 200 log")
                STATE["result"]="PASS"
        finally:
            guest.terminate()
            try:guest.wait(timeout=5)
            except subprocess.TimeoutExpired:guest.kill()
            if gateway:
                gateway.terminate()
                try:gateway.wait(timeout=4)
                except subprocess.TimeoutExpired:gateway.kill()
                gf.close()
            RESULT.write_text(json.dumps(STATE,ensure_ascii=False,indent=2))
if __name__=="__main__":
    try:main()
    except Exception as e:
        STATE["result"]="FAIL";STATE["error"]=str(e)
        RESULT.write_text(json.dumps(STATE,ensure_ascii=False,indent=2))
        traceback.print_exc()
        raise
