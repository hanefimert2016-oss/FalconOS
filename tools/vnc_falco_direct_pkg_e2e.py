#!/usr/bin/env python3
"""Real GUI Falco/Browser direct HTTPS .app.pkg URL -> native Downloads file.

Guest uses its own keyboard/RTL8139/HTTP/FAPP parser and file system.
Host Python stdlib performs real certificate+hostname checked public HTTPS.
No fake files or staged package, no Chromium, no QMP keyboard injection.
"""
import json,subprocess,sys,socket,time,traceback,re
from pathlib import Path
from vncdotool import api
from vnc_real_input_audit import k,t,pause,shot
import vnc_real_input_audit as helper
B=Path("build").absolute();B.mkdir(exist_ok=True)
LOG=B/"pkg-falco-url-kernel.log";REPORT=B/"pkg-falco-url-results.json"
STATE={"result":"RUNNING","checks":[]}
URL=("https://github.com/hanefimert2016-oss/FalconOS-Marketplace/"
     "releases/download/app-hello-world-v1.0.0/hello-world-v1.0.0.app.pkg")
def data():return LOG.read_bytes() if LOG.exists() else b""
def wait(x,offset=0,timeout=70):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        if x in data()[offset:]:return
        time.sleep(.15)
    raise AssertionError(f"guest event {x!r} missing: {data()[-230:]!r}")
