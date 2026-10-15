#!/usr/bin/env python3
"""Headless x86_64 QEMU smoke: boot ISO, capture display, check visual output."""
import argparse
import os
from pathlib import Path
import socket
import shutil
import struct
import subprocess
import time
import zlib

def png_chunk(kind, payload):
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xffffffff)

def ppm_to_png(src, dest):
    with open(src, "rb") as f:
        if f.readline().strip() != b"P6":
            raise RuntimeError("QEMU did not produce a P6 RGB screenshot")
        line = f.readline()
        while line.startswith(b"#"):
            line = f.readline()
        width, height = map(int, line.split())
        if f.readline().strip() != b"255":
            raise RuntimeError("Unsupported PPM maxval")
        pixels = f.read()
    if width < 640 or height < 400 or len(pixels) != width * height * 3:
        raise RuntimeError("Video output missing or incorrect geometry")
    stride = max(3, (len(pixels) // 3000 // 3) * 3)
    colored = 0
    diverse = set()
    for i in range(0, len(pixels) - 2, stride):
        rgb = tuple(pixels[i:i + 3])
        diverse.add(rgb)
        if max(rgb) - min(rgb) >= 20:
            colored += 1
    if len(diverse) < 12 or colored < 20:
        raise RuntimeError(f"Screen appears blank or monochrome ({len(diverse)} colors)")
    # A PNG stores each scanline prefixed by filter byte 0.
    rows = b"".join(b"\x00" + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))
    png = b"\x89PNG\r\n\x1a\n"
    png += png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += png_chunk(b"IDAT", zlib.compress(rows, 8))
    png += png_chunk(b"IEND", b"")
    Path(dest).write_bytes(png)
    print(f"PASS: QEMU graphics screenshot {width}x{height}, {len(diverse)} sampled colors")

def main():
    a = argparse.ArgumentParser()
    a.add_argument("--iso", default="build/FalconOS.iso")
    a.add_argument("--disk", help="Optional persistent raw data disk; CD-ROM still boots first")
    a.add_argument("--output", default="build/falcon-boot-smoke.png")
    a.add_argument("--boot-seconds", type=int, default=18)
    opts = a.parse_args()
    build = Path("build")
    build.mkdir(exist_ok=True)
    mon = (build / "smoke-monitor.sock").absolute()
    screen = (build / "smoke-screen.ppm").absolute()
    for path in (mon, screen):
        if path.exists():
            path.unlink()
    # FalconOS V2 is UEFI-only: the old SeaBIOS smoke launcher showed a
    # monochrome firmware error instead of booting FalconBoot.  Keep this
    # screenshot test a genuine OVMF/QEMU framebuffer assertion.
    firmware = [
        (Path("/usr/share/OVMF/OVMF_CODE_4M.fd"), Path("/usr/share/OVMF/OVMF_VARS_4M.fd")),
        (Path("/usr/share/OVMF/OVMF_CODE.fd"), Path("/usr/share/OVMF/OVMF_VARS.fd")),
        (Path("/usr/share/edk2/x64/OVMF_CODE.4m.fd"), Path("/usr/share/edk2/x64/OVMF_VARS.4m.fd")),
    ]
    pair = next(((code, var) for code, var in firmware
                 if code.is_file() and var.is_file()), None)
    if pair is None:
        raise RuntimeError("FalconOS V2 smoke requires installed OVMF UEFI firmware")
    nvram = (build / "smoke-ovmf-vars.fd").absolute()
    shutil.copyfile(pair[1], nvram)
    debug = (build / "smoke-debug.log").absolute()
    command = [
        "qemu-system-x86_64", "-machine", "q35", "-accel", "tcg",
        "-m", "4096", "-smp", "2",
        "-drive", f"if=pflash,unit=0,format=raw,readonly=on,file={pair[0]}",
        "-drive", f"if=pflash,unit=1,format=raw,file={nvram}",
        "-drive", f"file={Path(opts.iso).absolute()},if=ide,index=2,media=cdrom",
        "-boot", "order=d", "-display", "none",
        "-vga", "std", "-serial", "none",
        "-debugcon", f"file:{debug}", "-global", "isa-debugcon.iobase=0xe9",
        "-monitor", f"unix:{mon},server=on,wait=off", "-no-reboot"
    ]
    if opts.disk:
        disk = Path(opts.disk)
        if not disk.is_file():
            raise FileNotFoundError(f"Missing QEMU data disk: {disk}")
        command.extend(["-drive", f"file={disk},format=raw,if=ide,index=0"])
    proc = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        deadline = time.monotonic() + 15
        while not mon.exists():
            if proc.poll() is not None:
                raise RuntimeError("QEMU exited before creating monitor")
            if time.monotonic() > deadline:
                raise TimeoutError("QEMU monitor socket not ready")
            time.sleep(0.1)
        time.sleep(opts.boot_seconds)
        if proc.poll() is not None:
            raise RuntimeError("FalconOS VM exited before screenshot")
        with socket.socket(socket.AF_UNIX) as s:
            s.settimeout(15)
            s.connect(str(mon))
            s.sendall(("screendump " + str(screen) + "\n").encode())
            deadline = time.monotonic() + 15
            while not screen.exists():
                if time.monotonic() > deadline:
                    raise TimeoutError("QEMU could not capture framebuffer")
                time.sleep(0.1)
        try:
            ppm_to_png(screen, opts.output)
        except RuntimeError as exc:
            diag=debug.read_text(encoding="ascii",errors="replace") if debug.exists() else ""
            raise RuntimeError(f"{exc}; FalconBoot debug tail: {diag[-350:]}") from exc
    finally:
        proc.terminate()
        try:
            proc.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.communicate()
        if mon.exists():
            mon.unlink()

if __name__ == "__main__":
    main()
