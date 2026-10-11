#!/usr/bin/env python3
"""Falco actual HTTPS .app.pkg internet download into SHFS /downloads.
No host-side app execution, no mock response. Uses the public GitHub release,
TLS verified by the actual Falco native host gateway. VNC keyboard only.
"""
import json,socket,subprocess,sys,time,traceback
from pathlib import Path
from vncdotool import api
from vnc_real_input_audit import k,t,pause,shot
import vnc_real_input_audit as v
import vnc_app_pkg_doubleclick_e2e as pkg_input
B=Path("build").absolute();B.mkdir(exist_ok=True)
LOG=B/"falco-app-download-debug.log"
REPORT=B/"falco-app-download-results.json"
URL=("https://github.com/hanefimert2016-oss/FalconOS-Marketplace/releases/"
     "download/app-hello-world-v1.0.0/hello-world-v1.0.0.app.pkg")
RESULT={"status":"RUNNING","release_url":URL,"checks":[]}
def data():return LOG.read_bytes() if LOG.exists() else b""
def wait(marker,off=0,sec=100):
    end=time.monotonic()+sec
    while time.monotonic()<end:
        if marker in data()[off:]:return
        pause(.16)
    raise AssertionError(f"Guest marker {marker!r} missing: {data()[-190:]!r}")
def main():
    if LOG.exists():LOG.unlink()
    v.LOG=LOG
    hostlog=(B/"falco-app-https-host.log").open("wb")
    gateway=subprocess.Popen([sys.executable,"-u","tools/falcon_https_gateway.py",
                              "--bind","127.0.0.1"],stdout=hostlog,stderr=subprocess.STDOUT)
    proc=None;fd=None
    try:
        for _ in range(100):
            try:
                with socket.create_connection(("127.0.0.1",18444),timeout=.2):break
            except OSError:pause(.14)
        else:raise RuntimeError("CA-verified HTTPS companion failed to start")
        args=["qemu-system-x86_64","-accel","tcg","-m","1024",
              "-cdrom","build/FalconOS.iso","-boot","d",
              "-display","none","-vga","std","-global","VGA.vgamem_mb=256",
              "-netdev","user,id=network","-device","rtl8139,netdev=network",
              "-vnc","127.0.0.1:6","-monitor","none","-serial","none",
              "-debugcon",f"file:{LOG}","-global","isa-debugcon.iobase=0xe9",
              "-no-reboot"]
        fd=(B/"falco-app-download-qemu-stderr.log").open("wb")
        proc=subprocess.Popen(args,stderr=fd,stdout=subprocess.DEVNULL)
        for _ in range(110):
            try:
                with socket.create_connection(("127.0.0.1",5906),timeout=.2):break
            except OSError:pause(.18)
        else:raise RuntimeError("VNC unavailable")
        with api.connect("127.0.0.1::5906",timeout=35) as c:
            wait(b"L",sec=110)
            for token in (b"T",b"A",b"K",b"D",b"U"):
                # For faithful VNC release-URL typing select US-QWERTY at
                # keyboard setup. Human FalconOS defaults are untouched.
                if token==b"D":
                    k(c,"right");k(c,"right")
                at=len(data());k(c,"enter");wait(token,at,45)
            t(c,"falcon")
            for token in (b"P",b"Q",b"O"):
                at=len(data());k(c,"enter");wait(token,at,55)
            at=len(data());k(c,"enter");wait(b"aU",at,65);wait(b"H",at,65)
            k(c,"esc");pause(.6)
            k(c,"f2");t(c,"Falco")
            at=len(data());k(c,"enter");wait(b"zNn1",at,60)
            # Ctrl+L clears existing search box; pasting a real release URL
            # is modeled by ordinary VNC keyboard presses, not guest memory.
            c.keyDown("ctrl");k(c,"l");c.keyUp("ctrl")
            t(c,URL)
            pause(.7)
            shot(c,"FalconOS-Falco-Entered-AppPkg-URL")
            at=len(data());k(c,"enter")
            pause(2.5)
            shot(c,"FalconOS-Falco-AppPkg-Download-Status")
            wait(b"dW",at,95)
            RESULT["checks"].append("Falco fetched real GitHub release over CA-verified HTTPS and saved FAPP/1 to Downloads")
            pause(.7)
            shot(c,"FalconOS-Falco-Real-HTTPS-AppPkg-Downloaded")
            k(c,"f2");t(c,"Files")
            at=len(data());k(c,"enter");wait(b"zBn2",at,65)
            for _ in range(3):k(c,"right")  # Files: Downloads tab
            pause(1)
            shot(c,"FalconOS-Downloads-Real-Internet-AppPkg")
            RESULT["checks"].append("FalconOS Files showed the real .app.pkg internet download")
            pkg_input.LOG=LOG
            pkg_input.STATUS=RESULT
            pkg_input.point(c,target=(1090,534))
            at=len(data())
            pkg_input.click(c);wait(b"dS",at,35);pause(.26)
            pkg_input.click(c)
            wait(b"dC",at,50);wait(b"dY",at,50);wait(b"zTn3",at,70)
            pause(1.2)
            shot(c,"FalconOS-Downloaded-AppPkg-DoubleClick-Running")
            RESULT["checks"].append("Real internet-downloaded FAPP/1 executed via two native PS2 mouse clicks in its own window")
        RESULT["status"]="PASS"
    finally:
        if proc:
            proc.terminate()
            try:proc.wait(timeout=5)
            except subprocess.TimeoutExpired:proc.kill()
        if fd:fd.close()
        gateway.terminate()
        try:gateway.wait(timeout=5)
        except subprocess.TimeoutExpired:gateway.kill()
        hostlog.close()
        REPORT.write_text(json.dumps(RESULT,indent=2))
if __name__=="__main__":
    try:main()
    except Exception as e:
        RESULT["status"]="FAIL";RESULT["error"]=str(e)
        REPORT.write_text(json.dumps(RESULT,indent=2))
        traceback.print_exc()
        raise