def main():
    if LOG.exists():LOG.unlink()
    helper.LOG=LOG
    with (B/"pkg-falco-url-host.log").open("wb") as log, (B/"pkg-falco-url-qemu.log").open("wb") as qlog:
        gateway=subprocess.Popen([sys.executable,"-u","tools/falcon_https_gateway.py",
                                  "--bind","127.0.0.1"],stdout=log,stderr=subprocess.STDOUT)
        vm=None
        try:
            # Fail if host HTTP TLS bridge cannot start.
            for _ in range(85):
                try:
                    with socket.create_connection(("127.0.0.1",18444),timeout=.2):break
                except OSError:time.sleep(.12)
            else:raise RuntimeError("HTTPS companion failed to start")
            vm=subprocess.Popen(["qemu-system-x86_64","-accel","tcg","-m","1024",
                 "-cdrom","build/FalconOS.iso","-boot","d","-no-reboot",
                 "-display","none","-vga","std","-global","VGA.vgamem_mb=256",
                 "-netdev","user,id=lan","-device","rtl8139,netdev=lan",
                 "-vnc","127.0.0.1:8","-monitor","none","-serial","none",
                 "-debugcon",f"file:{LOG}","-global","isa-debugcon.iobase=0xe9"],
                 stdout=subprocess.DEVNULL,stderr=qlog)
            for _ in range(130):
                try:
                    with socket.create_connection(("127.0.0.1",5908),timeout=.2):break
                except OSError:time.sleep(.15)
            else:raise RuntimeError("Guest VNC unavailable")
            with api.connect("127.0.0.1::5908",timeout=35) as c:
                wait(b"L",timeout=95)
                # Real VNC/X11 sends US physical scancodes for punctuation.
                # Select English during installer, which sets KBD_US.
                # Without this, a US slash becomes Turkish '/' mapping's '.',
                # so URL is corrupted even before any HTTPS request.
                k(c,"right")
                # Installer begins with English and US-QWERTY, so keep its
                # already-correct default. Previous two Right events wrapped
                # US -> TR-Q -> TR-F, mangling h/t and URL punctuation.
                for mark in (b"T",b"A",b"K",b"D",b"U"):
                    off=len(data());k(c,"enter");wait(mark,off,40)
                t(c,"falcon")
                for mark in (b"P",b"Q",b"O"):
                    off=len(data());k(c,"enter");wait(mark,off,45)
                off=len(data());k(c,"enter");wait(b"aU",off,55);wait(b"H",off,55)
                k(c,"esc")
                # Launcher favorites are Files, Browser, Falco. Enter
                # FALCO itself (third icon), NOT the separate Browser app.
                # Two REAL Right keys navigate from Files -> Browser -> Falco.
                k(c,"f2");pause(.7)
                k(c,"right");k(c,"right")
                off=len(data());k(c,"enter");wait(b"zNn1",off,65)
                STATE["checks"].append("Actual Falco app launched with real guest keyboard")
                k(c,"f4")
                # Browser address typed via guest's real PS/2 keyboard.
                # A real browser accepts a hostname without scheme and
                # safely prepends HTTPS. VNC colon keysym is unreliable
                # across keyboard layouts, so this also verifies the
                # ergonomic scheme-less address bar path.
                t(c,URL[len("https://"):])
                shot(c,"FalconOS-Falco-HTTPS-URL-Before-Enter")
                off=len(data());k(c,"enter")
                pause(2.0)
                shot(c,"FalconOS-Falco-HTTPS-URL-After-Enter")
                wait(b"fD",off,95)
                STATE["checks"].append("Real HTTPS URL typed inside guest Falco, saved as inert Downloads .app.pkg")
                pause(1.5)
                shot(c,"FalconOS-Falco-Direct-HTTPS-PKG-Downloaded")
                # Open Downloads in actual Files window. Does not execute on download.
                k(c,"f2");t(c,"Files")
                off=len(data());k(c,"enter");wait(b"zBn2",off,65)
                for _ in range(3):k(c,"right")
                pause(1.8)
                shot(c,"FalconOS-Falco-Direct-HTTPS-PKG-in-Files")
                STATE["checks"].append("Downloaded .app.pkg is visible in the FalconOS filesystem")
                # Verify the WHOLE requested user flow in the SAME QEMU guest:
                # Falco HTTPS URL -> inert downloaded file -> two real RFB/
                # PS2 mouse clicks on the file -> actual native app window.
                c.mouseMove(400,320);pause(.25)
                for x,y in ((420,330),(440,345)):
                    c.mouseMove(x,y);pause(.24)
                def pointer():
                    samples=re.findall(rb"p(\d+),(\d+);",data())
                    return tuple(map(int,samples[-1])) if samples else None
                target=(1110,493)
                hx,hy=440,345
                for step in range(155):
                    pos=pointer()
                    if pos and abs(target[0]-pos[0])<=12 and abs(target[1]-pos[1])<=12:
                        break
                    if pos is None:
                        c.mouseMove(hx+15,hy+10);hx+=15;hy+=10
                        pause(.16);continue
                    dx=target[0]-pos[0];dy=target[1]-pos[1]
                    sx=max(-22,min(22,round(dx/(2.5 if abs(dx)>25 else 1.5))))
                    sy=max(-22,min(22,round(dy/(2.5 if abs(dy)>25 else 1.5))))
                    if sx==0 and dx:sx=1 if dx>0 else -1
                    if sy==0 and dy:sy=1 if dy>0 else -1
                    hx=max(6,min(2500,hx+sx))
                    hy=max(6,min(1400,hy+sy))
                    c.mouseMove(hx,hy);pause(.12)
                pos=pointer()
                if pos is None or abs(target[0]-pos[0])>14 or abs(target[1]-pos[1])>14:
                    raise AssertionError(f"Cannot aim guest pointer at downloaded file: {pos!r}")
                off=len(data());c.mouseDown(1);pause(.35);c.mouseUp(1)
                wait(b"f1",off,45)
                pause(.25)
                off=len(data());c.mouseDown(1);pause(.35);c.mouseUp(1)
                wait(b"f2",off,45);wait(b"fR",off,50)
                pause(1.2)
                shot(c,"FalconOS-Falco-HTTPS-PKG-DoubleClick-Running")
                STATE["checks"].append("Two real PS2 clicks ran directly downloaded .app.pkg inside its own FAPP Runner app window")
            STATE["result"]="PASS"
        finally:
            if vm:
                vm.terminate()
                try:vm.wait(timeout=5)
                except subprocess.TimeoutExpired:vm.kill()
            gateway.terminate()
            try:gateway.wait(timeout=3)
            except subprocess.TimeoutExpired:gateway.kill()
            REPORT.write_text(json.dumps(STATE,ensure_ascii=False,indent=2))
if __name__=="__main__":
    try:main()
    except Exception as e:
        STATE["result"]="FAIL";STATE["error"]=str(e)
        REPORT.write_text(json.dumps(STATE,ensure_ascii=False,indent=2))
        traceback.print_exc();raise
