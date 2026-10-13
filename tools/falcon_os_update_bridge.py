#!/usr/bin/env python3
"""FalconOS native OS ISO updater (COM2), independent of Marketplace COM1.

A real update stages a DIFFERENT verified, immutable ISO as the QEMU
CD-ROM media. A normal user reboot loads the new kernel from FalconBoot.
The host never writes to the FalconFS disk or arbitrary user paths.
HTTPS source is fixed to the owner-controlled Marketplace/updates branch.
"""
import argparse
import hashlib
import os
import re
import socket
import tempfile
import time
import urllib.request
from pathlib import Path

MANIFEST_ROOT="https://raw.githubusercontent.com/hanefimert2016-oss/FalconOS-Marketplace/updates/os/"
MANIFEST_URL=MANIFEST_ROOT+"latest.fosup"  # legacy approved Beta feed
CHANNEL_MANIFESTS={
    "stable":MANIFEST_ROOT+"stable/latest.fosup",
    "beta":MANIFEST_URL,
    "alpha":MANIFEST_ROOT+"alpha/latest.fosup",
}
ASSET_ROOT="https://github.com/hanefimert2016-oss/FalconOS-Marketplace/releases/download/"
MAX_ISO=128*1024*1024
VERSION=re.compile(r"2\.[0-9]{1,4}\.[0-9]{1,4}(?:-(?:beta|alpha))?\Z")
HEX=re.compile(r"[0-9a-f]{64}\Z")

def semver(v):
    if not VERSION.fullmatch(v):raise ValueError("unsupported update version")
    parts=v.split("-")
    a,b,c=parts[0].split(".")
    stage=parts[1] if len(parts)>1 else "stable"
    return (int(a),int(b),int(c),{"alpha":0,"beta":1,"stable":2}[stage])

def manifest_url(channel):
    if channel not in CHANNEL_MANIFESTS:
        raise ValueError("unsupported update channel")
    return CHANNEL_MANIFESTS[channel]

def version_in_channel(version,channel):
    if channel=="stable":return "-" not in version
    return version.endswith("-"+channel)

def parse_manifest(raw,channel="beta"):
    if len(raw)>1200 or not raw.isascii():raise ValueError("invalid manifest")
    lines=raw.decode("ascii").splitlines()
    if not lines or lines[0]!="FOSUP/1" or len(lines)!=6:
        raise ValueError("invalid FOSUP manifest")
    data={}
    for line in lines[1:]:
        k,sep,v=line.partition("=")
        if not sep or k in data or k not in ("version","tag","asset","sha256","bytes"):
            raise ValueError("invalid manifest key")
        data[k]=v
    if set(data)!={"version","tag","asset","sha256","bytes"}:
        raise ValueError("missing manifest field")
    semver(data["version"])
    manifest_url(channel)  # validate fixed feed selection, no URL injection
    if not version_in_channel(data["version"],channel):
        raise ValueError("manifest version does not match selected release channel")
    expected=f"os-v{data['version']}"
    if data["tag"]!=expected or data["asset"]!=f"FalconOS-{data['version']}.iso":
        raise ValueError("release tag or filename mismatch")
    if not HEX.fullmatch(data["sha256"]) or not data["bytes"].isdigit():
        raise ValueError("invalid digest or size")
    length=int(data["bytes"])
    if not (1024*1024<=length<=MAX_ISO):
        raise ValueError("ISO exceeds safe length limits")
    data["size"]=length
    return data

def read_manifest(channel="beta"):
    request=urllib.request.Request(manifest_url(channel),
                                   headers={"User-Agent":"FalconOS-System-Updater/1"})
    with urllib.request.urlopen(request,timeout=22) as r:
        return parse_manifest(r.read(1201),channel)

def download_verified(info,destination):
    url=ASSET_ROOT+info["tag"]+"/"+info["asset"]
    request=urllib.request.Request(url,headers={"User-Agent":"FalconOS-System-Updater/1"})
    destination=Path(destination)
    destination.parent.mkdir(parents=True,exist_ok=True)
    fd,tmp=tempfile.mkstemp(prefix=".falcon-update-",suffix=".part",dir=destination.parent)
    total=0;digest=hashlib.sha256()
    try:
        with os.fdopen(fd,"wb") as output,urllib.request.urlopen(request,timeout=70) as source:
            while True:
                chunk=source.read(65536)
                if not chunk:break
                total+=len(chunk)
                if total>info["size"]:raise ValueError("ISO exceeds manifest size")
                digest.update(chunk);output.write(chunk)
            output.flush();os.fsync(output.fileno())
        if total!=info["size"] or digest.hexdigest()!=info["sha256"]:
            raise ValueError("ISO SHA-256 or size mismatch")
        with open(tmp,"rb") as f:
            f.seek(0x8001)
            if f.read(5)!=b"CD001":raise ValueError("not an ISO9660 image")
        os.replace(tmp,destination)
        return destination
    finally:
        if os.path.exists(tmp):os.unlink(tmp)

