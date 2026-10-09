#!/usr/bin/env python3
"""Real QEMU read-only PCI capabilities test: NVMe, xHCI, VGA.
No attempt to issue NVMe queue commands or USB transfers.
"""
import pathlib,subprocess,time
root=pathlib.Path("build")
root.mkdir(exist_ok=True)
disk=root/"pci-probe-nvme.img"
if not disk.exists():
    with disk.open("wb") as f:f.truncate(16*1024*1024)
debug=root/"pci-probe-debug.log"
if debug.exists():debug.unlink()
cmd=["qemu-system-x86_64","-accel","tcg","-m","1024","-smp","1",
     "-cdrom","build/FalconOS.iso","-display","none","-vga","std",
     "-drive",f"file={disk},if=none,format=raw,id=nvmedrive",
     "-device","nvme,serial=FALCONOS-CI,drive=nvmedrive",
     "-device","qemu-xhci",
     "-serial","none","-monitor","none",
     "-debugcon",f"file:{debug}","-global","isa-debugcon.iobase=0xe9",
     "-no-reboot"]
proc=subprocess.Popen(cmd,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
try:
    deadline=time.monotonic()+40
    while time.monotonic()<deadline:
        if proc.poll() is not None:
            raise RuntimeError("QEMU exited before PCI probe; "+proc.stderr.read().decode(errors="replace")[-400:])
        out=debug.read_bytes() if debug.exists() else b""
        if b"VUG" in out:
            print("PASS PCI physical BAR: NVMe readable, xHCI readable, VGA identified")
            break
        if b"v" in out or b"u" in out or b"g" in out:
            raise RuntimeError("PCI class/BAR scan incomplete; debug "+repr(out))
        time.sleep(.2)
    else:
        raise TimeoutError("No NVMe/xHCI/VGA PCI debug signatures; "+repr(debug.read_bytes() if debug.exists() else b""))
finally:
    proc.terminate()
    try:proc.communicate(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill();proc.communicate()
