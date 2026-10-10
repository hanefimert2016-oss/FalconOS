#!/usr/bin/env python3
"""FalconOS native Marketplace: real VNC keys -> guest Store -> host HTTPS GitHub
Release -> SHA-256 checked in guest -> launch FAPP/1. No mocked catalog/assets.
No QMP/HMP keyboard commands. Publishing intentionally disabled.
"""
import hashlib,json,re,socket,subprocess,sys,threading,time,traceback
from pathlib import Path
from vncdotool import api
sys.path.insert(0,str(Path(__file__).resolve().parent))
import marketplace_bridge as bridge
from vnc_real_input_audit import k,t,pause,mark,shot
import vnc_real_input_audit as v
B=Path("build").absolute();B.mkdir(exist_ok=True)
LOG=B/"market-vnc-kernel.log";SER=B/"market-vnc-serial.sock"
REPORT=B/"market-vnc-results.json"
STATE={"result":"RUNNING","real_assets":[],"steps":[],"host_bridge_errors":[]}

def fresh_data():return LOG.read_bytes() if LOG.exists() else b''
def wait(token,from_byte=0,timeout=90):
    until=time.monotonic()+timeout
    while time.monotonic()<until:
        if token in fresh_data()[from_byte:]:return True
        time.sleep(.2)
    raise AssertionError(f"guest marker {token!r} missing: {fresh_data()[-200:]!r}")

def socket_connect(path):
    deadline=time.monotonic()+16
    while time.monotonic()<deadline:
        if path.exists():
            try:
                s=socket.socket(socket.AF_UNIX)
                s.connect(str(path))
                return s
            except (OSError,ConnectionError):pass
        time.sleep(.1)
    raise TimeoutError("QEMU COM1 unix socket missing")

def serve_real(sock):
    original=bridge.request_bytes
    def inspect(url):
        data=original(url)  # Genuine GitHub HTTPS; never a fixture
        STATE["real_assets"].append({
            "url":url,"sha256":hashlib.sha256(data).hexdigest(),"bytes":len(data)})
        return data
    bridge.request_bytes=inspect
    try:bridge.serve(sock)
    except (BrokenPipeError,OSError):pass
    except Exception as e:STATE["host_bridge_errors"].append(str(e))
    finally:bridge.request_bytes=original

def main():
    for p in (LOG,SER):
        if p.exists():p.unlink()
    v.LOG=LOG
    cmd=["qemu-system-x86_64","-accel","tcg","-m","1024","-smp","1",
         "-cdrom","build/FalconOS.iso","-boot","d",
         "-display","none","-vga","std","-global","VGA.vgamem_mb=256",
         "-vnc","127.0.0.1:8","-monitor","none",
         "-serial",f"unix:{SER},server=on,wait=off",
         "-debugcon",f"file:{LOG}","-global","isa-debugcon.iobase=0xe9",
         "-no-reboot"]
    with (B/"market-vnc-qemu-stderr.log").open("wb") as fd:
        proc=subprocess.Popen(cmd,stderr=fd,stdout=subprocess.DEVNULL)
        serial=None
        try:
            serial=socket_connect(SER)
            thread=threading.Thread(target=serve_real,args=(serial,),daemon=True)
            thread.start()
            for _ in range(120):
                try:
                    with socket.create_connection(("127.0.0.1",5908),timeout=.3):
                        break
                except OSError:pause(.2)
            else:raise TimeoutError("RFB 5908 not listening")
            with api.connect("127.0.0.1::5908",timeout=25) as c:
                wait(b"L",timeout=85)
                for x in (b"T",b"A",b"K",b"D",b"U"):
                    off=len(fresh_data());k(c,"enter");wait(x,off,30)
                t(c,"falcon")
                for x in (b"P",b"Q",b"O"):
                    off=len(fresh_data());k(c,"enter");wait(x,off,30)
                k(c,"right")
                off=len(fresh_data());k(c,"enter");wait(b"W",off,40)
                off=len(fresh_data());k(c,"enter");wait(b"H",off,40)
                k(c,"esc");pause(.8)
                k(c,"f2")
                t(c,"Store")  # bilingual launcher title alias
                off=len(fresh_data());k(c,"enter")
                wait(b"zCn1",off,70)
                wait(b"M",off,70)
                STATE["steps"].append("Store native window opened")
                shot(c,"FalconOS-Market-Store")
                # Genuine GitHub Releases (or authenticated reviewed static
                # catalog only when unauthenticated API is rate-limited).
                wait(b"C",off,80)
                STATE["steps"].append("GitHub marketplace catalog accepted in guest")
                pause(1.5)
                k(c,"f4")
                before=len(fresh_data())
                t(c,"Hello")
                wait(b"q1",before,90)
                STATE["steps"].append("Hello World filtered to exactly one package")
                pause(1.3)
                shot(c,"FalconOS-Market-Real-Search")
                k(c,"f4")
                before=len(fresh_data())
                k(c,"enter")
                wait(b"I",before,100)
                STATE["steps"].append("Real package downloaded and SHA256 checked by guest")
                pause(1.4)
                shot(c,"FalconOS-Market-Installed")
                before=len(fresh_data())
                k(c,"enter")
                wait(b"R",before,75)
                STATE["steps"].append("Downloaded FAPP/1 launched in native guest Terminal")
                pause(1.6)
                shot(c,"FalconOS-Market-App-Running")
                if len(STATE["real_assets"])<2:
                    raise AssertionError("No genuine GitHub release bytes + checksum observed")
                if STATE["host_bridge_errors"]:
                    raise AssertionError("Host HTTPS bridge errors: "+repr(STATE["host_bridge_errors"]))
                STATE["result"]="PASS: real GitHub release to guest and Terminal"
        finally:
            REPORT.write_text(json.dumps(STATE,ensure_ascii=False,indent=2))
            if serial:
                serial.close()
            proc.terminate()
            try:proc.wait(timeout=4)
            except subprocess.TimeoutExpired:proc.kill()
if __name__=="__main__":
    try:main()
    except Exception as e:
        STATE["result"]="FAIL"
        STATE["error"]=str(e)
        REPORT.write_text(json.dumps(STATE,ensure_ascii=False,indent=2))
        traceback.print_exc()
        raise
