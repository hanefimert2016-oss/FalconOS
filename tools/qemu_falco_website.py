#!/usr/bin/env python3
"""QEMU framebuffer proof: open falconos.tech from the actual Falco application.

Unlike a static design mockup, these PNGs are taken from the running guest.
A loaded page is only reported after native TLS emits a verified render marker.
Network failure produces an honest diagnostic screenshot and a failing test.
"""
from pathlib import Path
import argparse
import socket
import subprocess
import time

from qemu_smoke import ppm_to_png
from qemu_ui_smoke import command, screenshot, wait_for_marker

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--iso", default="build/FalconOS.iso")
    parser.add_argument("--timeout", type=int, default=65)
    args = parser.parse_args()
    root = Path("build").absolute()
    root.mkdir(parents=True, exist_ok=True)
    mon = root / "falco-website-monitor.sock"
    debug = root / "falco-website-debug.log"
    initial = root / "FalconOS-Falco-Search.ppm"
    site = root / "FalconOS-Falco-falconos-tech.ppm"
    for file in (mon, debug, initial, site):
        if file.exists():
            file.unlink()
    cmd = [
        "qemu-system-x86_64", "-accel", "tcg", "-cpu", "max",
        "-m", "1024", "-smp", "1", "-cdrom", args.iso, "-boot", "d",
        "-display", "none", "-vga", "std",
        "-netdev", "user,id=network", "-device", "rtl8139,netdev=network",
        "-serial", "none", "-monitor", f"unix:{mon},server=on,wait=off",
        "-debugcon", f"file:{debug}", "-global", "isa-debugcon.iobase=0xe9",
        "-no-reboot"
    ]
    process = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
    try:
        deadline = time.monotonic() + 15
        while not mon.exists():
            if process.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError("Falco QEMU monitor did not start")
            time.sleep(.1)
        with socket.socket(socket.AF_UNIX) as monitor:
            monitor.settimeout(7)
            monitor.connect(str(mon))
            wait_for_marker(debug, b"L", timeout=40)
            for marker in (b"T", b"A", b"K", b"D", b"U"):
                offset = len(debug.read_bytes())
                command(monitor, "sendkey ret", .3)
                wait_for_marker(debug, marker, after=offset)
            for letter in "falcon":
                command(monitor, "sendkey " + letter, .26)
            for marker in (b"P", b"Q", b"O"):
                offset = len(debug.read_bytes())
                command(monitor, "sendkey ret", .3)
                wait_for_marker(debug, marker, after=offset)
            command(monitor, "sendkey right", .3)
            offset = len(debug.read_bytes())
            command(monitor, "sendkey ret", .5)
            wait_for_marker(debug, b"W", after=offset)
            offset = len(debug.read_bytes())
            command(monitor, "sendkey ret", .6)
            wait_for_marker(debug, b"H", after=offset)
            command(monitor, "sendkey esc", .3)
            command(monitor, "sendkey f2", .7)
            command(monitor, "sendkey right", .25)
            command(monitor, "sendkey right", .25)
            command(monitor, "sendkey ret", .7)  # Favorites index 2 = Falco
            screenshot(monitor, initial)
            ppm_to_png(initial, root / "FalconOS-Falco-Search.png")
            before = len(debug.read_bytes())
            command(monitor, "sendkey f6", .1)  # Open https://falconos.tech
            result = "timeout"
            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError("Falco QEMU exited unexpectedly")
                data = debug.read_bytes()[before:] if debug.exists() else b""
                if b"bY" in data:
                    result = "verified"
                    break
                if b"bN" in data:
                    result = "TLS/render failure"
                    break
                time.sleep(.3)
            time.sleep(1.8)
            screenshot(monitor, site)
            ppm_to_png(site, root / "FalconOS-Falco-falconos-tech.png")
            if result != "verified":
                raise AssertionError(
                    f"Falco falconos.tech did not pass native HTTPS: {result}; "
                    f"guest debug={debug.read_bytes()[-140:]!r}"
                )
            print("PASS: Falco navigated to falconos.tech and rendered"
                  " native certificate-validated HTML-to-text", flush=True)
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()

if __name__ == "__main__":
    main()
