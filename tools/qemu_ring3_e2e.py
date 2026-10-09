#!/usr/bin/env python3
"""Actual CPL3 CPU privilege test under QEMU with user/supervisor page bits.
Requires debug E (probe entry), U (INT 0x80 from CPL3), R (kernel resume).
This DOES NOT verify native ELF process scheduling or production isolation.
"""
from pathlib import Path
import subprocess
import time

debug=Path("build/ring3-debug.log")
debug.parent.mkdir(exist_ok=True)
if debug.exists():debug.unlink()
args=["qemu-system-x86_64","-accel","tcg","-cpu","max","-m","1024",
      "-smp","1","-cdrom","build/FalconOS.iso","-display","none",
      "-vga","std","-serial","none","-monitor","none",
      "-debugcon",f"file:{debug}","-global","isa-debugcon.iobase=0xe9",
      "-no-reboot"]
proc=subprocess.Popen(args,stderr=subprocess.PIPE,stdout=subprocess.DEVNULL)
try:
    until=time.monotonic()+55
    while time.monotonic()<until:
        if proc.poll() is not None:
            raise RuntimeError("Ring3 boot crashed: "+proc.stderr.read().decode(errors="replace")[-600:])
        content=debug.read_bytes() if debug.exists() else b""
        if b"EUR" in content:
            print("PASS real x86_64 ring-3 privilege transition, INT80 kernel call and ring-0 resume")
            break
        if b"r" in content:
            raise AssertionError("No safe physical RAM available for Ring3 test; "+repr(content))
        time.sleep(.2)
    else:
        raise TimeoutError("User-mode CPU test stalled; markers="+repr(debug.read_bytes() if debug.exists() else b""))
finally:
    proc.terminate()
    try:proc.communicate(timeout=4)
    except subprocess.TimeoutExpired:
        proc.kill();proc.communicate()
