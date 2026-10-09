#!/usr/bin/env python3
"""Outbound real internet HTTPS from FalconOS guest, not host-side proxy.

Pass only after QEMU RTL8139 -> DHCP/IP -> DNS -> TCP -> BearSSL TLS 1.2
CA/hostname validation -> authenticated HTTP 200 with Example Domain text.
"""
from pathlib import Path
import subprocess,time,argparse
def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--iso",default="build/FalconOS.iso")
    ap.add_argument("--timeout",type=int,default=115)
    args=ap.parse_args()
    log=Path("build/native-public-https.log")
    log.parent.mkdir(exist_ok=True)
    if log.exists():log.unlink()
    pcap=Path("build/native-public-https.pcap")
    if pcap.exists():pcap.unlink()
    cmd=["qemu-system-x86_64","-accel","tcg","-cpu","max","-m","1024",
         "-smp","1","-cdrom",args.iso,
         "-display","none","-vga","std",
         "-netdev","user,id=net0","-device","rtl8139,netdev=net0",
         "-object",f"filter-dump,id=publicwatch,netdev=net0,file={pcap}",
         "-serial","none","-monitor","none",
         "-debugcon",f"file:{log}","-global","isa-debugcon.iobase=0xe9",
         "-no-reboot"]
    q=subprocess.Popen(cmd,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        end=time.monotonic()+args.timeout
        while time.monotonic()<end:
            if q.poll() is not None:
                raise RuntimeError("Guest exited/crashed before public HTTPS")
            output=log.read_bytes() if log.exists() else b""
            if b"PY" in output:
                print("PASS real public example.com certificate-validated native HTTPS inside QEMU")
                return
            if b"PN" in output:
                raise AssertionError("Real public HTTPS request failed after DNS/TCP/TLS: "+repr(output))
            time.sleep(.3)
        raise TimeoutError("Guest public native HTTPS timeout; debug="+repr(log.read_bytes() if log.exists() else b""))
    finally:
        q.terminate()
        try:q.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            q.kill();q.communicate()
if __name__=="__main__":main()
