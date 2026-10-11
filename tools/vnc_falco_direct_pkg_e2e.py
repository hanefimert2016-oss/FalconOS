#!/usr/bin/env python3
"""Real GUI Falco/Browser direct HTTPS .app.pkg URL -> native Downloads file.

Guest uses its own keyboard/RTL8139/HTTP/FAPP parser and file system.
Host Python stdlib performs real certificate+hostname checked public HTTPS.
No fake files or staged package, no Chromium, no QMP keyboard injection.
"""
import json,subprocess,sys,socket,time,traceback
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
                for mark in (b"T",b"A",b"K",b"D",b"U"):
                    off=len(data());k(c,"enter");wait(mark,off,40)
                t(c,"falcon")
                for mark in (b"P",b"Q",b"O"):
                    off=len(data());k(c,"enter");wait(mark,off,45)
                off=len(data());k(c,"enter");wait(b"aU",off,55);wait(b"H",off,55)
                k(c,"esc")
                k(c,"f2");t(c,"Browser")
                off=len(data());k(c,"enter");wait(b"zOn1",off,65)
                k(c,"f4")
                # Browser address typed via guest's real PS/2 keyboard.
                t(c,URL)
                shot(c,"FalconOS-HTTPS-URL-Before-Enter")
                off=len(data());k(c,"enter")
                pause(2.0)
                shot(c,"FalconOS-HTTPS-URL-After-Enter")
                wait(b"fD",off,95)
                STATE["checks"].append("Real HTTPS URL typed in guest Browser and saved in guest Downloads")
                pause(1.5)
                shot(c,"FalconOS-Falco-Direct-HTTPS-PKG-Downloaded")
                # Open Downloads in actual Files window. Does not execute on download.
                k(c,"f2");t(c,"Files")
                off=len(data());k(c,"enter");wait(b"zBn2",off,65)
                for _ in range(3):k(c,"right")
                pause(1.8)
                shot(c,"FalconOS-Falco-Direct-HTTPS-PKG-in-Files")
                STATE["checks"].append("Downloaded .app.pkg is visible in the FalconOS filesystem")
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
