#!/usr/bin/env python3
"""Script the real first-boot GUI using QEMU's HMP keyboard, capture Store screen.
This is a UI smoke test, not a native HTTPS/Market end-to-end test.
"""
import argparse
from pathlib import Path
import socket
import subprocess
import time
from qemu_smoke import ppm_to_png

def command(sock, line, pause=0.3):
    sock.sendall((line + "\n").encode("ascii"))
    time.sleep(pause)

def screenshot(sock, path):
    path = Path(path).absolute()
    if path.exists():
        path.unlink()
    command(sock, "screendump " + str(path), 0.5)
    deadline = time.monotonic() + 8
    while not path.exists():
        if time.monotonic() > deadline:
            raise TimeoutError("QEMU screenshot was not created")
        time.sleep(0.1)
    return path.read_bytes()

def picture_difference(a, b):
    # PPM P6 headers share resolution; different pixels must reflect UI transitions.
    def split(ppm):
        start = 0
        for _ in range(3):
            start = ppm.index(b"\n", start) + 1
        return ppm[start:]
    a, b = split(a), split(b)
    if len(a) != len(b):
        return 10000
    stride = max(3, len(a) // 6000)
    return sum(a[i] != b[i] for i in range(0, len(a), stride))

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--iso", default="build/FalconOS.iso")
    p.add_argument("--output", default="build/FalconOS-Store-screen.png")
    p.add_argument("--boot-seconds", type=float, default=7)
    args = p.parse_args()
    root = Path("build")
    root.mkdir(exist_ok=True)
    monitor = (root / "ui-monitor.sock").absolute()
    first = (root / "ui-before.ppm").absolute()
    last = (root / "ui-after.ppm").absolute()
    debug = (root / "ui-debugcon.log").absolute()
    if debug.exists(): debug.unlink()
    if monitor.exists(): monitor.unlink()
    cmd = ["qemu-system-x86_64", "-accel", "tcg", "-m", "1024", "-smp", "1",
           "-cdrom", args.iso, "-boot", "d", "-display", "none", "-vga", "std",
           "-serial", "none", "-monitor", "unix:" + str(monitor) + ",server=on,wait=off",
           "-debugcon", "file:" + str(debug), "-global", "isa-debugcon.iobase=0xe9",
           "-no-reboot"]
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        deadline = time.monotonic() + 10
        while not monitor.exists():
            if proc.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError("QEMU UI test did not start")
            time.sleep(.1)
        time.sleep(args.boot_seconds)
        with socket.socket(socket.AF_UNIX) as sock:
            sock.settimeout(5)
            sock.connect(str(monitor))
            before = screenshot(sock, first)
            # Setup: language, theme, accent, keyboard, secure disk.
            for _ in range(5): command(sock, "sendkey ret", 0.4)
            for char in "falcon": command(sock, "sendkey " + char, 0.3)
            # username + blank password and confirmation
            for _ in range(3): command(sock, "sendkey ret", 0.45)
            # "Add another user?" -> no
            command(sock, "sendkey right", 0.3)
            command(sock, "sendkey ret", 1)
            # lock screen without password
            command(sock, "sendkey ret", 1.5)
            # Open Launchpad (F2), move Home -> Files -> Store,
            # press Enter. Require kernel debugcon events as evidence.
            command(sock, "sendkey esc", .4)
            before = screenshot(sock, first)
            command(sock, "sendkey f2", .8)
            launcher=screenshot(sock, root/"aura-launcher.ppm")
            ppm_to_png(root/"aura-launcher.ppm",root/"FalconOS-Aura-Launcher.png")
            # Functional Shelf launcher: Store is eighth favorite (slot 7).
            for _ in range(7): command(sock, "sendkey right", .17)
            command(sock, "sendkey ret", 1.5)
            after = screenshot(sock, last)
        score = picture_difference(before, after)
        ppm_to_png(last, args.output)
        events = debug.read_bytes() if debug.exists() else b""
        if score < 100 or b"S" not in events or b"M" not in events:
            raise RuntimeError("Store not verified: image difference=" + str(score) +
                               ", debug events=" + repr(events[-200:]))
        print("PASS: QEMU Store opened AND rendered (debug markers S/M), changed pixels", score)
    finally:
        proc.terminate()
        try: proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()

if __name__ == "__main__":
    main()
