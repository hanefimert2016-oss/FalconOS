#!/usr/bin/env python3
"""Real FalconOS Browser HTTPS GUI proof inside an Internet-connected QEMU VM.

The browser itself must open example.com using RTL8139->DNS->native TCP->
BearSSL CA/SAN validation. The Arch host HTTPS gateway is NOT running.
Captures real framebuffer after successful native guest render.
"""
import argparse
from pathlib import Path
import socket,subprocess,time
from qemu_smoke import ppm_to_png
from qemu_ui_smoke import command,screenshot,wait_for_marker,picture_difference

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--iso",default="build/FalconOS.iso")
    p.add_argument("--timeout",type=int,default=100)
    args=p.parse_args()
    root=Path("build").absolute();root.mkdir(parents=True,exist_ok=True)
    monitor=root/"native-browser-monitor.sock"
    debug=root/"native-browser.log"
    first=root/"native-browser-before.ppm"
    last=root/"native-browser-after.ppm"
    shot=root/"FalconOS-Native-HTTPS-Internet-Browser.png"
    for f in (monitor,debug,first,last,shot):
        if f.exists():f.unlink()
    cmd=["qemu-system-x86_64","-accel","tcg","-cpu","max","-m","1024",
         "-smp","1","-cdrom",args.iso,"-boot","d","-display","none",
         "-vga","std","-netdev","user,id=internet",
         "-device","rtl8139,netdev=internet",
         "-serial","none",
         "-monitor",f"unix:{monitor},server=on,wait=off",
         "-debugcon",f"file:{debug}","-global","isa-debugcon.iobase=0xe9",
         "-no-reboot"]
    q=subprocess.Popen(cmd,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        deadline=time.monotonic()+16
        while not monitor.exists():
            if q.poll() is not None or time.monotonic()>deadline:
                raise RuntimeError("QEMU HMP not started")
            time.sleep(.1)
        with socket.socket(socket.AF_UNIX) as sock:
            sock.settimeout(5);sock.connect(str(monitor))
            wait_for_marker(debug,b"L",timeout=30)
            for marker in (b"T",b"A",b"K",b"D",b"U"):
                offset=len(debug.read_bytes())
                command(sock,"sendkey ret",.25)
                wait_for_marker(debug,marker,after=offset)
            for char in "falcon":
                command(sock,"sendkey "+char,.24)
            for marker in (b"P",b"Q",b"O"):
                offset=len(debug.read_bytes())
                command(sock,"sendkey ret",.28)
                wait_for_marker(debug,marker,after=offset)
            command(sock,"sendkey right",.2)
            off=len(debug.read_bytes())
            command(sock,"sendkey ret",.25)
            wait_for_marker(debug,b"W",after=off)
            off=len(debug.read_bytes())
            command(sock,"sendkey ret",.25)
            wait_for_marker(debug,b"H",after=off)
            command(sock,"sendkey esc",.2)
            before=screenshot(sock,first)
            command(sock,"sendkey f2",.65)
            command(sock,"sendkey right",.22)
            command(sock,"sendkey ret",.7) # Browser is favorite index 1.
            browser_before=screenshot(sock,root/"native-browser-initial.ppm")
            command(sock,"sendkey ret",.05) # Live https://example.com/
            # No host bridge is active. QEMU's NAT provides the Internet.
            start=time.monotonic()
            while time.monotonic()-start<args.timeout:
                if q.poll() is not None:
                    raise RuntimeError("QEMU crashed while browsing")
                data=debug.read_bytes() if debug.exists() else b""
                if b"bY" in data:
                    # The kernel emits the test marker on load completion,
                    # before the next graphics-present iteration. Wait for
                    # the actual guest to redraw instead of photographing
                    # the prior "press Enter" placeholder.
                    time.sleep(2.0)
                    after=screenshot(sock,last)
                    delta=picture_difference(browser_before,after)
                    ppm_to_png(last,shot) # archive framebuffer even when check fails
                    if delta<20:
                        raise AssertionError("Verified TLS but browser did not visibly repaint; pixel delta="+str(delta))
                    print("PASS real Internet guest native TLS 1.2, CA+hostname, HTML browser framebuffer, pixel delta",delta,shot)
                    return
                if b"bN" in data:
                    raise AssertionError("Browser TLS or HTTP data failed: "+repr(data[-160:]))
                time.sleep(.2)
            raise TimeoutError("Native Browser HTTPS GUI did not finish: "+repr(
                debug.read_bytes()[-250:] if debug.exists() else b""))
    finally:
        q.terminate()
        try:q.communicate(timeout=4)
        except subprocess.TimeoutExpired:q.kill();q.communicate()

if __name__=="__main__":main()
