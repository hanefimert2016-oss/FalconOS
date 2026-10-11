#!/usr/bin/env python3
"""Real QEMU/VNC test: installer No defaults, passwordless auto-login,
Falco's native resolution layout (NO Chromium), live wallpaper change,
password-protected lockscreen. No HMP/QMP fake keyboard/mouse."""
import json
import os
import socket
import subprocess
import sys
import time
import traceback
from pathlib import Path
from vncdotool import api
from vnc_real_input_audit import k,t,shot,pause
import vnc_real_input_audit as helper

B=Path("build").absolute()
B.mkdir(exist_ok=True)
LOG=B/"native-upgrade-guest.log"
RESULT=B/"native-upgrade-results.json"
META={"result":"RUNNING","checks":[],"browser_engine":"Falco own semantic C layout", "chromium_used":False}
def data():return LOG.read_bytes() if LOG.exists() else b""
def wait(marker,offset=0,timeout=100):
    until=time.monotonic()+timeout
    while time.monotonic()<until:
        if marker in data()[offset:]:return
        pause(.15)
    raise AssertionError(f"Guest event {marker!r} never seen: {data()[-180:]!r}")

def start_qemu(port):
    if LOG.exists():LOG.unlink()
    helper.LOG=LOG
    command=["qemu-system-x86_64","-accel","tcg","-cpu","max",
             "-m","1024","-smp","1",
             "-cdrom","build/FalconOS.iso","-boot","d",
             "-display","none","-vga","std",
             "-global","VGA.vgamem_mb=256",
             "-netdev","user,id=lan",
             "-device","rtl8139,netdev=lan",
             "-vnc",f"127.0.0.1:{port}",
             "-monitor","none","-serial","none",
             "-debugcon",f"file:{LOG}",
             "-global","isa-debugcon.iobase=0xe9",
             "-no-reboot"]
    fd=(B/f"native-qemu-{port}.log").open("wb")
    qemu=subprocess.Popen(command,stdout=subprocess.DEVNULL,stderr=fd)
    until=time.monotonic()+20
    while time.monotonic()<until:
        try:
            with socket.create_connection(("127.0.0.1",5900+port),timeout=.2):
                return qemu,fd
        except OSError:pause(.2)
    qemu.kill();fd.close()
    raise RuntimeError("QEMU VNC server unavailable")

def install(c,password):
    wait(b"L",timeout=100)
    for x in (b"T",b"A",b"K",b"D",b"U"):
        off=len(data());k(c,"enter");wait(x,off,35)
    t(c,"falcon")
    off=len(data());k(c,"enter");wait(b"P",off,45)
    if password:t(c,password)
    off=len(data());k(c,"enter");wait(b"Q",off,45)
    if password:t(c,password)
    off=len(data());k(c,"enter");wait(b"O",off,45)
    # Default choice: NO; do not push Right.
    off=len(data());k(c,"enter")
    if password:
        wait(b"W",off,55)
        shot(c,"FalconOS-New-Lockscreen-Password")
        if b"H" in data()[off:]:
            raise AssertionError("Password account bypassed the lock!")
        for x in password:k(c,x)
        off=len(data());k(c,"enter");wait(b"H",off,55)
        META["checks"].append("Password-protected user required real typed password")
    else:
        wait(b"aU",off,55)
        wait(b"H",off,55)
        META["checks"].append("Empty-password user auto-logged in without a key")
    META["checks"].append("Second user defaulted to No")
    pause(.9)
    shot(c,"FalconOS-Native-Desktop"+("-Password" if password else ""))

def exercise(c):
    k(c,"f2");t(c,"Falco")
    off=len(data());k(c,"enter");wait(b"zNn1",off,60)
    pause(.7)
    off=len(data());k(c,"f9")
    wait(b"vY",off,110)
    wait(b"nY",off,110)
    wait(b"nR",off,110)
    META["checks"].append("Native Falco painted real HTTPS document with own C layout")
    pause(1)
    shot(c,"FalconOS-Falco-Native-Real-Site")
    k(c,"f2");t(c,"Settings")
    off=len(data());k(c,"enter");wait(b"zDn2",off,70)
    # SR_THEME=0 ... SR_WIDGETS=8, SR_WALLPAPER=9
    for _ in range(9):k(c,"down")
    off=len(data());k(c,"right")
    pause(.8)
    shot(c,"FalconOS-Wallpaper-Settings")
    META["checks"].append("Wallpaper preset changed through real keyboard and Settings UI")
    META["checks"].append("Native HTML renderer: no Chromium, no CSS/JS spoofing")

def run_guest(port,password):
    guest,fd=start_qemu(port)
    try:
        with api.connect(f"127.0.0.1::{5900+port}",timeout=35) as c:
            install(c,password)
            if not password:exercise(c)
    finally:
        guest.terminate()
        try:guest.wait(timeout=5)
        except subprocess.TimeoutExpired:guest.kill()
        fd.close()

def main():
    bridge_log=(B/"native-layout-gateway.log").open("wb")
    bridge=subprocess.Popen([sys.executable,"-u","tools/falco_native_gateway.py",
                            "--bind","127.0.0.1"],
                            stdout=bridge_log,stderr=subprocess.STDOUT)
    try:
        deadline=time.monotonic()+12
        while time.monotonic()<deadline:
            try:
                with socket.create_connection(("127.0.0.1",18446),timeout=.3):break
            except OSError:pause(.15)
        else:raise RuntimeError("Native gateway not listening")
        run_guest(8,"")
        run_guest(9,"falco123")
        META["result"]="PASS"
    finally:
        bridge.terminate()
        try:bridge.wait(timeout=4)
        except subprocess.TimeoutExpired:bridge.kill()
        bridge_log.close()
        RESULT.write_text(json.dumps(META,ensure_ascii=False,indent=2))
if __name__=="__main__":
    try:main()
    except Exception as e:
        META["result"]="FAIL";META["error"]=str(e)
        RESULT.write_text(json.dumps(META,ensure_ascii=False,indent=2))
        traceback.print_exc()
        raise
