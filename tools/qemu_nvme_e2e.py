#!/usr/bin/env python3
"""Live QEMU NVMe Admin SQ/CQ DMA + Identify Controller/Namespace.
NO namespace write or read commands are executed in this test.
"""
from pathlib import Path
import subprocess,time
base=Path("build");base.mkdir(exist_ok=True)
disk=base/"nvme-admin-only.img"
with disk.open("wb") as fp:fp.truncate(16*1024*1024)
trace=base/"nvme-admin-debug.log"
if trace.exists():trace.unlink()
cmd=["qemu-system-x86_64","-accel","tcg","-cpu","max",
     "-m","1024","-smp","1","-cdrom","build/FalconOS.iso",
     "-display","none","-vga","std",
     "-drive",f"file={disk},if=none,format=raw,id=nvme-test",
     "-device","nvme,serial=FALCONOS-CI,drive=nvme-test",
     "-serial","none","-monitor","none",
     "-debugcon",f"file:{trace}","-global","isa-debugcon.iobase=0xe9",
     "-no-reboot"]
p=subprocess.Popen(cmd,stderr=subprocess.PIPE,stdout=subprocess.DEVNULL)
try:
    until=time.monotonic()+55
    while time.monotonic()<until:
        if p.poll() is not None:
            raise RuntimeError("NVMe QEMU exited "+p.stderr.read().decode(errors="replace")[-500:])
        result=trace.read_bytes() if trace.exists() else b""
        if b"I" in result:
            print("PASS real NVMe PCI BAR, Admin SQ/CQ DMA and controller+namespace Identify")
            break
        if b"i" in result:
            raise AssertionError("NVMe admin identify failed; trace "+repr(result))
        time.sleep(.2)
    else:raise TimeoutError("NVMe admin Identify stuck; trace "+
                            repr(trace.read_bytes() if trace.exists() else b""))
finally:
    p.terminate()
    try:p.communicate(timeout=3)
    except subprocess.TimeoutExpired:
        p.kill();p.communicate()
