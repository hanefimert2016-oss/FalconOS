#!/usr/bin/env python3
"""Boot the actual FalconOS kernel with QEMU user-net RTL8139 and require
a guest-generated 'N' debugcon event after a gateway ICMP echo reply.
This is a real QEMU NIC/ARP/IPv4/ICMP test; it is not HTTPS or TCP.
"""
import argparse
from pathlib import Path
import subprocess
import time

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--iso",default="build/FalconOS.iso")
    p.add_argument("--timeout",type=float,default=40)
    args=p.parse_args()
    debug=Path("build/net-smoke-debug.log")
    debug.parent.mkdir(exist_ok=True)
    if debug.exists(): debug.unlink()
    cmd=["qemu-system-x86_64","-accel","tcg","-m","1024","-smp","1",
         "-cdrom",args.iso,"-display","none","-vga","std",
         "-netdev","user,id=net0","-device","rtl8139,netdev=net0",
         "-serial","none","-monitor","none",
         "-debugcon",f"file:{debug}","-global","isa-debugcon.iobase=0xe9",
         "-no-reboot"]
    proc=subprocess.Popen(cmd,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        deadline=time.monotonic()+args.timeout
        while time.monotonic()<deadline:
            if proc.poll() is not None:
                raise RuntimeError("QEMU exited before network probe completed")
            data=debug.read_bytes() if debug.exists() else b""
            if b"N" in data:
                print("PASS native RTL8139 -> ARP -> IPv4 -> ICMP echo reply")
                return
            if b"n" in data:
                raise RuntimeError("Guest NIC probe did not receive ICMP reply; debug "+repr(data))
            time.sleep(.2)
        raise TimeoutError("Guest network probe timed out; debug "+repr(debug.read_bytes() if debug.exists() else b""))
    finally:
        proc.terminate()
        try: proc.communicate(timeout=4)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.communicate()

if __name__=="__main__":
    main()
