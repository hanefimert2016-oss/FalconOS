#!/usr/bin/env python3
"""Native QEMU TCP/HTTP smoke:
host process listens on loopback:18080; guest RTL8139->ARP->TCP->HTTP
must complete in its own x86_64 kernel, without the serial host bridge.
"""
import argparse
from pathlib import Path
import socket
import subprocess
import threading
import time

HOST_PORT=18080
class OneShotHTTP:
    def __init__(self):
        self.error=None
        self.requests=[]
        self.socket=socket.socket(socket.AF_INET,socket.SOCK_STREAM)
        self.socket.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
        self.socket.bind(("127.0.0.1",HOST_PORT))
        self.socket.listen(1)
        self.socket.settimeout(60)
    def serve(self):
        try:
            client,_=self.socket.accept()
            client.settimeout(5)
            with client:
                raw=bytearray()
                while b"\r\n\r\n" not in raw and len(raw)<4096:
                    data=client.recv(2048)
                    if not data:break
                    raw.extend(data)
                self.requests.append(bytes(raw))
                body=b"falcon tcp works"
                client.sendall(b"HTTP/1.0 200 OK\r\nContent-Length: 16\r\n"
                               b"Connection: close\r\n\r\n"+body)
        except OSError as e:
            self.error=str(e)
        finally:
            self.socket.close()

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--iso",default="build/FalconOS.iso")
    ap.add_argument("--timeout",type=float,default=65)
    args=ap.parse_args()
    server=OneShotHTTP()
    thread=threading.Thread(target=server.serve,daemon=True)
    thread.start()
    debug=Path("build/tcp-smoke-debug.log")
    if debug.exists():debug.unlink()
    cmd=["qemu-system-x86_64","-accel","tcg","-m","1024",
         "-smp","1","-cdrom",args.iso,"-display","none","-vga","std",
         "-netdev","user,id=net0","-device","rtl8139,netdev=net0",
         "-serial","none","-monitor","none",
         "-debugcon",f"file:{debug}","-global","isa-debugcon.iobase=0xe9",
         "-no-reboot"]
    proc=subprocess.Popen(cmd,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        deadline=time.monotonic()+args.timeout
        while time.monotonic()<deadline:
            if proc.poll() is not None:
                raise RuntimeError("QEMU exited unexpectedly")
            data=debug.read_bytes() if debug.exists() else b""
            if b"T" in data:
                if not server.requests or b"GET /falcon-test HTTP/1.0" not in server.requests[0]:
                    raise AssertionError("Guest marker appeared without valid native request")
                print("PASS guest RTL8139 -> native TCP handshake -> HTTP server -> kernel response")
                return
            if b"t" in data:
                raise AssertionError("Native TCP HTTP failed; debug="+repr(data)+
                                     " server_request="+repr(server.requests))
            time.sleep(.2)
        raise TimeoutError("Guest TCP smoke exceeded timeout; debug="+repr(
            debug.read_bytes() if debug.exists() else b"")+
            " server_request="+repr(server.requests)+" error="+repr(server.error))
    finally:
        proc.terminate()
        try:proc.communicate(timeout=4)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.communicate()

if __name__=="__main__":
    main()
