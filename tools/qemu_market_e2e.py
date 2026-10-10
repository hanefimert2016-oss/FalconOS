#!/usr/bin/env python3
"""Integration test: real QEMU guest, COM1 transport, guest FAPP installation,
and Terminal launch, with OFFLINE mocked GitHub Release bytes.
This tests guest GUI and serial transport; it does not assert native HTTPS.
"""
import argparse
import hashlib
from pathlib import Path
import socket
import subprocess
import threading
import time
from unittest.mock import patch

import marketplace_bridge as bridge
from qemu_smoke import ppm_to_png

PKG = (
    b"FAPP/1\nid=hello-world\nname=Hello World\nversion=1.0.0\n"
    b"summary=Run inside FalconOS VM\n\n"
    b"clear\necho Downloaded from simulated GitHub Release\nuname\n"
)


def hmp(sock, command, pause=0.24):
    sock.sendall((command + "\n").encode("ascii"))
    time.sleep(pause)


def wait_for_marker(debug, marker, *, after=0, timeout=25):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        data = debug.read_bytes() if debug.exists() else b""
        if marker in data[after:]:
            return len(data)
        time.sleep(0.15)
    raise AssertionError(
        f"Guest did not emit Marketplace event {marker!r}; "
        f"debug={debug.read_bytes()[-100:] if debug.exists() else b''!r}"
    )


def connect_unix(path, timeout=12):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.exists():
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            try:
                sock.connect(str(path))
                return sock
            except (ConnectionRefusedError, OSError):
                sock.close()
        time.sleep(0.15)
    raise TimeoutError("QEMU COM1 socket not accepting connections")


