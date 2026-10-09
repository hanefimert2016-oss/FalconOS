#!/usr/bin/env python3
"""Fetch pinned upstream BearSSL 0.6 as a build-only TLS dependency.

The official archive SHA-256 below is fixed. Never extract or compile an
unverified download. The TLS kernel feature is opt-in until QEMU E2E passes.
"""
import hashlib
import io
import pathlib
import tarfile
import urllib.request

URL="https://www.bearssl.org/bearssl-0.6.tar.gz"
SHA256="6705bba1714961b41a728dfc5debbe348d2966c117649392f8c8139efc83ff14"
OUTPUT=pathlib.Path("third_party/bearssl")
def main():
    if (OUTPUT/"inc"/"bearssl.h").exists():
        print("BearSSL sources already present; verify your existing checkout")
        return
    with urllib.request.urlopen(URL,timeout=30) as response:
        content=response.read(3_000_000)
    actual=hashlib.sha256(content).hexdigest()
    if actual != SHA256:
        raise RuntimeError(f"BearSSL 0.6 SHA-256 mismatch: {actual}")
    OUTPUT.parent.mkdir(exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(content),mode="r:gz") as archive:
        members=archive.getmembers()
        root=members[0].name.split("/")[0]
        for member in members:
            p=pathlib.PurePosixPath(member.name)
            if not p.parts or p.parts[0]!=root or ".." in p.parts or member.issym() or member.islnk():
                raise RuntimeError("Unexpected BearSSL archive member")
            if not (member.isfile() or member.isdir()):
                raise RuntimeError("Unexpected BearSSL file type")
        # The validated upstream archive contains only files and directories.
        archive.extractall(OUTPUT.parent,filter="data")
    extracted=OUTPUT.parent/root
    if extracted!=OUTPUT:
        extracted.rename(OUTPUT)
    print("Pinned BearSSL 0.6 source verified and unpacked.")
if __name__=="__main__":
    main()
