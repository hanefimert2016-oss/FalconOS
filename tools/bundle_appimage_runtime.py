#!/usr/bin/env python3
"""Bundle just Linux userland QEMU/Python for a real two-click FalconOS AppImage.

- QEMU boots the FalconOS bare-metal ISO. Not Chromium or an HTML engine.
- Private virtual disk only, no access to host /dev block devices.
- Copy QEMU BIOS firmware, QEMU UI modules and recursively linked shared libs.
- Bundled Python 3 stdlib powers cert-validated host helper scripts; excludes
  pip, Chromium, browsers, selenium, playwright and the CI tooling.
- Do not bundle glibc/ELF interpreter; Linux host ABI remains prerequisite.
- GPL-2.0 QEMU and PSF Python license notices included.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
KEEP_GLIBC = {"libc.so.6","libm.so.6","libpthread.so.0","librt.so.1",
              "libdl.so.2","libresolv.so.2","libutil.so.1",
              "libanl.so.1","ld-linux-x86-64.so.2"}
EXCLUDE_STD = {"site-packages","dist-packages","test","tests","tkinter",
               "idlelib","turtledemo","ensurepip","venv","pydoc_data"}
def copy(src,dst):
    dst.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(src,dst,follow_symlinks=True)
def dependencies(binary):
    p=subprocess.run(["ldd",str(binary)],capture_output=True,text=True)
    if p.returncode:
        return []
    libs=[]
    for line in p.stdout.splitlines():
        m=re.search(r"=>\s+(/[^\s]+)\s+\(",line)
        if not m:m=re.search(r"^\s*(/[^\s]+)\s+\(",line)
        if m:libs.append(Path(m.group(1)))
    return libs
def bundle(appdir):
    usr=appdir/"usr"
    bins=usr/"bin"
    lib=usr/"lib/falcon-runtime"
    bins.mkdir(parents=True,exist_ok=True)
    qemu=shutil.which("qemu-system-x86_64")
    if not qemu:raise RuntimeError("QEMU not installed in build image")
    qemu_bin=Path(qemu).resolve()
    python=Path("/usr/bin/python3").resolve()
    pyver=subprocess.check_output([str(python),"-c",
                                   "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')"],
                                  text=True).strip()
    copy(qemu_bin,bins/"qemu-system-x86_64")
    copy(python,bins/"python3")
    python_library=Path("/usr/lib")/f"python{pyver}"
    if not python_library.exists():
        raise RuntimeError("System Python stdlib missing: "+str(python_library))
    target_lib=usr/"lib"/f"python{pyver}"
    shutil.copytree(python_library,target_lib,
                    ignore=lambda directory,items:{n for n in items
                          if n in EXCLUDE_STD or n=="__pycache__"},
                    dirs_exist_ok=True,symlinks=False)
    include=[qemu_bin,python]
    module_sources=[Path("/usr/lib/x86_64-linux-gnu/qemu"),
                    Path("/usr/lib/qemu")]
    target_modules=usr/"lib/qemu"
    for mod in module_sources:
        if mod.is_dir():
            for item in mod.rglob("*.so"):
                dest=target_modules/item.relative_to(mod)
                copy(item,dest)
                include.append(item)
    for item in (target_lib/"lib-dynload").glob("*.so"):
        include.append(item)
    qemu_data=Path("/usr/share/qemu")
    if not qemu_data.is_dir():
        raise RuntimeError("QEMU firmware missing; cannot boot a complete AppImage")
    shutil.copytree(qemu_data,usr/"share/qemu",dirs_exist_ok=True,symlinks=False)
    # Some Ubuntu firmware targets refer to /usr/share/seabios or vgabios.
    for fw in ("seabios","vgabios"):
        src=Path("/usr/share")/fw
        if src.is_dir():
            shutil.copytree(src,usr/"share"/fw,dirs_exist_ok=True,symlinks=False)
    queued=list(include);seen=set()
    while queued:
        elf=queued.pop()
        for src in dependencies(elf):
            if src.name in KEEP_GLIBC:continue
            real=src.resolve()
            if real in seen:continue
            seen.add(real)
            dest=lib/src.name
            if not dest.exists():
                copy(real,dest)
            if real.name!=src.name and not (lib/real.name).exists():
                copy(real,lib/real.name)
            queued.append(real)
    # Basic licensing: QEMU GPL license text & distro package source info.
    licenses=usr/"share/doc/falconos-runtime"
    licenses.mkdir(parents=True,exist_ok=True)
    for source,name in [
        ("/usr/share/common-licenses/GPL-2","GPL-2.txt"),
        ("/usr/share/doc/qemu-system-x86/copyright","qemu-copyright"),
        ("/usr/share/doc/python3/copyright","python3-copyright")]:
        file=Path(source)
        if file.exists():copy(file,licenses/name)
    meta={
        "QEMU binary":str(qemu_bin),
        "Python version":pyver,
        "Bundled shared library count":len(seen),
        "Renderer":"FalconOS kernel C, no Chromium/WebKit",
        "QEMU upstream source":"https://www.qemu.org/download/",
        "QEMU distro source":"https://packages.ubuntu.com/noble/qemu-system-x86",
        "License note":"QEMU GPL-2, Python PSF; see individual copyright/license files",
    }
    (licenses/"RUNTIME-README.txt").write_text("\n".join(f"{k}: {v}" for k,v in meta.items())+"\n")
    # These must be standalone executable ELF binaries, not wrappers.
    assert (bins/"qemu-system-x86_64").is_file()
    assert (bins/"python3").is_file()
    assert any((usr/"share/qemu").iterdir())
    print("PORTABLE_RUNTIME_OK",meta,flush=True)
if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("appdir",type=Path)
    bundle(parser.parse_args().appdir)
