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
import shutil

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--iso",default="build/FalconOS.iso")
    p.add_argument("--timeout",type=float,default=40)
    p.add_argument("--require-dns",action="store_true")
    p.add_argument("--require-dhcp",action="store_true")
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
    firmware=[(Path("/usr/share/OVMF/OVMF_CODE_4M.fd"),Path("/usr/share/OVMF/OVMF_VARS_4M.fd")),
              (Path("/usr/share/OVMF/OVMF_CODE.fd"),Path("/usr/share/OVMF/OVMF_VARS.fd")),
              (Path("/usr/share/edk2/x64/OVMF_CODE.4m.fd"),Path("/usr/share/edk2/x64/OVMF_VARS.4m.fd"))]
    pair=next(((c,v) for c,v in firmware if c.exists() and v.exists()),None)
    if not pair:raise RuntimeError("Real network test requires UEFI OVMF firmware")
    nvram=Path("build/rtl8139-net-ovmf-vars.fd")
    shutil.copyfile(pair[1],nvram)
    flash=["-drive",f"if=pflash,unit=0,format=raw,readonly=on,file={pair[0]}",
           "-drive",f"if=pflash,unit=1,format=raw,file={nvram}"]
    cmd=["qemu-system-x86_64","-machine","q35","-accel","tcg",
         *flash,"-m","1024","-smp","1",
         "-boot","d","-cdrom",args.iso,"-display","none","-vga","std",
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
            # FalcoBoot's literal string "MENU DRAWN" contains 'N'.
            # NEVER treat it as a network success. The guest must have
            # passed UEFI handoff and emitted the adjacent RTL8139 live
            # driver marker V + its real ICMP result N from main.c.
            pos=data.find(b"FALCONBOOT/1 HANDOFF")
            guest=data[pos+len(b"FALCONBOOT/1 HANDOFF"):] if pos>=0 else b""
            if b"N!" in guest and b"V" in guest[:guest.find(b"N!")]:
                payload=pcap.read_bytes() if pcap.exists() else b""
                eth=[];offset=24
                while offset+16<=len(payload):
                    size=struct.unpack_from("<I",payload,offset+8)[0]
                    offset+=16
                    if size<14 or offset+size>len(payload):break
                    eth.append(int.from_bytes(payload[offset+12:offset+14],"big"))
                    offset+=size
                if 0x0806 not in eth or 0x0800 not in eth:
                    raise RuntimeError("Kernel reported ICMP success without ARP + IPv4 frames: "+summary())
                if not args.require_dns:
                    print("PASS guest RTL8139 ACTIVE -> real ARP + IPv4 pcap -> ICMP echo reply")
                    return
                if b"D" in guest:
                    if not args.require_dhcp:
                        print("PASS native RTL8139 ARP/ICMP and DNS-over-UDP response")
                        return
                    if b"B" in guest:
                        print("PASS native RTL8139 ARP + ICMP + DNS and DHCP DORA lease")
                        return
                    if b"b" in guest:
                        raise RuntimeError("Guest DHCP DORA failed; "+repr(guest)+"; "+summary())
                if b"d" in guest:
                    raise RuntimeError("Native DNS query failed; debug "+repr(guest)+"; "+summary())
            if b"n?" in guest or b"v" in guest[:2]:
                raise RuntimeError("Guest RTL8139 ping failed; debug "+repr(guest[-120:])+"; "+summary())
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
