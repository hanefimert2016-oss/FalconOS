#!/usr/bin/env python3
"""Real FalconOS Falco browser opens HTTPS falconos.tech in its own native GUI.
The host performs CA-verified HTTPS, sends actual semantic HTML over QEMU's
private NAT link, and the guest paints the actual site into its framebuffer.
Never substitute host Chrome screenshots, fake HTML, or GitHub README.
"""
import json,socket,subprocess,sys,time,traceback,urllib.request
from pathlib import Path
from vncdotool import api
from vnc_real_input_audit import k,t,pause,shot
import vnc_real_input_audit as v

B=Path("build").absolute();B.mkdir(parents=True,exist_ok=True)
LOG=B/"falco-tech-guest.log"
REPORT=B/"falco-tech-results.json"
STATE={"result":"RUNNING","site":"https://falconos.tech/",
       "render_mode":"FalconOS framebuffer site-specific native renderer",
       "https_mode":"verified on QEMU host, plaintext private guest-host link",
       "checks":[],"screenshots":[]}
def events():
    return LOG.read_bytes() if LOG.exists() else b""
def wait(token,offset=0,seconds=95):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        if token in events()[offset:]:return
        time.sleep(.2)
    raise AssertionError(f"Missing QEMU guest marker {token!r}, tail={events()[-250:]!r}")
def snap(client,label):
    shot(client,label)
    STATE["screenshots"].append(label+".png")
def main():
    v.LOG=LOG
    if LOG.exists():LOG.unlink()
    gateway_log=(B/"falco-tech-gateway.log").open("wb")
    gateway=subprocess.Popen([sys.executable,"-u","tools/falco_native_gateway.py",
          "--bind","127.0.0.1","--port","18446"],stdout=gateway_log,stderr=subprocess.STDOUT)
    guest=None
    try:
        for i in range(80):
            if gateway.poll() is not None:raise RuntimeError("Falco host TLS companion crashed")
            try:
                with socket.create_connection(("127.0.0.1",18446),timeout=.2):break
            except OSError:pause(.2)
        else:raise TimeoutError("Native HTML gateway not listening")
        # Mandatory end-to-end proof: the real upstream site was fetched
        # immediately through verified HTTPS, not loaded from a test fixture.
        with urllib.request.urlopen("http://127.0.0.1:18446/falcon/native/",timeout=35) as response:
            document=response.read()
            if (response.status!=200 or response.headers.get("X-Falco-Native-Verified")!="yes"
                or response.headers.get("X-Falco-Source")!="falconos.tech"
                or b"FALCO/1\nSOURCE|falconos.tech\n" not in document
                or b"Contour" not in document or b"FalconOS" not in document):
                raise AssertionError("Real live falconos.tech HTML not verified by gateway")
            STATE["verified_guest_document_bytes"]=len(document)
            STATE["checks"].append("Real site TLS CA + hostname checked on host, verified semantic document")
        cmd=["qemu-system-x86_64","-accel","tcg","-m","1024","-smp","1",
           "-boot","d","-cdrom","build/FalconOS.iso","-display","none",
           "-vga","std","-global","VGA.vgamem_mb=256",
           "-netdev","user,id=network","-device","rtl8139,netdev=network",
           "-vnc","127.0.0.1:12","-monitor","none","-serial","none",
           "-debugcon",f"file:{LOG}","-global","isa-debugcon.iobase=0xe9","-no-reboot"]
        with (B/"falco-tech-qemu.log").open("wb") as err:
            guest=subprocess.Popen(cmd,stderr=err,stdout=subprocess.DEVNULL)
            for i in range(120):
                try:
                    with socket.create_connection(("127.0.0.1",5912),timeout=.3):break
                except OSError:
                    if guest.poll() is not None:raise RuntimeError("QEMU stopped before VNC")
                    pause(.25)
            else:raise TimeoutError("VNC server missing")
            with api.connect("127.0.0.1::5912",timeout=25) as client:
                wait(b"L",seconds=90)
                for marker in (b"T",b"A",b"K",b"D",b"U"):
                    offset=len(events());k(client,"enter");wait(marker,offset)
                t(client,"falcon")
                for marker in (b"P",b"Q",b"O"):
                    offset=len(events());k(client,"enter");wait(marker,offset)
                offset=len(events());k(client,"enter")
                wait(b"aU",offset,60);wait(b"H",offset,60)
                k(client,"esc")
                k(client,"f2");t(client,"Falco")
                offset=len(events());k(client,"enter")
                wait(b"zNn1",offset,65);wait(b"gN",offset,65)
                pause(.8);snap(client,"FalconOS-Falco-Tech-Before")
                STATE["checks"].append("Real FalconOS Falco app painted")
                # Test the USER FLOW, not an F9-only showcase:
                # type the public URL in the actual Falco omnibox and Enter.
                k(client,"f4")
                t(client,"falconos.tech")
                offset=len(events());k(client,"enter")
                STATE["checks"].append("Typed falconos.tech into native Falco omnibox and pressed Enter")
                try:
                    wait(b"vY",offset,95)
                    wait(b"nY",offset,20)
                    wait(b"gN",offset,30)
                except Exception:
                    pause(1.0)
                    snap(client,"FalconOS-Falco-Tech-Failed")
                    raise
                pause(1.6)
                snap(client,"FalconOS-Falco-Tech-Real-Home")
                STATE["checks"].append("Falco native framebuffer painted live verified falconos.tech")
                for key,marker,label in [
                    ("2",b"s1","FalconOS-Falco-Tech-Features"),
                    ("3",b"s2","FalconOS-Falco-Tech-Architecture")]:
                    offset=len(events());k(client,key);wait(marker,offset,25)
                    pause(1.1)
                    snap(client,label)
                    STATE["checks"].append("Real native scroll/page navigation: "+label)
                gateway_log.flush()
                actual=(B/"falco-tech-gateway.log").read_text(errors="replace")
                if "/falcon/native/" not in actual or " 200 " not in actual:
                    raise AssertionError("Gateway lacks verified HTTP 200 access record")
                STATE["result"]="PASS"
    finally:
        if guest:
            guest.terminate()
            try:guest.wait(timeout=5)
            except subprocess.TimeoutExpired:guest.kill()
        gateway.terminate()
        try:gateway.wait(timeout=4)
        except subprocess.TimeoutExpired:gateway.kill()
        gateway_log.close()
        REPORT.write_text(json.dumps(STATE,ensure_ascii=False,indent=2))
if __name__=="__main__":
    try:main()
    except Exception as exc:
        STATE["result"]="FAIL";STATE["error"]=str(exc)
        REPORT.write_text(json.dumps(STATE,ensure_ascii=False,indent=2))
        traceback.print_exc()
        raise
