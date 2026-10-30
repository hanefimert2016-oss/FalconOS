#!/usr/bin/env python3
"""Boot real FalconOS V2 under UEFI and verify a controller's test marker.

These tests perform an xHCI command-ring NOOP or NVMe admin Identify only.
They NEVER issue NVMe namespace reads/writes or touch physical disks.
"""
import argparse
import re
import shutil
import subprocess
import time
from pathlib import Path

FIRMWARE = (
    ("/usr/share/OVMF/OVMF_CODE_4M.fd", "/usr/share/OVMF/OVMF_VARS_4M.fd"),
    ("/usr/share/OVMF/OVMF_CODE.fd", "/usr/share/OVMF/OVMF_VARS.fd"),
    ("/usr/share/edk2/x64/OVMF_CODE.4m.fd", "/usr/share/edk2/x64/OVMF_VARS.4m.fd"),
)
PROBES = {"xhci": (b"X", b"x"), "nvme": (b"I", b"i")}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("probe", choices=PROBES)
    parser.add_argument("--iso", type=Path, default=Path("build/FalconOS.iso"))
    parser.add_argument("--timeout", type=int, default=70)
    args = parser.parse_args()

    if not args.iso.is_file():
        raise FileNotFoundError(args.iso)
    matched = next(((Path(a), Path(b)) for a,b in FIRMWARE
                    if Path(a).is_file() and Path(b).is_file()), None)
    if matched is None:
        raise RuntimeError("OVMF firmware is required; SeaBIOS is not a valid V2 test")
    build = Path("build")
    build.mkdir(exist_ok=True)
    nvram = build / ("probe-" + args.probe + "-vars.fd")
    shutil.copyfile(matched[1], nvram)
    trace = (build / ("probe-" + args.probe + "-debug.log")).absolute()
    trace.unlink(missing_ok=True)

    cmd = [
        "qemu-system-x86_64", "-machine", "q35", "-accel", "tcg",
        "-cpu", "max", "-m", "1024", "-smp", "1",
        "-drive", f"if=pflash,unit=0,format=raw,readonly=on,file={matched[0].absolute()}",
        "-drive", f"if=pflash,unit=1,format=raw,file={nvram.absolute()}",
        "-drive", f"file={args.iso.absolute()},if=ide,index=2,media=cdrom",
        "-boot", "order=d", "-display", "none", "-vga", "std",
        "-serial", "none", "-monitor", "none",
        "-debugcon", f"file:{trace}", "-global", "isa-debugcon.iobase=0xe9",
        "-no-reboot",
    ]
    if args.probe == "xhci":
        cmd += ["-device", "qemu-xhci"]
    else:
        # Created exclusively inside build/, never a /dev disk.
        disk = build / "nvme-probe-empty.raw"
        with disk.open("wb") as f:
            f.truncate(16 * 1024 * 1024)
        cmd += [
            "-drive", f"file={disk.absolute()},if=none,format=raw,id=probe-nvme",
            "-device", "nvme,serial=FALCON-READONLY-PROBE,drive=probe-nvme",
        ]
    good, bad = PROBES[args.probe]
    process = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                               stderr=subprocess.PIPE)
    try:
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            if process.poll() is not None:
                detail = process.stderr.read().decode(errors="replace")[-700:]
                raise RuntimeError(f"QEMU stopped before {args.probe} probe: {detail}")
            data = trace.read_bytes() if trace.exists() else b""
            if good in data:
                if args.probe == "xhci":
                    bar = re.search(rb"\\[BAR:([0-9A-F]{8}):([0-9A-F]{8})\\]", data)
                    if not bar:
                        raise AssertionError("xHCI high-BAR diagnostic missing")
                    high, low = (int(part, 16) for part in bar.groups())
                    if high == 0 or (low & 6) != 4:
                        raise AssertionError(
                            f"xHCI test requires an actual 64-bit BAR above 4 GiB, "
                            f"got {high:08X}:{low:08X}"
                        )
                    print(f"PASS: mapped high xHCI PCI BAR {high:08X}:{low:08X}")
                print(f"PASS UEFI/QEMU {args.probe}: actual controller diagnostic marker")
                return
            if bad in data:
                raise AssertionError(f"{args.probe} controller failed, trace={data!r}")
            time.sleep(0.2)
        raise TimeoutError(f"{args.probe} timed out; trace={trace.read_bytes() if trace.exists() else b''!r}")
    finally:
        process.terminate()
        try:
            process.communicate(timeout=4)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()

if __name__ == "__main__":
    main()
