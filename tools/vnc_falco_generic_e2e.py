#!/usr/bin/env python3
"""Live QEMU native Falco static HTML visual test (public example.org).
Real VNC keyboard + real host certificate HTTPS + native C software renderer.
This is NOT a standards-complete browser test or proof of JavaScript/CSS.
"""
import json,socket,subprocess,sys,time,traceback
from pathlib import Path
from vncdotool import api
from vnc_real_input_audit import k,t,pause,shot
import vnc_real_input_audit as helper
B=Path("build").absolute();B.mkdir(exist_ok=True)
LOG=B/"falco-generic-guest.log";REPORT=B/"falco-generic-guest-results.json"
ST={"status":"RUNNING","origin":"https://example.org/","engine":"FalconOS C custom renderer"}
def trace():return LOG.read_bytes() if LOG.exists() else b""
def wait(marker,start=0,seconds=80):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        if marker in trace()[start:]:return
        time.sleep(.17)
    raise AssertionError(f"No genuine QEMU marker {marker!r}: {trace()[-300:]!r}")
def main():
    if LOG.exists():LOG.unlink()
    helper.LOG=LOG
    hostlog=(B/"falco-generic-host.log").open("wb")
    gateway=subprocess.Popen([sys.executable,"-u","tools/falco_native_gateway.py","--bind","127.0.0.1"],
                             stdout=hostlog,stderr=subprocess.STDOUT)
    qemu=None
    try:
        for i in range(60):
            try:
                with socket.create_connection(("127.0.0.1",18446),timeout=.4):break
            except OSError:time.sleep(.15)
        else:raise RuntimeError("Native HTML companion failed")
        cmd=["qemu-system-x86_64","-accel","tcg","-m","1024",
             "-cdrom","build/FalconOS.iso","-boot","d",
             "-netdev","user,id=net","-device","rtl8139,netdev=net",
             "-vga","std","-global","VGA.vgamem_mb=256",
             "-display","none","-vnc","127.0.0.1:6",
             "-monitor","none","-serial","none",
             "-debugcon",f"file:{LOG}","-global","isa-debugcon.iobase=0xe9",
             "-no-reboot"]
        with (B/"falco-generic-qemu.log").open("wb") as out:
            qemu=subprocess.Popen(cmd,stdout=subprocess.DEVNULL,stderr=out)
            for i in range(120):
                try:
                    with socket.create_connection(("127.0.0.1",5906),timeout=.3):break
                except OSError:time.sleep(.2)
            else:raise RuntimeError("VNC guest not reachable")
            with api.connect("127.0.0.1::5906",timeout=28) as c:
                wait(b"L",seconds=90)
                for evt in (b"T",b"A",b"K",b"D",b"U"):
                    off=len(trace());k(c,"enter");wait(evt,off,35)
                t(c,"falcon")
                for evt in (b"P",b"Q",b"O"):
                    off=len(trace());k(c,"enter");wait(evt,off,35)
                off=len(trace());k(c,"enter")
                wait(b"aU",off,55);wait(b"H",off,55)
                k(c,"f2");t(c,"Falco")
                off=len(trace());k(c,"enter");wait(b"zN",off,65)
                k(c,"f4")
                t(c,"example.org")
                off=len(trace());k(c,"f9")
                wait(b"vY",off,110);wait(b"nG",off,110)
                pause(1.5)
                shot(c,"FalconOS-Falco-Generic-Example-org-REAL")
                ST["status"]="PASS"
                ST["checks"]=["Actual user-typed public HTTPS domain",
                              "Host TLS+DNS validated static HTML",
                              "FalconOS guest created full resolution native HTML reader view"]
    finally:
        if qemu:
            qemu.terminate()
            try:qemu.wait(timeout=4)
            except subprocess.TimeoutExpired:qemu.kill()
        gateway.terminate()
        try:gateway.wait(timeout=4)
        except subprocess.TimeoutExpired:gateway.kill()
        hostlog.close()
        REPORT.write_text(json.dumps(ST,indent=2))
if __name__=="__main__":
    try:main()
    except Exception as e:
        ST["status"]="FAIL";ST["error"]=str(e)
        REPORT.write_text(json.dumps(ST,indent=2))
        traceback.print_exc()
        raise
