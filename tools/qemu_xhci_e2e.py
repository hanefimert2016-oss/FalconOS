#!/usr/bin/env python3
"""Actual QEMU xHCI Run/Stop, command ring and Event TRB test.
No USB endpoint transfers are exercised.
"""
from pathlib import Path
import subprocess,time

debug=Path("build/xhci-command-debug.log")
debug.parent.mkdir(exist_ok=True)
if debug.exists():debug.unlink()
args=["qemu-system-x86_64","-accel","tcg","-cpu","max","-m","1024",
      "-smp","1","-cdrom","build/FalconOS.iso","-display","none",
      "-vga","std","-device","qemu-xhci",
      "-serial","none","-monitor","none",
      "-debugcon",f"file:{debug}","-global","isa-debugcon.iobase=0xe9",
      "-no-reboot"]
proc=subprocess.Popen(args,stderr=subprocess.PIPE,stdout=subprocess.DEVNULL)
try:
    end=time.monotonic()+60
    while time.monotonic()<end:
        if proc.poll() is not None:raise RuntimeError("QEMU stopped unexpectedly "+
                                      proc.stderr.read().decode(errors="replace")[-500:])
        output=debug.read_bytes() if debug.exists() else b""
        if b"X" in output:
            print("PASS real QEMU xHCI Controller Reset -> Command Ring No-op -> Completion Event")
            break
        if b"x" in output:raise AssertionError("Guest failed xHCI No-op: "+repr(output))
        time.sleep(.2)
    else:raise TimeoutError("xHCI command/event test timed out; debug "+
                            repr(debug.read_bytes() if debug.exists() else b""))
finally:
    proc.terminate()
    try:proc.communicate(timeout=4)
    except subprocess.TimeoutExpired:
        proc.kill();proc.communicate()
