#!/usr/bin/env python3
"""FalconOS Desktop click-to-run Linux x86_64 launcher (no Chromium).
Uses local QEMU virtual machine, never installs to physical host disk.
Persistent virtual FalconFS image lives ONLY in XDG_DATA_HOME/FalconOS.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parent
ISO=ROOT/"FalconOS.iso"
STORE=Path(os.environ.get("XDG_DATA_HOME",str(Path.home()/".local/share")))/"FalconOS"
DISK=STORE/"falconos-user.raw"
SOCK=STORE/"marketplace.sock"
QEMU=shutil.which("qemu-system-x86_64")

def announce(message):
    try:
        subprocess.run(["zenity","--info","--title=FalconOS","--text="+message],
                       timeout=40,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    except (FileNotFoundError,subprocess.TimeoutExpired):
        print("FalconOS:",message,file=sys.stderr)

def main():
    if len(sys.argv)>1 and sys.argv[1]=="--check":
        expected=["FalconOS.iso","make_safe_disk.py","marketplace_bridge.py",
                  "falcon_https_gateway.py","falco_native_gateway.py"]
        runtime=Path(sys.executable).resolve()
        q=shutil.which("qemu-system-x86_64")
        if not runtime.is_file() or not q or not Path(q).is_file():
            raise RuntimeError("Executable portable Python/QEMU runtime missing")
        missing=[x for x in expected if not (ROOT/x).is_file()]
        if missing:
            print("FalconOS AppImage resource validation failed:",missing,file=sys.stderr)
            return 2
        print("PASS: actual AppImage entrypoint and ISO/native resources verified")
        return 0
    if os.name!="posix" or sys.platform!="linux":
        announce("This release is a Linux x86_64 AppImage, not a Windows EXE.")
        return 1
    if QEMU is None:
        announce("QEMU is not installed. Install qemu-desktop / qemu-system-x86_64 using your distro's software center first.")
        return 2
    if not ISO.is_file():
        announce("FalconOS ISO is missing from this AppImage.")
        return 3
    STORE.mkdir(parents=True,exist_ok=True)
    if not DISK.exists():
        # Safe sparse 4G virtual file. Never access any /dev/nvme* or Windows data.
        subprocess.run([sys.executable,str(ROOT/"make_safe_disk.py"),
                        "--image",str(DISK),"--size","4G"],check=True)
    if SOCK.exists():
        # Never remove a socket belonging to another running instance.
        announce("Another FalconOS session may already be running. Close it first.")
        return 4
    with (STORE/"launcher.log").open("a",encoding="utf-8") as logfile:
        market=subprocess.Popen([sys.executable,str(ROOT/"marketplace_bridge.py"),
                                "--socket",str(SOCK)],
                                stdout=logfile,stderr=subprocess.STDOUT)
        gateway=subprocess.Popen([sys.executable,str(ROOT/"falcon_https_gateway.py"),
                                 "--bind","127.0.0.1"],
                                 stdout=logfile,stderr=subprocess.STDOUT)
        native=subprocess.Popen([sys.executable,str(ROOT/"falco_native_gateway.py"),
                                "--bind","127.0.0.1"],
                                stdout=logfile,stderr=subprocess.STDOUT)
        try:
            qemu_base=[QEMU,"-m","4096","-smp","4",
                       "-cdrom",str(ISO),"-boot","d",
                       "-drive",f"file={DISK},if=ide,index=0,format=raw",
                       "-netdev","user,id=net0",
                       "-device","rtl8139,netdev=net0",
                       "-serial",f"unix:{SOCK},server=on,wait=off",
                       "-vga","std","-global","VGA.vgamem_mb=256",
                       "-accel","kvm","-accel","tcg","-no-reboot"]
            firmware=os.environ.get("FALCON_QEMU_FIRMWARE")
            if firmware and Path(firmware).is_dir():
                qemu_base += ["-L",firmware]
            # GTK is a regular QEMU display backend, not a browser engine.
            with (STORE/"qemu.log").open("a") as qlog:
                cmd=qemu_base+["-display","gtk"]
                proc=subprocess.run(cmd,stdout=qlog,stderr=qlog)
                if proc.returncode!=0:
                    # Alternative display backend on systems without QEMU GTK.
                    proc=subprocess.run(qemu_base+["-display","sdl"],
                                        stdout=qlog,stderr=qlog)
            if proc.returncode!=0:
                announce("FalconOS could not launch. Check ~/.local/share/FalconOS/qemu.log (QEMU GTK/SDL may be missing).")
            return proc.returncode
        finally:
            for process in (market,gateway,native):
                process.terminate()
                try:process.wait(timeout=3)
                except subprocess.TimeoutExpired:process.kill()
            if SOCK.is_socket():
                try:SOCK.unlink()
                except OSError:pass
if __name__=="__main__":
    sys.exit(main())
