#!/usr/bin/env python3
"""FalconOS .app.pkg integration: real VNC keys and TWO PS/2 clicks -> live Store -> HTTPS GitHub
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
LOG=B/"pkg-doubleclick-guest.log";SER=B/"pkg-doubleclick-serial.sock"
REPORT=B/"pkg-doubleclick-results.json"
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
                # CAT events are streamed one by one; do not search until the
                # real host completed the entire catalog with DONE.
                wait(b"mD",off,110)
                STATE["steps"].append("Real GitHub marketplace catalog fully transferred")
                pause(1.3)
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
                wait(b"fD",0,35)
                STATE["steps"].append("Real GitHub release copied to FalconOS /home/falcon/Downloads/*.app.pkg")
                # Open the genuine FalconOS Files app, not a hosted UI.
                k(c,"f2");t(c,"Files")
                before=len(fresh_data());k(c,"enter")
                wait(b"zBn2",before,65)
                # From All -> Home -> Desktop -> Downloads
                for _ in range(3):k(c,"right")
                pause(1.0)
                shot(c,"FalconOS-Downloads-APP-PKG")
                # RFB pointer maps to a guest PS/2 mouse. Calibrate against
                # actual IRQ coordinate reports, never QMP/HMP injection.
                c.mouseMove(400,320);pause(.25)
                for x,y in ((420,330),(440,345)):
                    c.mouseMove(x,y);pause(.24)
                def pointer():
                    samples=re.findall(rb"p(\d+),(\d+);",fresh_data())
                    return tuple(map(int,samples[-1])) if samples else None
                target=(1110,493)  # Downloads first actual file row
                hx,hy=440,345
                for step in range(150):
                    pos=pointer()
                    if pos and abs(target[0]-pos[0])<=11 and abs(target[1]-pos[1])<=10:
                        break
                    if pos is None:
                        c.mouseMove(hx+15,hy+10);hx+=15;hy+=10
                        pause(.16);continue
                    dx=target[0]-pos[0];dy=target[1]-pos[1]
                    sx=max(-22,min(22,round(dx/(2.5 if abs(dx)>25 else 1.5))))
                    sy=max(-22,min(22,round(dy/(2.5 if abs(dy)>25 else 1.5))))
                    if sx==0 and dx:sx=1 if dx>0 else -1
                    if sy==0 and dy:sy=1 if dy>0 else -1
                    hx=max(6,min(2500,hx+sx))
                    hy=max(6,min(1400,hy+sy))
                    c.mouseMove(hx,hy);pause(.12)
                pos=pointer()
                if pos is None or abs(target[0]-pos[0])>13 or abs(target[1]-pos[1])>13:
                    raise AssertionError(f"Cannot aim PS/2 pointer {pos!r} at Downloads file {target!r}")
                before=len(fresh_data())
                c.mouseDown(1);pause(.35);c.mouseUp(1)
                wait(b"f1",before,40)
                STATE["steps"].append("Real mouse single-click selected the .app.pkg file")
                pause(.25)
                before=len(fresh_data())
                c.mouseDown(1);pause(.35);c.mouseUp(1)
                wait(b"f2",before,40)
                wait(b"fR",before,45)
                STATE["steps"].append("Real second click launched FAPP/1 executable document in native guest Terminal")
                pause(1.3)
                shot(c,"FalconOS-DoubleClick-PKG-App-Running")
                STATE["file_click_method"]="Two actual RFB PS/2 clicks on Files app row"
                if len(STATE["real_assets"])<2:
                    raise AssertionError("No genuine GitHub release bytes + checksum observed")
                if STATE["host_bridge_errors"]:
                    raise AssertionError("Host HTTPS bridge errors: "+repr(STATE["host_bridge_errors"]))
                STATE["result"]="PASS: real GitHub .app.pkg downloaded and double-click executed inside FalconOS"
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
