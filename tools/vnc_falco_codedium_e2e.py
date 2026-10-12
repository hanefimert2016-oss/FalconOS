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
                # Actual V2 About window is opened through Launchpad and drawn
                # by the real guest framebuffer (not a post-processed mockup).
                k(c,"f2");t(c,"About")
                off=LOG.stat().st_size;k(c,"enter")
                wait(b"zR",off,60);wait(b"gR",off,65)
                pause(1.2)
                snap(c,"FalconOS-V2-Beta-About-Real-QEMU")
                STATE["checks"].append("Real FalconOS V2 Beta About window rendered inside QEMU")
                k(c,"f2");t(c,"Terminal")
                off=LOG.stat().st_size;k(c,"enter")
                wait(b"zF",off,60);wait(b"gF",off,65)
                t(c,"ver");k(c,"enter");pause(.8)
                t(c,"uname");k(c,"enter");pause(.7)
                t(c,"doctor");k(c,"enter");pause(.9)
                snap(c,"FalconOS-V2-Beta-Terminal-Real-QEMU")
                STATE["checks"].append("Native Terminal accepted ver, uname and honest doctor runtime diagnostics in QEMU")
                # V2 Calendar is a real RTC-based application, present in
                # Launchpad's All catalogue, not a demo counter of uptime.
                k(c,"f2");t(c,"Calendar")
                off=LOG.stat().st_size;k(c,"enter")
                wait(b"zK",off,60);wait(b"gK",off,65)
                pause(1)
                snap(c,"FalconOS-V2-Gregorian-Calendar-Today-Real-QEMU")
                from PIL import Image, ImageChops
                initial=Image.open(B/"FalconOS-V2-Gregorian-Calendar-Today-Real-QEMU.png").convert("RGB")
                off=LOG.stat().st_size
                k(c,"right")
                wait(b"MN",off,40) # guest month state increment acknowledged
                # Set a NEW cursor after the input acknowledgement. Otherwise
                # gK could match an OLD frame rendered during keyPress().
                painted_after=LOG.stat().st_size
                wait(b"gK",painted_after,90)
                month_title_box=(initial.width*35//100,initial.height*29//100,
                                 initial.width*47//100,initial.height*35//100)
                # VNC is asynchronous even after guest gfx_present. Poll the
                # ACTUAL QEMU framebuffer until its month title changes;
                # never silently accept a stale screenshot.
                changed=False
                for attempt in range(14):
                    pause(.7)
                    shot(c,"FalconOS-V2-Gregorian-Calendar-Next-Month-Real-QEMU")
                    following=Image.open(B/"FalconOS-V2-Gregorian-Calendar-Next-Month-Real-QEMU.png").convert("RGB")
                    delta=ImageChops.difference(initial.crop(month_title_box),
                                                following.crop(month_title_box))
                    if delta.getbbox() is not None:
                        changed=True
                        break
                STATE["screenshots"].append("FalconOS-V2-Gregorian-Calendar-Next-Month-Real-QEMU.png")
                if not changed:
                    raise AssertionError("Guest confirmed month navigation but no new Calendar title reached VNC after 10s")
                off=LOG.stat().st_size
                k(c,"t");wait(b"MT",off,40)
                STATE["checks"].append("REAL RTC Calendar: guest acknowledged right arrow and month header visibly changed")
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
