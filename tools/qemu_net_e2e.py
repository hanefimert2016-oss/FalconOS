#!/usr/bin/env python3
"""Boot the actual FalconOS kernel with QEMU user-net RTL8139 and require
a guest-generated 'N' debugcon event after a gateway ICMP echo reply.
This is a real QEMU NIC/ARP/IPv4/ICMP test; it is not HTTPS or TCP.
"""
import argparse
from pathlib import Path
import subprocess
import time
import struct

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--iso",default="build/FalconOS.iso")
    p.add_argument("--timeout",type=float,default=40)
    args=p.parse_args()
    debug=Path("build/net-smoke-debug.log")
    pcap=Path("build/net-smoke.pcap")
    if pcap.exists(): pcap.unlink()
    def summary():
        try:
            data=pcap.read_bytes()
            if len(data)<24:return "pcap missing"
            off=24;counts={};arps=[]
            while off+16<=len(data):
                captured=struct.unpack_from('<I',data,off+8)[0]
                off+=16
                if captured<14 or off+captured>len(data):break
                typ=int.from_bytes(data[off+12:off+14],'big')
                name={0x0800:'IPv4',0x0806:'ARP'}.get(typ,'other')
                counts[name]=counts.get(name,0)+1
                if typ==0x0806 and captured>=42:
                    q=data[off:off+captured]
                    arps.append({
                      "op":int.from_bytes(q[20:22],"big"),
                      "src":q[6:12].hex(":"),
                      "dst":q[0:6].hex(":"),
                      "sender_ip":".".join(map(str,q[28:32])),
                      "target_ip":".".join(map(str,q[38:42])),
                    })
                off+=captured
            return f'captured Ethernet frames: {counts}; ARP details: {arps[:6]}'
        except OSError as e:return str(e)

    debug.parent.mkdir(exist_ok=True)
    if debug.exists(): debug.unlink()
    cmd=["qemu-system-x86_64","-accel","tcg","-m","1024","-smp","1",
         "-cdrom",args.iso,"-display","none","-vga","std",
         "-netdev","user,id=net0","-device","rtl8139,netdev=net0",
         "-object",f"filter-dump,id=trace,netdev=net0,file={pcap}",
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
                raise RuntimeError("Guest NIC probe did not receive ICMP reply; debug "+repr(data)+"; "+summary())
            time.sleep(.2)
        raise TimeoutError("Guest network probe timed out; debug "+repr(debug.read_bytes() if debug.exists() else b"")+"; "+summary())
    finally:
        proc.terminate()
        try: proc.communicate(timeout=4)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.communicate()

if __name__=="__main__":
    main()
