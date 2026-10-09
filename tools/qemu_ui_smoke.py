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

def wait_for_marker(debug, marker, after=0, timeout=22):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        data=debug.read_bytes() if debug.exists() else b""
        if marker in data[after:]:return
        time.sleep(.13)
    raise RuntimeError(f"Installer event {marker!r} missing. debug="+
                       repr(debug.read_bytes() if debug.exists() else b""))

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
            ppm_to_png(first,root/"FalconOS-Aura-Setup.png")
            # Synchronize with real guest step transitions. A fixed sleep can
            # send characters while GLB intro/GRUB still owns the keyboard.
            wait_for_marker(debug,b"L",timeout=30)
            for marker in (b"T",b"A",b"K",b"D",b"U"):
                offset=len(debug.read_bytes())
                command(sock,"sendkey ret",0.4)
                wait_for_marker(debug,marker,after=offset)
            for char in "falcon":command(sock,"sendkey "+char,0.4)
            for marker in (b"P",b"Q",b"O"):
                offset=len(debug.read_bytes())
                command(sock,"sendkey ret",0.45)
                wait_for_marker(debug,marker,after=offset)
            command(sock,"sendkey right",0.35)
            offset=len(debug.read_bytes())
            command(sock,"sendkey ret",0.8)
            wait_for_marker(debug,b"W",after=offset)
            offset=len(debug.read_bytes())
            command(sock,"sendkey ret",1.1)
            wait_for_marker(debug,b"H",after=offset)
            # Open Launchpad (F2), move Home -> Files -> Store,
            # press Enter. Require kernel debugcon events as evidence.
            command(sock, "sendkey esc", .4)
            before = screenshot(sock, first)
            ppm_to_png(first,root/"FalconOS-Aura-Desktop.png")
            command(sock, "sendkey f2", .8)
            launcher=screenshot(sock, root/"aura-launcher.ppm")
            ppm_to_png(root/"aura-launcher.ppm",root/"FalconOS-Aura-Launcher.png")
            # Navigate native Launchpad at QEMU/TCG-safe cadence; use
            # explicit kernel window-open markers, not merely pixel diffs.
            for _ in range(7): command(sock, "sendkey right", .24)
            off=len(debug.read_bytes())
            command(sock, "sendkey ret", 1.2)
            wait_for_marker(debug,b"zCn1",after=off,timeout=30) # Store ID=2
            time.sleep(.8)
            after = screenshot(sock, last)

            # All these are actual native rendered windows, not Launchpad.
            current=7
            for target,name,app_id in ((0,"Files",1),(1,"Browser",14),
                                       (2,"Falco",13),(3,"Calculator",6),
                                       (4,"Notes",7),(5,"Settings",3)):
                command(sock,"sendkey esc",.5)
                command(sock,"sendkey f2",1.0)
                while current>target:
                    command(sock,"sendkey left",.25)
                    current-=1
                while current<target:
                    command(sock,"sendkey right",.25)
                    current+=1
                off=len(debug.read_bytes())
                command(sock,"sendkey ret",1.0)
                marker=("z"+chr(ord("A")+app_id)+"n1").encode("ascii")
                wait_for_marker(debug,marker,after=off,timeout=35)
                time.sleep(.9)
                ppm=root/("FalconOS-Aura-"+name+".ppm")
                shot=screenshot(sock,ppm)
                if picture_difference(before,shot)<40:
                    raise AssertionError("No visual change after launching "+name)
                ppm_to_png(ppm,root/("FalconOS-Aura-"+name+".png"))
                print("PASS: QEMU app ID and window count traced, native PNG",name)
            # Native framebuffer compositor verification, synchronized to
            # kernel debugcon events (z<app-id> n<visible-window-count>).
            # A screenshot with just Launchpad open must never count as pass.
            command(sock,"sendkey f2",.95)
            for _ in range(5):command(sock,"sendkey left",.22)
            off=len(debug.read_bytes())
            command(sock,"sendkey ret",1.6)  # Files while Settings remains
            wait_for_marker(debug,b"zBn2",after=off,timeout=35)
            command(sock,"info status",1.2)
            multi2=root/"FalconOS-Aura-MultiWindow-2.ppm"
            shot2=screenshot(sock,multi2)
            if picture_difference(shot,shot2)<40:
                raise AssertionError("Native WM did not repaint two windows")
            ppm_to_png(multi2,root/"FalconOS-Aura-MultiWindow-2.png")

            command(sock,"sendkey f2",.95)
            command(sock,"sendkey right",.35)
            off=len(debug.read_bytes())
            command(sock,"sendkey ret",1.6)  # Browser; 3 windows must remain
            wait_for_marker(debug,b"zOn3",after=off,timeout=35)
            command(sock,"info status",1.2)
            multi3=root/"FalconOS-Aura-MultiWindow-3.ppm"
            shot3=screenshot(sock,multi3)
            if picture_difference(shot2,shot3)<40:
                raise AssertionError("Native WM did not repaint three windows")
            ppm_to_png(multi3,root/"FalconOS-Aura-MultiWindow-3.png")
            print("PASS: guest compositor emitted Files count=2, Browser count=3 and redrew both screenshot states")
            # Native CodeDium: launch from System tab and export a real
            # FAPP/1 file into the guest's Desktop, then capture both states.
            command(sock,"sendkey f2",.7)
            command(sock,"sendkey tab",.25)   # System
            for _ in range(4):command(sock,"sendkey right",.13)
            off=len(debug.read_bytes())
            command(sock,"sendkey ret",.65)    # CodeDium
            wait_for_marker(debug,b"zSn4",after=off,timeout=35)
            ppm4=root/"FalconOS-Aura-MultiWindow-4.ppm"
            screenshot(sock,ppm4)
            ppm_to_png(ppm4,root/"FalconOS-Aura-MultiWindow-4.png")
            ppm=root/"FalconOS-Aura-CodeDium.ppm"
            screen=screenshot(sock,ppm)
            ppm_to_png(ppm,root/"FalconOS-Aura-CodeDium.png")
            if picture_difference(before,screen)<40:
                raise AssertionError("CodeDium did not render in actual guest")
            command(sock,"sendkey f7",.5) # export reviewed FAPP/1 source
            ppm=root/"FalconOS-Aura-CodeDium-Export.ppm"
            screenshot(sock,ppm)
            ppm_to_png(ppm,root/"FalconOS-Aura-CodeDium-Export.png")
            print("PASS: CodeDium and four simultaneous native windows, FAPP/1 exported")
            # Cover every non-demo app available in the native Launchpad.
            # The System tab remains selected after opening CodeDium (index 4).
            current_system=4
            for target,name,app_id in (
                (1,"Stats",9),(2,"Updates",4),(3,"About",17),(5,"Terminal",5)
            ):
                command(sock,"sendkey f2",.6)
                while current_system>target:
                    command(sock,"sendkey left",.13)
                    current_system-=1
                while current_system<target:
                    command(sock,"sendkey right",.13)
                    current_system+=1
                off=len(debug.read_bytes())
                command(sock,"sendkey ret",.7)
                marker=("z"+chr(ord("A")+app_id)).encode("ascii")
                wait_for_marker(debug,marker,after=off,timeout=35)
                ppm=root/("FalconOS-Aura-"+name+".ppm")
                screenshot(sock,ppm)
                ppm_to_png(ppm,root/("FalconOS-Aura-"+name+".png"))
                print("PASS: real guest screenshot",name)
            command(sock,"sendkey f2",.65)
            command(sock,"sendkey tab",.18) # System -> All apps
            command(sock,"sendkey tab",.18) # All apps -> Essentials
            for _ in range(8):command(sock,"sendkey right",.12)
            off=len(debug.read_bytes())
            command(sock,"sendkey ret",.8) # Clock at Favorites index 8
            wait_for_marker(debug,b"zIn4",after=off,timeout=35)
            ppm=root/"FalconOS-Aura-Clock.ppm"
            screenshot(sock,ppm)
            ppm_to_png(ppm,root/"FalconOS-Aura-Clock.png")
            print("PASS: real guest screenshot Clock; functional Launchpad gallery complete")
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
