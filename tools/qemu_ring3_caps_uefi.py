#!/usr/bin/env python3
"""Real UEFI/QEMU CPL3 per-task syscall grants, no physical disks."""
from pathlib import Path
import shutil
import subprocess
import time

OVMF = (
    ("/usr/share/OVMF/OVMF_CODE_4M.fd", "/usr/share/OVMF/OVMF_VARS_4M.fd"),
    ("/usr/share/OVMF/OVMF_CODE.fd", "/usr/share/OVMF/OVMF_VARS.fd"),
    ("/usr/share/edk2/x64/OVMF_CODE.4m.fd", "/usr/share/edk2/x64/OVMF_VARS.4m.fd"),
)

def main():
    iso = Path("build/FalconOS.iso").absolute()
    if not iso.is_file():
        raise FileNotFoundError(iso)
    firmware = next(((Path(code), Path(var)) for code,var in OVMF
                     if Path(code).is_file() and Path(var).is_file()), None)
    if firmware is None:
        raise RuntimeError("OVMF UEFI firmware not installed")
    build = Path("build")
    build.mkdir(exist_ok=True)
    nvram = (build/"ring3-caps-vars.fd").absolute()
    shutil.copyfile(firmware[1],nvram)
    trace = (build/"ring3-caps-debug.log").absolute()
    trace.unlink(missing_ok=True)
    args = [
        "qemu-system-x86_64","-machine","q35","-accel","tcg",
        "-cpu","max","-m","1024","-smp","1",
        "-drive",f"if=pflash,unit=0,format=raw,readonly=on,file={firmware[0].absolute()}",
        "-drive",f"if=pflash,unit=1,format=raw,file={nvram}",
        "-drive",f"file={iso},if=ide,index=2,media=cdrom",
        "-boot","order=d","-display","none","-vga","std",
        "-serial","none","-monitor","none",
        "-debugcon",f"file:{trace}","-global","isa-debugcon.iobase=0xe9",
        "-no-reboot",
    ]
    proc = subprocess.Popen(args,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        until = time.monotonic()+75
        while time.monotonic()<until:
            if proc.poll() is not None:
                raise RuntimeError("Ring3 guest stopped: "+proc.stderr.read().decode(errors="replace")[-500:])
            raw = trace.read_bytes() if trace.exists() else b""
            if b"P[" in raw:
                data = raw.split(b"P[",1)[1]
                if b"z" in data:
                    raise AssertionError("Ring3 capability demo failed: "+repr(data))
                if b"Z" in data:
                    result=data[:data.index(b"Z")+1]
                    if result.count(b"U")!=1 or result.count(b"D")!=2:
                        raise AssertionError("Unexpected allowed/denied syscalls: "+repr(result))
                    if result.count(b"A")!=2 or result.count(b"B")!=2:
                        raise AssertionError("Two-task round robin incomplete: "+repr(result))
                    if result[-1:]!=b"Z":
                        raise AssertionError("No capability success marker")
                    print("PASS UEFI CPL3 two ELF tasks: one granted diagnostic/clock, one denied; four slices")
                    return
            time.sleep(0.2)
        raise TimeoutError("Ring3 capability QEMU timeout: "+repr(trace.read_bytes() if trace.exists() else b""))
    finally:
        proc.terminate()
        try:
            proc.communicate(timeout=4)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.communicate()

if __name__=="__main__":
    main()
