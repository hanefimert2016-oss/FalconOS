#!/usr/bin/env python3
"""Prove simultaneous TCP sockets have separate 4-tuples and RX buffers."""
from pathlib import Path
import socket,subprocess,threading,time

class Server:
    def __init__(self,port,reply):
        self.received=b""
        self.error=None
        self.reply=reply
        self.listener=socket.socket()
        self.listener.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
        self.listener.bind(("127.0.0.1",port))
        self.listener.listen(1)
        self.listener.settimeout(35)
    def run(self):
        try:
            peer,_=self.listener.accept()
            with peer:
                peer.settimeout(10)
                self.received=peer.recv(1024)
                peer.sendall(self.reply)
        except OSError as ex:self.error=str(ex)
        finally:self.listener.close()

one=Server(18081,b"1 server one")
two=Server(18082,b"2 server two")
threading.Thread(target=one.run,daemon=True).start()
threading.Thread(target=two.run,daemon=True).start()

debug=Path("build/tcp-multi-debug.log")
debug.parent.mkdir(exist_ok=True)
if debug.exists():debug.unlink()
cmd=["qemu-system-x86_64","-accel","tcg","-m","1024","-smp","1",
     "-cdrom","build/FalconOS.iso","-display","none","-vga","std",
     "-netdev","user,id=net0","-device","rtl8139,netdev=net0",
     "-serial","none","-monitor","none",
     "-debugcon",f"file:{debug}","-global","isa-debugcon.iobase=0xe9",
     "-no-reboot"]
proc=subprocess.Popen(cmd,stderr=subprocess.PIPE,stdout=subprocess.DEVNULL)
try:
    limit=time.monotonic()+58
    while time.monotonic()<limit:
        if proc.poll() is not None:
            raise RuntimeError("QEMU died unexpectedly")
        content=debug.read_bytes() if debug.exists() else b""
        if b"Q" in content:
            assert one.received==b"first",one.received
            assert two.received==b"second",two.received
            print("PASS two concurrent TCP connections, distinct destination ports and replies")
            break
        if b"q" in content:
            raise AssertionError(f"Multi TCP failed: {content!r}, servers {one.received!r}/{two.received!r} errors {one.error!r}/{two.error!r}")
        time.sleep(.2)
    else:raise TimeoutError(f"Concurrent TCP timed out: {debug.read_bytes() if debug.exists() else b''!r}")
finally:
    proc.terminate()
    try:proc.communicate(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill();proc.communicate()
