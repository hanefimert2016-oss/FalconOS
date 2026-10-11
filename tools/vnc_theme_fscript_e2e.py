#!/usr/bin/env python3
"""Real guest E2E: auto GRUB handoff -> passwordless desktop -> native Theme
Creator and FSC/1 app, keyboard-only user actions via VNC, actual guest PNG.
Nothing injects HMP/QMP or paints a mock FalconOS screenshot.
"""
import json,socket,subprocess,time,traceback
from pathlib import Path
from vncdotool import api
from vnc_real_input_audit import k,t,pause,shot
import vnc_real_input_audit as helper
B=Path("build").absolute();B.mkdir(exist_ok=True)
LOG=B/"theme-fscript-guest.log"
REPORT=B/"theme-fscript-results.json"
STATE={"status":"RUNNING","checks":[]}
def data():return LOG.read_bytes() if LOG.exists() else b""
def wait(marker,off=0,timeout=65):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        if marker in data()[off:]:return
        time.sleep(.14)
    raise AssertionError(f"Guest {marker!r} missing from {data()[-250:]!r}")
def start():
    if LOG.exists():LOG.unlink()
    helper.LOG=LOG
    cmd=["qemu-system-x86_64","-accel","tcg","-m","1024",
         "-cdrom","build/FalconOS.iso","-boot","d",
         "-display","none","-vga","std","-global","VGA.vgamem_mb=256",
         "-netdev","user,id=net","-device","rtl8139,netdev=net",
         "-vnc","127.0.0.1:7","-serial","none","-monitor","none",
         "-debugcon",f"file:{LOG}","-global","isa-debugcon.iobase=0xe9",
         "-no-reboot"]
    out=(B/"theme-fscript-qemu.log").open("wb")
    proc=subprocess.Popen(cmd,stdout=subprocess.DEVNULL,stderr=out)
    for _ in range(85):
        try:
            with socket.create_connection(("127.0.0.1",5907),timeout=.25):
                return proc,out
        except OSError:pause(.16)
    raise RuntimeError("Guest VNC unavailable")
def open_app(c,name,marker):
    k(c,"f2");t(c,name)
    off=len(data());k(c,"enter");wait(marker.encode("ascii"),off,55)
def main():
    proc,out=start()
    try:
        with api.connect("127.0.0.1::5907",timeout=30) as c:
            wait(b"L",timeout=80)
            STATE["checks"].append("Zero-interaction GRUB auto-boot to real wizard")
            for mark in (b"T",b"A",b"K",b"D",b"U"):
                off=len(data());k(c,"enter");wait(mark,off)
            t(c,"falcon")
            for mark in (b"P",b"Q",b"O"):
                off=len(data());k(c,"enter");wait(mark,off)
            off=len(data());k(c,"enter");wait(b"aU",off);wait(b"H",off)
            STATE["checks"].append("No second account by default, no-password auto-login")
            open_app(c,"Theme","zU")
            wait(b"tR",0,65)
            shot(c,"FalconOS-Theme-Creator")
            off=len(data());k(c,"p");k(c,"e");wait(b"tE",off,25)
            STATE["checks"].append("Native Theme Creator renders and exports real .fth")
            shot(c,"FalconOS-Theme-Edited")
            open_app(c,"Codedium","zS")
            off=len(data());k(c,"f6");wait(b"FC",off,40)
            wait(b"FS",off,40)
            shot(c,"FalconOS-FScript-Running")
            STATE["checks"].append("CodeDium compiles FSC/1 sample to actual native FScript window")
            open_app(c,"Codedium","zS")
            off=len(data());k(c,"f7");wait(b"E",off,45)
            STATE["checks"].append("CodeDium exports two distinct files: installed .app.pkg and setup .setup.app.pkg")
            shot(c,"FalconOS-FScript-Editor")
            STATE["status"]="PASS"
    finally:
        proc.terminate()
        try:proc.wait(timeout=4)
        except subprocess.TimeoutExpired:proc.kill()
        out.close()
        REPORT.write_text(json.dumps(STATE,indent=2,ensure_ascii=False))
if __name__=="__main__":
    try:main()
    except Exception as e:
        STATE["status"]="FAIL";STATE["error"]=str(e)
        REPORT.write_text(json.dumps(STATE,indent=2,ensure_ascii=False))
        traceback.print_exc()
        raise
