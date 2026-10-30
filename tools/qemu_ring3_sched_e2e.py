#!/usr/bin/env python3
"""UEFI/OVMF QEMU proof: two existing cooperative CPL3 ELF tasks.
This uses the actual FalconBoot UEFI ISO, not SeaBIOS firmware.
Tasks still share an identity-mapped user test region; not full isolation.
"""
from pathlib import Path
import shutil
import subprocess
import time

OVMF=(
    ("/usr/share/OVMF/OVMF_CODE_4M.fd", "/usr/share/OVMF/OVMF_VARS_4M.fd"),
    ("/usr/share/OVMF/OVMF_CODE.fd", "/usr/share/OVMF/OVMF_VARS.fd"),
    ("/usr/share/edk2/x64/OVMF_CODE.4m.fd", "/usr/share/edk2/x64/OVMF_VARS.4m.fd"),
)

def main():
    firmware=next(((Path(c),Path(v)) for c,v in OVMF
                   if Path(c).is_file() and Path(v).is_file()),None)
    if firmware is None:raise RuntimeError("OVMF firmware required")
    iso=Path("build/FalconOS.iso").absolute()
    if not iso.is_file():raise FileNotFoundError(iso)
    build=Path("build");build.mkdir(exist_ok=True)
    var=(build/"ring3-scheduler-ovmf-vars.fd").absolute()
    shutil.copyfile(firmware[1],var)
    debug=(build/"ring3-scheduler-debug.log").absolute()
    debug.unlink(missing_ok=True)
    command=[
        "qemu-system-x86_64","-machine","q35","-accel","tcg","-cpu","max",
        "-m","1024","-smp","1",
        "-drive",f"if=pflash,unit=0,format=raw,readonly=on,file={firmware[0].absolute()}",
        "-drive",f"if=pflash,unit=1,format=raw,file={var}",
        "-drive",f"file={iso},if=ide,index=2,media=cdrom",
        "-boot","order=d","-display","none","-vga","std",
        "-serial","none","-monitor","none",
        "-debugcon",f"file:{debug}","-global","isa-debugcon.iobase=0xe9",
        "-no-reboot"
    ]
    q=subprocess.Popen(command,stderr=subprocess.PIPE,stdout=subprocess.DEVNULL)
    try:
        deadline=time.monotonic()+75
        while time.monotonic()<deadline:
            if q.poll() is not None:
                raise RuntimeError("Ring3 scheduler guest stopped: "+
                                   q.stderr.read().decode(errors="replace")[-500:])
            data=debug.read_bytes() if debug.exists() else b""
            if b"K[" in data:
                trace=data.split(b"K[",1)[1]
                if b"z" in trace:
                    raise AssertionError("Ring3 scheduler failed: "+repr(trace))
                if b"Z" in trace:
                    trace=trace[:trace.index(b"Z")+1]
                    if trace.count(b"A")!=2 or trace.count(b"B")!=2 or trace.count(b"U")!=2:
                        raise AssertionError("CPL3 round robin incomplete: "+repr(trace))
                    print("PASS UEFI/QEMU two CPL3 ELF tasks yielded/resumed/exited with explicit diagnostic grants")
                    return
            time.sleep(.2)
        raise TimeoutError("Ring3 scheduler timeout: "+
                           repr(debug.read_bytes() if debug.exists() else b""))
    finally:
        q.terminate()
        try:q.communicate(timeout=4)
        except subprocess.TimeoutExpired:q.kill();q.communicate()

if __name__=="__main__":
    main()
