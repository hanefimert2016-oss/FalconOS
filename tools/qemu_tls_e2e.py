#!/usr/bin/env python3
"""Live QEMU TLS 1.2 HTTPS E2E, with CA-signed local certificates.
Valid SAN falcon.test MUST succeed; invalid SAN wrong.test MUST fail.
No host COM1/HTTPS bridge participates.
"""
from pathlib import Path
import argparse
import socket
import ssl
import struct
import subprocess
import threading
import time

PORT=18443
class OneShotTLS:
    def __init__(self,cert,key):
        self.requests=[]
        self.error=None
        self.s=socket.socket()
        self.s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
        self.s.bind(("127.0.0.1",PORT))
        self.s.listen(1)
        self.s.settimeout(75)
        self.ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.ctx.minimum_version=ssl.TLSVersion.TLSv1_2
        self.ctx.maximum_version=ssl.TLSVersion.TLSv1_2
        self.ctx.load_cert_chain(str(cert),str(key))
    def run(self):
        try:
            sock,_=self.s.accept()
            sock.settimeout(12)
            with self.ctx.wrap_socket(sock,server_side=True) as session:
                request=bytearray()
                while b"\r\n\r\n" not in request and len(request)<2048:
                    chunk=session.recv(1024)
                    if not chunk:break
                    request.extend(chunk)
                self.requests.append(bytes(request))
                body=b"falcon tls works"
                session.sendall(
                    b"HTTP/1.0 200 OK\r\nContent-Length: 16\r\n"
                    b"Connection: close\r\n\r\n"+body)
        except (OSError,ssl.SSLError) as e:
            self.error=repr(e)
        finally:
            self.s.close()
def main():
    p=argparse.ArgumentParser()
    p.add_argument("--iso",default="build/FalconOS.iso")
    p.add_argument("--certificate",required=True,type=Path)
    p.add_argument("--key",required=True,type=Path)
    p.add_argument("--expect-failure",action="store_true")
    p.add_argument("--timeout",type=int,default=85)
    args=p.parse_args()
    server=OneShotTLS(args.certificate,args.key)
    threading.Thread(target=server.run,daemon=True).start()
    debug=Path("build/tls-smoke-debug.log")
    if debug.exists():debug.unlink()
    pcap=Path("build/tls-smoke.pcap")
    if pcap.exists():pcap.unlink()
    def traffic():
        try:
            blob=pcap.read_bytes()
            off=24; found=[]
            while off+16<=len(blob):
                n=struct.unpack_from("<I",blob,off+8)[0];off+=16
                if n<14 or off+n>len(blob):break
                eth=blob[off:off+n];off+=n
                if eth[12:14]!=b"\x08\x00" or len(eth)<54:continue
                ihl=(eth[14]&15)*4
                if eth[23]!=6 or len(eth)<14+ihl+20:continue
                seg=eth[14+ihl:]
                hdr=(seg[12]>>4)*4
                found.append({"port":(int.from_bytes(seg[0:2],"big"),
                                      int.from_bytes(seg[2:4],"big")),
                              "flags":hex(seg[13]),"payload":max(0,len(seg)-hdr)})
            return str(found[:36])
        except OSError:return "pcap unavailable"
    cmd=["qemu-system-x86_64","-accel","tcg","-cpu","max",
         "-m","1024","-smp","1","-cdrom",args.iso,
         "-display","none","-vga","std",
         "-netdev","user,id=net0","-device","rtl8139,netdev=net0",
         "-object",f"filter-dump,id=tlswatch,netdev=net0,file={pcap}",
         "-serial","none","-monitor","none",
         "-debugcon",f"file:{debug}","-global","isa-debugcon.iobase=0xe9",
         "-no-reboot"]
    proc=subprocess.Popen(cmd,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        deadline=time.monotonic()+args.timeout
        while time.monotonic()<deadline:
            if proc.poll() is not None:raise RuntimeError("QEMU terminated")
            data=debug.read_bytes() if debug.exists() else b""
            if b"Z" in data or b"z" in data:
                if args.expect_failure:
                    if b"Z" in data:raise AssertionError("TLS accepted INVALID hostname!")
                    print("PASS wrong DNS SAN rejected by guest BearSSL")
                else:
                    if b"z" in data:
                        raise AssertionError("Native HTTPS negotiation failed; debug "+
                                             repr(data)+"; server="+str(server.error))
                    if not server.requests or b"GET /falcon-test" not in server.requests[0]:
                        raise AssertionError("TLS success with no real HTTP request")
                    print("PASS native TCP + TLS 1.2 + X509 SAN/CA + HTTPS authenticated data")
                return
            time.sleep(.2)
        raise TimeoutError("Native TLS test timed out; debug="+repr(
            debug.read_bytes() if debug.exists() else b"")+
            " server="+repr(server.error)+" traffic="+traffic())
    finally:
        proc.terminate()
        try:proc.communicate(timeout=4)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.communicate()
if __name__=="__main__":
    main()
