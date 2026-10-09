#!/usr/bin/env python3
"""Proof of two separate cooperative user ELF64 processes in CPL3.

User A yields -> user B yields -> A resumes/exit -> B resumes/exit.
Each context saves GPRs, IRETQ frame and isolated user stack snapshot.
This is NOT timer preemption or per-process virtual-memory isolation.
"""
from pathlib import Path
import subprocess,time
debug=Path("build/ring3-scheduler-debug.log")
debug.parent.mkdir(exist_ok=True)
if debug.exists():debug.unlink()
command=["qemu-system-x86_64","-accel","tcg","-cpu","max","-m","1024",
         "-smp","1","-cdrom","build/FalconOS.iso","-display","none",
         "-vga","std","-serial","none","-monitor","none",
         "-debugcon",f"file:{debug}","-global","isa-debugcon.iobase=0xe9",
         "-no-reboot"]
q=subprocess.Popen(command,stderr=subprocess.PIPE,stdout=subprocess.DEVNULL)
try:
    end=time.monotonic()+55
    while time.monotonic()<end:
        if q.poll() is not None:
            raise RuntimeError("Ring3 scheduler QEMU terminated: "+q.stderr.read().decode(errors="replace")[-500:])
        data=debug.read_bytes() if debug.exists() else b""
        if b"KAB" in data and b"Z" in data[data.index(b"K"):]:
            index=data.index(b"K")
            trace=data[index:]
            if trace.count(b"A")<2 or trace.count(b"B")<2 or trace.count(b"U")<2:
                raise AssertionError("Incomplete round-robin/user syscalls: "+repr(trace))
            print("PASS two native CPL3 ELF tasks yield, schedule round-robin, resume registers, exit")
            break
        if b"z" in data[data.index(b"K"):] if b"K" in data else False:
            raise AssertionError("CPL3 scheduler failed: "+repr(data))
        time.sleep(.2)
    else:
        raise TimeoutError("Ring3 scheduler timeout: "+repr(debug.read_bytes() if debug.exists() else b""))
finally:
    q.terminate()
    try:q.communicate(timeout=4)
    except subprocess.TimeoutExpired:
        q.kill();q.communicate()
