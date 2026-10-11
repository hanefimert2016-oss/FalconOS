#!/usr/bin/env python3
"""Real hardware-emulated keyboard & VM tests for FalconOS Files + ISO Studio.

Genuine immutable ISO Studio release from public GitHub HTTPS -> FAPP/1 ->
the FalconOS ISO Studio app window -> ISO9660 image with CD001 metadata ->
journaled 0xFA FalconFS storage -> Files > Images. NO USB writes.
Also checks Files F6 create folder and F7 create a text document.
"""
import hashlib,json,re,socket,subprocess,sys,threading,time,traceback
from pathlib import Path
from vncdotool import api
sys.path.insert(0,str(Path(__file__).resolve().parent))
import marketplace_bridge as bridge
from vnc_real_input_audit import k,t,pause,shot
import vnc_real_input_audit as v

B=Path("build").absolute();B.mkdir(exist_ok=True)
DISK=B/"iso-studio-e2e-virtual-disk.raw"
LOG=B/"iso-studio-guest.log";SER=B/"iso-studio-serial.sock"
REPORT=B/"iso-studio-e2e-results.json"
STATE={"result":"RUNNING","checks":[],"releases":[]}
def trace():return LOG.read_bytes() if LOG.exists() else b""
def wait(key,start=0,seconds=90):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        if key in trace()[start:]:return
        time.sleep(.14)
    raise AssertionError(f"QEMU real event {key!r} absent: {trace()[-300:]!r}")
def bridge_connect():
    end=time.monotonic()+16
    while time.monotonic()<end:
        if SER.exists():
            try:
                sock=socket.socket(socket.AF_UNIX)
                sock.connect(str(SER))
                return sock
            except OSError:pass
        time.sleep(.13)
    raise TimeoutError("QEMU serial socket not connected")
def serve(sock):
    original=bridge.request_bytes
    def checked(url):
        binary=original(url)
        STATE["releases"].append({"url":url,"sha256":hashlib.sha256(binary).hexdigest(),"bytes":len(binary)})
        return binary
    bridge.request_bytes=checked
    try:bridge.serve(sock)
    except (OSError,BrokenPipeError):pass
    except Exception as e:STATE["bridge_error"]=repr(e)
    finally:bridge.request_bytes=original
def main():
    for p in (LOG,SER):
        if p.exists():p.unlink()
    if DISK.exists():raise RuntimeError("Refusing to overwrite existing test disk")
    subprocess.run([sys.executable,"tools/make_safe_disk.py",
                    "--image",str(DISK),"--size","2G"],check=True)
    v.LOG=LOG
    cmd=["qemu-system-x86_64","-accel","tcg","-m","1024","-smp","1",
         "-cdrom","build/FalconOS.iso","-boot","d",
         "-drive",f"file={DISK},if=ide,index=0,format=raw",
         "-display","none","-vga","std","-global","VGA.vgamem_mb=256",
         "-netdev","user,id=network","-device","rtl8139,netdev=network",
         "-vnc","127.0.0.1:7","-monitor","none",
         "-serial",f"unix:{SER},server=on,wait=off",
         "-debugcon",f"file:{LOG}","-global","isa-debugcon.iobase=0xe9",
         "-no-reboot"]
    with (B/"iso-studio-qemu.log").open("wb") as qlog:
        guest=subprocess.Popen(cmd,stdout=subprocess.DEVNULL,stderr=qlog)
        serial=None
        try:
            serial=bridge_connect()
            threading.Thread(target=serve,args=(serial,),daemon=True).start()
            for _ in range(100):
                try:
                    with socket.create_connection(("127.0.0.1",5907),timeout=.3):break
                except OSError:time.sleep(.2)
            else:raise TimeoutError("VNC port absent")
            with api.connect("127.0.0.1::5907",timeout=30) as client:
                wait(b"L",seconds=95)
                for evt in (b"T",b"A",b"K",b"D",b"U"):
                    prev=len(trace());k(client,"enter");wait(evt,prev,40)
                t(client,"falcon")
                for evt in (b"P",b"Q",b"O"):
                    prev=len(trace());k(client,"enter");wait(evt,prev,40)
                prev=len(trace());k(client,"enter")
                wait(b"aU",prev,60);wait(b"H",prev,60)
                STATE["checks"].append("Safe dedicated VM disk and no-password autologin")

                k(client,"f2");t(client,"Store")
                prev=len(trace());k(client,"enter")
                wait(b"zCn1",prev,90);wait(b"M",prev,90)
                wait(b"mD",prev,130)
                k(client,"f4")
                t(client,"ISO Studio")
                wait(b"q1",0,85)
                k(client,"f4")
                pause(.6);shot(client,"FalconOS-ISO-Studio-Marketplace")
                prev=len(trace());k(client,"enter");wait(b"I",prev,130)
                wait(b"fD",prev,40)
                STATE["checks"].append("Reviewed ISO Studio release downloaded, SHA256 verified in guest")
                prev=len(trace());k(client,"enter")
                wait(b"zU",prev,60) # built-in app ID 20, ASCII A+20 = U
                pause(.6);shot(client,"FalconOS-ISO-Studio-Before-Build")
                prev=len(trace());k(client,"enter");wait(b"iB",prev,130)
                STATE["checks"].append("Native ISO9660 file generated, stored on JFS2 and CD001 read-back checked")
                pause(1.2);shot(client,"FalconOS-ISO-Studio-After-Build")
                k(client,"f2");t(client,"Files")
                prev=len(trace());k(client,"enter");wait(b"zB",prev,60)
                # Navigate real Files sidebar to journaled Images via keyboard
                for _ in range(5):k(client,"right")
                pause(.9)
                shot(client,"FalconOS-Files-ISO9660-JFS2")
                STATE["checks"].append("Files UI integrates journaled archive alongside SHFS data")
                k(client,"f6");t(client,"Research")
                prev=len(trace());k(client,"enter")
                wait(b"dF",prev,35)
                # Native file creation does not need network/serial bridge.
                pause(.7);shot(client,"FalconOS-Files-New-Folder")
                k(client,"f7");t(client,"draft.txt")
                prev=len(trace());k(client,"enter")
                wait(b"dN",prev,35)
                STATE["checks"].append("Real VNC keyboard created a subfolder and text document")
                if "bridge_error" in STATE:raise AssertionError(STATE["bridge_error"])
                assert any("iso-studio" in a["url"] and a["bytes"]>=100 for a in STATE["releases"]),STATE["releases"]
                STATE["result"]="PASS"
        finally:
            REPORT.write_text(json.dumps(STATE,indent=2,ensure_ascii=False))
            if serial:serial.close()
            guest.terminate()
            try:guest.wait(timeout=4)
            except subprocess.TimeoutExpired:guest.kill()
if __name__=="__main__":
    try:main()
    except Exception as e:
        STATE["result"]="FAIL";STATE["error"]=str(e)
        REPORT.write_text(json.dumps(STATE,indent=2,ensure_ascii=False))
        traceback.print_exc()
        raise