def command_hmp(monitor,iso):
    if any(c in str(iso) for c in ('"',"'",'\n','\r',';')):raise ValueError("invalid path")
    # Exact device ID is created with -drive if=ide,index=2,id=falcon_os_cd.
    with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as s:
        s.settimeout(12);s.connect(monitor)
        try:s.recv(4096)
        except socket.timeout:pass
        s.sendall(("change falcon_os_cd "+str(iso)+"\n").encode("ascii"))
        response=b""
        deadline=time.monotonic()+10
        while time.monotonic()<deadline:
            try:response+=s.recv(4096)
            except socket.timeout:break
            if b"(qemu)" in response:break
        if any(e in response.lower() for e in (b"error",b"could not",b"unknown",b"not found",b"failed")):
            raise ValueError("QEMU rejected CD-ROM change: "+response.decode("ascii","replace")[-150:])
        # Query the live QEMU block state, not just the HMP return code.
        s.sendall(b"info block\n")
        verify=b"";deadline=time.monotonic()+10
        while time.monotonic()<deadline:
            try:verify+=s.recv(4096)
            except socket.timeout:break
            if b"(qemu)" in verify:break
        if str(iso).encode() not in verify:
            raise ValueError("QEMU CD-ROM change not reflected in info block")

def send(sock,reply):
    sock.sendall((reply+"\n").encode("ascii"))

def activate_on_next_boot(staging,target):
    """Persist QEMU's selected boot ISO without touching real disks.

    The original build/FalconOS.iso remains the recovery image. Only a
    verified file inside build/os-updates may become active. The relative
    symlink is atomically replaced to survive VM shutdown/relaunch.
    """
    staging=Path(staging).resolve()
    target=Path(target).resolve()
    if target.parent!=staging or not target.is_file():
        raise ValueError("refusing OS media outside controlled update storage")
    new=staging/(".active-"+str(os.getpid())+".tmp")
    try:
        new.symlink_to(target.name)
        os.replace(new,staging/"active.iso")
    finally:
        if new.is_symlink():new.unlink()

def command_loop(sock,monitor,staging):
    choice=None;have=None;data=bytearray()
    while True:
        c=sock.recv(1)
        if not c:raise ConnectionError("guest disconnected")
        if c!=b"\n":
            if c in b"\r":continue
            if not(32<=c[0]<=126) or len(data)>=130:
                data.clear()
                continue
            data+=c
            continue
        command=data.decode("ascii");data.clear()
        try:
            if command.startswith("CHECK|"):
                args=command[6:].split("|")
                if len(args)!=2:
                    raise ValueError("CHECK requires installed version and channel")
                current,channel=args
                semver(current)
                manifest_url(channel)
                choice=None
                try:
                    choice=read_manifest(channel)
                except urllib.error.HTTPError as ex:
                    if ex.code==404:
                        send(sock,"EMPTY|"+channel)
                        continue
                    raise
                if semver(choice["version"])>semver(current):
                    send(sock,"AVAILABLE|"+choice["version"])
                else:
                    send(sock,"CURRENT|"+current)
            elif command.startswith("INSTALL|"):
                version=command[8:]
                if choice is None or choice["version"]!=version:
                    raise ValueError("check update before install")
                send(sock,"BUSY|downloading")
                target=Path(staging)/choice["asset"]
                if not target.exists() or hashlib.sha256(target.read_bytes()).hexdigest()!=choice["sha256"]:
                    download_verified(choice,target)
                command_hmp(monitor,target.resolve())
                activate_on_next_boot(staging,target)
                have=version
                send(sock,"STAGED|"+have)
            elif command:
                send(sock,"ERROR|Unknown update command")
        except (ValueError,OSError,TimeoutError,urllib.error.URLError) as exc:
            print("Update rejected:",exc,flush=True)
            safe=str(exc).replace("|"," ").replace("\n"," ")[:100]
            send(sock,"ERROR|"+safe)

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--socket",default="build/falcon-update.sock")
    p.add_argument("--monitor",default="build/falcon-update-monitor.sock")
    p.add_argument("--staging",default="build/os-updates")
    args=p.parse_args()
    sock=Path(args.socket)
    deadline=time.monotonic()+45
    while not sock.exists():
        if time.monotonic()>deadline:raise TimeoutError("QEMU updater COM2 unavailable")
        time.sleep(.15)
    with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as s:
        s.connect(str(sock));print("FalconOS System Update bridge connected (no publish rights)",flush=True)
        command_loop(s,args.monitor,args.staging)
if __name__=="__main__":
    main()
