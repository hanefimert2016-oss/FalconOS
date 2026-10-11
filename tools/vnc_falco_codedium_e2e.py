#!/usr/bin/env python3
"""Photograph the actual native FalconOS desktop, Falco and Codedium in QEMU."""
import json,socket,subprocess,time,traceback
from pathlib import Path
from vncdotool import api
from vnc_real_input_audit import k,t,pause,shot
import vnc_real_input_audit as v
B=Path("build").absolute();B.mkdir(exist_ok=True)
LOG=B/"falco-codedium-review-guest.log"
REPORT=B/"falco-codedium-review-result.json"
STATE={"result":"RUNNING","screenshots":[],"checks":[]}
def wait(marker,off=0,seconds=80):
    end=time.monotonic()+seconds
    body=b""
    while time.monotonic()<end:
        body=LOG.read_bytes() if LOG.exists() else b""
        if marker in body[off:]:return
        pause(.2)
    raise AssertionError(f"Missing real guest marker {marker!r}; tail={body[-180:]!r}")
def snap(c,label):
    shot(c,label)
    STATE["screenshots"].append(label+".png")
def main():
    v.LOG=LOG
    if LOG.exists():LOG.unlink()
    cmd=["qemu-system-x86_64","-accel","tcg","-m","1024","-smp","1",
         "-boot","d","-cdrom","build/FalconOS.iso","-display","none",
         "-vga","std","-global","VGA.vgamem_mb=256",
         "-vnc","127.0.0.1:11","-monitor","none","-serial","none",
         "-debugcon","file:"+str(LOG),"-global","isa-debugcon.iobase=0xe9","-no-reboot"]
    with (B/"falco-codedium-review-qemu.log").open("wb") as err:
        guest=subprocess.Popen(cmd,stdout=subprocess.DEVNULL,stderr=err)
        try:
            for _ in range(120):
                try:
                    with socket.create_connection(("127.0.0.1",5911),timeout=.3):break
                except OSError:
                    if guest.poll() is not None:raise RuntimeError("QEMU exited before VNC")
                    pause(.25)
            else:raise TimeoutError("No VNC server for QEMU")
            with api.connect("127.0.0.1::5911",timeout=25) as c:
                wait(b"L",seconds=90)
                for marker in (b"T",b"A",b"K",b"D",b"U"):
                    off=LOG.stat().st_size;k(c,"enter");wait(marker,off)
                t(c,"falcon")
                for marker in (b"P",b"Q",b"O"):
                    off=LOG.stat().st_size;k(c,"enter");wait(marker,off)
                off=LOG.stat().st_size;k(c,"enter")
                wait(b"aU",off,60);wait(b"H",off,60)
                k(c,"esc");pause(.6)
                snap(c,"FalconOS-Review-Desktop")
                k(c,"f2");t(c,"Falco")
                off=LOG.stat().st_size;k(c,"enter")
                wait(b"zNn1",off,60)
                # zN proves application selection only; gN is emitted AFTER
                # the native framebuffer is fully drawn with launcher closed.
                wait(b"gN",off,65)
                pause(1.3)
                snap(c,"FalconOS-Review-Falco")
                STATE["checks"].append("Real Falco rendered in QEMU via PS/2 input")
                k(c,"f2");t(c,"Codedium")
                off=LOG.stat().st_size;k(c,"enter")
                wait(b"zS",off,60)
                wait(b"gS",off,65)
                pause(1.3)
                snap(c,"FalconOS-Review-Codedium")
                STATE["checks"].append("Real Codedium editor rendered in QEMU")
                STATE["result"]="PASS"
        finally:
            guest.terminate()
            try:guest.wait(timeout=5)
            except subprocess.TimeoutExpired:guest.kill()
            REPORT.write_text(json.dumps(STATE,indent=2))
if __name__=="__main__":
    try:main()
    except Exception as err:
        STATE["result"]="FAIL";STATE["error"]=str(err)
        REPORT.write_text(json.dumps(STATE,indent=2))
        traceback.print_exc()
        raise
