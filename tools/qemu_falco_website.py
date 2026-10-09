#!/usr/bin/env python3
"""Real QEMU screenshots of Falco visiting falconos.tech.

F6: guest-native BearSSL only. F7: explicitly requested host-validated HTTPS,
with a plaintext transport confined to the QEMU VM-to-host link. These paths
are never conflated. A screenshot showing an error does not pass this test.
"""
import argparse
from pathlib import Path
import socket
import subprocess
import sys
import time
import urllib.request
import urllib.error

from qemu_smoke import ppm_to_png
from qemu_ui_smoke import command, screenshot, wait_for_marker


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--iso", default="build/FalconOS.iso")
    parser.add_argument("--timeout", type=int, default=65)
    parser.add_argument("--via-host", action="store_true")
    args = parser.parse_args()
    root = Path("build").absolute()
    root.mkdir(parents=True, exist_ok=True)
    monitor_path = root / "falco-website-monitor.sock"
    debug = root / "falco-website-debug.log"
    initial = root / "FalconOS-Falco-Search.ppm"
    site = root / "FalconOS-Falco-falconos-tech.ppm"
    for file in (monitor_path, debug, initial, site):
        if file.exists():
            file.unlink()
    qemu_command = [
        "qemu-system-x86_64", "-accel", "tcg", "-cpu", "max",
        "-m", "1024", "-smp", "1", "-cdrom", args.iso, "-boot", "d",
        "-display", "none", "-vga", "std",
        "-netdev", "user,id=network", "-device", "rtl8139,netdev=network",
        "-serial", "none", "-monitor",
        f"unix:{monitor_path},server=on,wait=off",
        "-debugcon", f"file:{debug}", "-global", "isa-debugcon.iobase=0xe9",
        "-no-reboot"
    ]
    gateway, guest, gateway_log = None, None, None
    try:
        if args.via_host:
            gateway_log = open(root / "falco-gateway.log", "wb")
            gateway = subprocess.Popen(
                [sys.executable, "-u", "tools/falcon_https_gateway.py",
                 "--bind", "127.0.0.1"],
                stdout=gateway_log, stderr=subprocess.STDOUT,
            )
            limit = time.monotonic() + 10
            while time.monotonic() < limit:
                if gateway.poll() is not None:
                    raise RuntimeError("HTTPS gateway exited before VM boot")
                try:
                    with socket.create_connection(("127.0.0.1", 18444), timeout=.3):
                        break
                except OSError:
                    time.sleep(.15)
            else:
                raise TimeoutError("Verified HTTPS gateway not listening")
            # Separate host TLS failures from QEMU VM-to-host TCP failures.
            # Request the exact same path outside the VM first.
            try:
                with urllib.request.urlopen(
                    "http://127.0.0.1:18444/fetch/falconos.tech/", timeout=18
                ) as verify:
                    passed = (verify.status == 200 and
                              verify.headers.get("X-Falcon-Host-HTTPS-Verified") == "yes")
                    excerpt = verify.read(280).decode("utf-8", errors="replace")
                    print("HOST_PREFLIGHT", verify.status, passed, repr(excerpt), flush=True)
                    if not passed:
                        raise RuntimeError("Host fetch was not verified")
            except (urllib.error.HTTPError, urllib.error.URLError, TimeoutError) as exc:
                print("HOST_PREFLIGHT_FAIL", repr(exc), flush=True)
                raise
        guest = subprocess.Popen(
            qemu_command, stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
        limit = time.monotonic() + 15
        while not monitor_path.exists():
            if guest.poll() is not None or time.monotonic() > limit:
                raise RuntimeError("Falco VM monitor did not start")
            time.sleep(.1)
        with socket.socket(socket.AF_UNIX) as monitor:
            monitor.settimeout(7)
            monitor.connect(str(monitor_path))
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
            command(monitor, "sendkey ret", .7)
            screenshot(monitor, initial)
            ppm_to_png(initial, root / "FalconOS-Falco-Search.png")
            start = len(debug.read_bytes())
            # An explicit choice: F6 native TLS, F7 host-verified companion.
            command(monitor, "sendkey f7" if args.via_host else "sendkey f6", .1)
            result = "timeout"
            limit = time.monotonic() + args.timeout
            while time.monotonic() < limit:
                if guest.poll() is not None:
                    raise RuntimeError("Falco VM exited while opening website")
                events = debug.read_bytes()[start:] if debug.exists() else b""
                if b"fY" in events:
                    result = "verified"
                    break
                if b"fN" in events:
                    result = "TLS or HTTP failure"
                    break
                time.sleep(.3)
            time.sleep(1.8)
            screenshot(monitor, site)
            ppm_to_png(site, root / "FalconOS-Falco-falconos-tech.png")
            if result != "verified":
                raise AssertionError(
                    f"Falco site not verified: {result}. "
                    f"Guest trace: {debug.read_bytes()[-140:]!r}"
                )
            if args.via_host:
                access_log = (root / "falco-gateway.log").read_text(
                    encoding="utf-8", errors="replace"
                )
                if ("GET /fetch/falconos.tech/" not in access_log
                        or " 200 " not in access_log):
                    raise AssertionError("The verified host gateway did not return the website")
            method = "host-verified HTTPS (local VM link HTTP)" if args.via_host else "guest-native HTTPS"
            print("PASS: real Falco website rendered from " + method, flush=True)
    finally:
        if guest is not None:
            guest.terminate()
            try:
                guest.wait(timeout=5)
            except subprocess.TimeoutExpired:
                guest.kill()
                guest.wait()
        if gateway is not None:
            gateway.terminate()
            try:
                gateway.wait(timeout=3)
            except subprocess.TimeoutExpired:
                gateway.kill()
                gateway.wait()
        if gateway_log is not None:
            gateway_log.flush()
            gateway_log.close()
            try:
                print("GATEWAY_DIAGNOSTICS",
                      (root / "falco-gateway.log").read_text(
                          encoding="utf-8", errors="replace"
                      )[-3500:], flush=True)
            except OSError:
                pass


if __name__ == "__main__":
    main()