def test_gui(args):
    root = Path("build").absolute()
    root.mkdir(exist_ok=True)
    monitor = root / "market-e2e-monitor.sock"
    serial = root / "market-e2e-serial.sock"
    debug = root / "market-e2e-debug.log"
    screen = root / "market-e2e-screen.ppm"
    for file in (monitor, serial, debug, screen):
        if file.exists():
            file.unlink()
    command = [
        "qemu-system-x86_64", "-accel", "tcg", "-m", "1024", "-smp", "1",
        "-cdrom", args.iso, "-boot", "d", "-display", "none", "-vga", "std",
        "-monitor", f"unix:{monitor},server=on,wait=off",
        "-serial", f"unix:{serial},server=on,wait=off",
        "-debugcon", f"file:{debug}", "-global", "isa-debugcon.iobase=0xe9",
        "-no-reboot",
    ]
    checksum = hashlib.sha256(PKG).hexdigest().encode("ascii") + b"  hello-world-v1.0.0.app.pkg\n"
    record = {
        "asset": {"browser_download_url": "https://example.invalid/pkg"},
        "checksum_asset": {"browser_download_url": "https://example.invalid/pkg.sha256"},
        "version": "1.0.0",
        "release": {"name": "Hello World"},
    }
    state = {"errors": []}

    def fake_download(url):
        return checksum if url.endswith(".sha256") else PKG

    proc = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    serial_sock = None
    try:
        deadline = time.monotonic() + 12
        while not monitor.exists():
            if proc.poll() is not None:
                raise RuntimeError("QEMU stopped before creating monitor")
            if time.monotonic() > deadline:
                raise TimeoutError("QEMU HMP monitor unavailable")
            time.sleep(0.1)
        serial_sock = connect_unix(serial)
        with patch.object(bridge, "releases", return_value={"hello-world": record}), \
             patch.object(bridge, "request_bytes", side_effect=fake_download):
            def serve_bridge():
                try:
                    bridge.serve(serial_sock)
                except (ConnectionError, BrokenPipeError, OSError):
                    return
                except Exception as exc:
                    state["errors"].append(str(exc))
            t = threading.Thread(target=serve_bridge, daemon=True)
            t.start()
            time.sleep(args.boot_seconds)
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as mon:
                mon.settimeout(10)
                mon.connect(str(monitor))
                # Synchronize against actual kernel screen transitions.
                # Avoid lost keystrokes while GRUB and setup are still booting.
                wait_for_marker(debug, b"L", timeout=30)
                for step in (b"T", b"A", b"K", b"D", b"U"):
                    prior = len(debug.read_bytes())
                    hmp(mon, "sendkey ret", 0.35)
                    wait_for_marker(debug, step, after=prior, timeout=12)
                for char in "falcon":
                    hmp(mon, "sendkey " + char, 0.3)
                for step in (b"P", b"Q", b"O"):
                    prior = len(debug.read_bytes())
                    hmp(mon, "sendkey ret", 0.4)
                    wait_for_marker(debug, step, after=prior, timeout=12)
                hmp(mon, "sendkey right", 0.3)  # no additional user
                prior = len(debug.read_bytes())
                hmp(mon, "sendkey ret", 0.7)
                wait_for_marker(debug, b"W", after=prior, timeout=12)
                prior = len(debug.read_bytes())
                hmp(mon, "sendkey ret", 0.7)   # passwordless login
                wait_for_marker(debug, b"H", after=prior, timeout=12)
                hmp(mon, "sendkey esc", 0.4)
                hmp(mon, "sendkey f2", 0.8)
                # Functional app list: Store is favorite slot 7.
                for _ in range(7): hmp(mon, "sendkey right", 0.17)
                hmp(mon, "sendkey ret", 1.0)
                try:
                    wait_for_marker(debug, b"M", timeout=15)
                except AssertionError:
                    # Capture failing GUI state to distinguish lost keys from
                    # serial/device setup regressions. The CI artifact remains.
                    hmp(mon, "screendump " + str(screen), 0.6)
                    if screen.exists():
                        ppm_to_png(screen, args.output)
                    raise
                prior = len(debug.read_bytes())
                hmp(mon, "sendkey r", 0.4)
                wait_for_marker(debug, b"C", after=prior, timeout=20)
                # Search a *real* package, require one actual filtered match.
                hmp(mon, "sendkey f4", 0.32)
                for letter in "world":
                    hmp(mon, "sendkey " + letter, 0.30)
                # Match must be emitted AFTER the last key was submitted,
                # otherwise an earlier prefix match can cause a stale shot.
                prior = len(debug.read_bytes())
                wait_for_marker(debug, b"q1", after=prior, timeout=25)
                wait_for_marker(debug, b"gC", after=prior, timeout=25)
                time.sleep(1.0)
                hmp(mon, "screendump " + str(root / "market-search.ppm"), 0.6)
                ppm_to_png(root / "market-search.ppm",
                           root / "FalconOS-Discover-Search.png")
                hmp(mon, "sendkey f4", 0.4)  # back to Install/Run mode
                prior = len(debug.read_bytes())
                hmp(mon, "sendkey ret", 0.5)
                wait_for_marker(debug, b"I", after=prior, timeout=35)
                prior = len(debug.read_bytes())
                hmp(mon, "sendkey ret", 1.5)
                wait_for_marker(debug, b"R", after=prior, timeout=12)
                hmp(mon, "screendump " + str(screen), 0.6)
                deadline = time.monotonic() + 10
                while not screen.exists():
                    if time.monotonic() > deadline:
                        raise TimeoutError("QEMU failed to capture Terminal screen")
                    time.sleep(0.1)
            ppm_to_png(screen, args.output)
            if state["errors"]:
                raise RuntimeError("Host bridge errors: " + repr(state["errors"]))
            print("PASS real Discover search q1 -> COM1 LIST -> SHA256 verified install -> Terminal launch")
    finally:
        if serial_sock:
            try: serial_sock.close()
            except OSError: pass
        proc.terminate()
        try: proc.communicate(timeout=4)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.communicate()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--iso", default="build/FalconOS.iso")
    p.add_argument("--output", default="build/FalconOS-market-e2e.png")
    p.add_argument("--boot-seconds", type=float, default=7)
    test_gui(p.parse_args())


if __name__ == "__main__":
    main()
