#!/usr/bin/env python3
"""Verify AppImage's virtual-only disk persists users and wallpaper across real
QEMU reboot. Read/write the guest solely through QEMU's ordinary VNC keyboard.
The test creates a NEW temporary 0xFA VM image and never opens host block devs.
"""
import json,os,socket,subprocess,sys,time,traceback
from pathlib import Path
from vncdotool import api
from vnc_real_input_audit import k,t,pause,shot
import vnc_real_input_audit as v
B=Path("build").absolute();B.mkdir(exist_ok=True)
DISK=B/"test-native-wallpaper-persistent.raw"
LOG=B/"persist-guest.log"
REPORT=B/"persist-results.json"
STATE={"status":"RUNNING","checks":[]}
def bytes_():return LOG.read_bytes() if LOG.exists() else b""
def wait(marker,start=0,timeout=90):
    stop=time.monotonic()+timeout
    while time.monotonic()<stop:
        if marker in bytes_()[start:]:return
        time.sleep(.15)
    raise AssertionError(f"Expected real {marker!r} event, got {bytes_()[-160:]!r}")
def start(port):
    if LOG.exists():LOG.unlink()
    v.LOG=LOG
    args=["qemu-system-x86_64","-accel","tcg","-m","1024",
          "-cdrom","build/FalconOS.iso","-boot","d",
          "-drive",f"file={DISK},if=ide,index=0,format=raw",
          "-netdev","user,id=net","-device","rtl8139,netdev=net",
          "-display","none","-vga","std","-global","VGA.vgamem_mb=256",
          "-vnc",f"127.0.0.1:{port}","-serial","none",
          "-monitor","none","-no-reboot","-debugcon",f"file:{LOG}",
          "-global","isa-debugcon.iobase=0xe9"]
    fd=(B/f"persist-qemu-{port}.log").open("wb")
    proc=subprocess.Popen(args,stdout=subprocess.DEVNULL,stderr=fd)
    for _ in range(125):
        try:
            with socket.create_connection(("127.0.0.1",5900+port),timeout=.3):
                return proc,fd
        except OSError:pause(.2)
    raise RuntimeError("QEMU VNC listener absent")
def close(proc,fd):
    proc.terminate()
    try:proc.wait(timeout=4)
    except subprocess.TimeoutExpired:proc.kill()
    fd.close()
def main():
    if DISK.exists():raise AssertionError("Refusing overwrite of test image")
    subprocess.run([sys.executable,"tools/make_safe_disk.py",
                    "--image",str(DISK),"--size","2G"],check=True)
    with DISK.open("rb") as fd:
        fd.seek(400)
        if fd.read(19)!=b"FALCONOS-VM-DISK-01":
            raise AssertionError("Virtual companion safety marker missing")
    proc,fd=start(6)
    try:
        with api.connect("127.0.0.1::5906",timeout=35) as c:
            wait(b"L")
            for mark in (b"T",b"A",b"K",b"D",b"U"):
                off=len(bytes_());k(c,"enter");wait(mark,off)
            t(c,"falcon")
            off=len(bytes_());k(c,"enter");wait(b"P",off)
            off=len(bytes_());k(c,"enter");wait(b"Q",off)
            off=len(bytes_());k(c,"enter");wait(b"O",off)
            off=len(bytes_());k(c,"enter");wait(b"aU",off);wait(b"H",off)
            STATE["checks"].append("Marked safe virtual disk selected as default during setup")
            k(c,"f2");t(c,"Settings")
            off=len(bytes_());k(c,"enter");wait(b"zDn1",off)
            for _ in range(9):k(c,"down")
            off=len(bytes_());k(c,"right")
            wait(b"wP",off)
            wait(b"wS",off)
            shot(c,"FalconOS-Persistent-Wallpaper-Changed")
            STATE["checks"].append("User wallpaper safely persisted on launcher-owned partition")
    finally:close(proc,fd)
    # Restart WITHOUT any installer keystrokes: settings must load from disk,
    # and empty-password login must bypass the lock screen again.
    proc,fd=start(6)
    try:
        with api.connect("127.0.0.1::5906",timeout=35) as c:
            wait(b"wB1",timeout=80)
            wait(b"aU",timeout=80)
            wait(b"H",timeout=80)
            if b"L" in bytes_():
                raise AssertionError("Installer reappeared despite valid persisted data")
            pause(1.0)
            shot(c,"FalconOS-Persistent-Wallpaper-After-Reboot")
            STATE["checks"].append("Same wallpaper restored after real QEMU reboot")
            STATE["checks"].append("Account restored and auto-logged in without a password")
    finally:close(proc,fd)
    STATE["status"]="PASS"
    REPORT.write_text(json.dumps(STATE,indent=2))
if __name__=="__main__":
    try:main()
    except Exception as e:
        STATE["status"]="FAIL";STATE["error"]=str(e)
        REPORT.write_text(json.dumps(STATE,indent=2))
        traceback.print_exc()
        raise
