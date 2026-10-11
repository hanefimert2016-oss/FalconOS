#!/usr/bin/env python3
"""Actual .app.pkg double click through FalconOS own Files app.
Human-equivalent RFB keyboard+PS2 mouse, no QMP/HMP injection.
Codedium F7 exports a REAL valid package; Files sees the SAME SHFS inode,
then a real double click executes it in the guest Terminal.
"""
import json,re,socket,subprocess,time,traceback
from pathlib import Path
from vncdotool import api
from vnc_real_input_audit import k,t,pause,shot
import vnc_real_input_audit as v

ROOT=Path("build").absolute();ROOT.mkdir(exist_ok=True)
LOG=ROOT/"app-pkg-qemu-debug.log";OUT=ROOT/"app-pkg-results.json"
STATUS={"status":"RUNNING","format":"FAPP/1","tests":[]}
def data():return LOG.read_bytes() if LOG.exists() else b""
def wait(mark,since=0,sec=75):
    stop=time.monotonic()+sec
    while time.monotonic()<stop:
        if mark in data()[since:]:return
        pause(.14)
    raise AssertionError(f"Missing marker {mark!r}; log tail={data()[-240:]!r}")
def guest_mouse_pos():
    samples=re.findall(rb"p(\d+),(\d+);",data())
    return tuple(map(int,samples[-1])) if samples else None
def click(c):
    c.mouseDown(1);pause(.28);c.mouseUp(1);pause(.29)
def point(c,target=(1090,456)):
    x,y=380,240
    c.mouseMove(x,y);pause(.25)
    for a,b in [(405,263),(440,285),(465,307)]:
        x,y=a,b;c.mouseMove(x,y);pause(.2)
    if not guest_mouse_pos():
        click(c);c.mouseMove(x+19,y+19);pause(.5);x+=19;y+=19
    start=time.monotonic()
    for i in range(120):
        pos=guest_mouse_pos()
        if pos is None:raise AssertionError("PS2 pointer did not report coordinates")
        dx,dy=target[0]-pos[0],target[1]-pos[1]
        if abs(dx)<=11 and abs(dy)<=11:
            STATUS["mouse_target"]=target
            STATUS["mouse_guest_pos"]=pos
            return
        sx=max(-24,min(24,round(dx/(2.5 if abs(dx)>24 else 1.5))))
        sy=max(-24,min(24,round(dy/(2.5 if abs(dy)>24 else 1.5))))
        if dx and not sx:sx=1 if dx>0 else -1
        if dy and not sy:sy=1 if dy>0 else -1
        x=max(8,min(2530,x+sx));y=max(8,min(1410,y+sy))
        c.mouseMove(x,y);pause(.16)
        if time.monotonic()-start>40:break
    raise AssertionError("Could not move PS2 pointer to .app.pkg file row: "+str(guest_mouse_pos()))
def main():
    if LOG.exists():LOG.unlink()
    v.LOG=LOG
    args=["qemu-system-x86_64","-accel","tcg","-m","1024","-smp","1",
          "-cdrom","build/FalconOS.iso","-boot","d",
          "-display","none","-vga","std","-global","VGA.vgamem_mb=256",
          "-netdev","user,id=lan","-device","rtl8139,netdev=lan",
          "-vnc","127.0.0.1:7","-monitor","none","-serial","none",
          "-debugcon",f"file:{LOG}","-global","isa-debugcon.iobase=0xe9",
          "-no-reboot"]
    with (ROOT/"app-pkg-qemu-stderr.log").open("wb") as logs:
        qemu=subprocess.Popen(args,stdout=subprocess.DEVNULL,stderr=logs)
        try:
            for _ in range(100):
                try:
                    with socket.create_connection(("127.0.0.1",5907),timeout=.3):
                        break
                except OSError:pause(.18)
            else:raise RuntimeError("VNC not available")
            with api.connect("127.0.0.1::5907",timeout=35) as c:
                wait(b"L",sec=110)
                for ch in (b"T",b"A",b"K",b"D",b"U"):
                    mark=len(data());k(c,"enter");wait(ch,mark,45)
                t(c,"falcon")
                for ch in (b"P",b"Q",b"O"):
                    mark=len(data());k(c,"enter");wait(ch,mark,50)
                mark=len(data());k(c,"enter")
                wait(b"aU",mark,65);wait(b"H",mark,70)
                k(c,"esc");pause(.35)
                k(c,"f2");k(c,"tab");t(c,"Codedium") # Codedium is in System, not Essentials
                mark=len(data());k(c,"enter");wait(b"zSn1",mark,70)
                mark=len(data());k(c,"f7");wait(b"E",mark,45)
                STATUS["tests"].append("Real Codedium exported native FAPP/1 Desktop/code.app.pkg")
                k(c,"f2");t(c,"Files")
                mark=len(data());k(c,"enter");wait(b"zBn2",mark,70)
                k(c,"right");k(c,"right") # Files: All->Home->Desktop
                pause(1.1)
                shot(c,"FalconOS-APP-PKG-Desktop-Before-DoubleClick")
                point(c)
                mark=len(data())
                click(c)
                pause(.26)
                click(c)
                wait(b"dC",mark,45)
                wait(b"dY",mark,60)
                wait(b"zTn3",mark,55)
                STATUS["tests"].append("Two real PS2 mouse clicks on selected .app.pkg launched a dedicated FalconOS application window")
                pause(1.1)
                shot(c,"FalconOS-APP-PKG-DoubleClick-Running")
                STATUS["status"]="PASS"
        finally:
            qemu.terminate()
            try:qemu.wait(timeout=5)
            except subprocess.TimeoutExpired:qemu.kill()
            OUT.write_text(json.dumps(STATUS,ensure_ascii=False,indent=2))
if __name__=="__main__":
    try:main()
    except Exception as err:
        STATUS["status"]="FAIL";STATUS["error"]=str(err)
        OUT.write_text(json.dumps(STATUS,ensure_ascii=False,indent=2))
        traceback.print_exc()
        raise
