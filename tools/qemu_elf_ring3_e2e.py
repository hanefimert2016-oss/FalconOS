#!/usr/bin/env python3
"""A genuine ELF64 PT_LOAD + code execution under x86_64 CPL3 in QEMU.

The kernel constructs a minimal ELF in memory, validates/stages it, enters
Ring3 and invokes getpid/diag/exit syscalls. No binary is run in Ring0.
This does NOT imply general multi-process ELF application support.
"""
import pathlib,subprocess,time
p=pathlib.Path("build/elf-ring3-debug.log")
p.parent.mkdir(exist_ok=True)
if p.exists():p.unlink()
args=["qemu-system-x86_64","-accel","tcg","-cpu","max","-m","1024",
      "-smp","1","-cdrom","build/FalconOS.iso","-display","none",
      "-vga","std","-serial","none","-monitor","none",
      "-debugcon",f"file:{p}","-global","isa-debugcon.iobase=0xe9",
      "-no-reboot"]
q=subprocess.Popen(args,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
try:
    end=time.monotonic()+55
    while time.monotonic()<end:
        if q.poll() is not None:
            raise RuntimeError("QEMU guest crashed")
        output=p.read_bytes() if p.exists() else b""
        if b"eUL" in output:
            print("PASS real ELF64 PT_LOAD -> Ring3 getpid/diagnostic/exit system calls")
            break
        if b"l" in output:
            raise AssertionError("Guest rejected ELF64 or did not return from Ring3: "+repr(output))
        time.sleep(.2)
    else:
        raise TimeoutError("ELF64 user executable did not finish: "+repr(
            p.read_bytes() if p.exists() else b""))
finally:
    q.terminate()
    try:q.communicate(timeout=3)
    except subprocess.TimeoutExpired:
        q.kill();q.communicate()
