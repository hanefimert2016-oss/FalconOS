#!/usr/bin/env python3
"""Falcon Capture Studio / first-party QEMU OBS-compatible companion.

No OBS binary and no host webpage-rendered replacement. FalconOS initiates
START/STOP through its own native UI (FCAP/1 on debugcon). This companion
encodes genuine QEMU framebuffer VNC pixels in FFmpeg H.264 MP4 and saves
native guest screenshots. Audio support is deliberately not claimed.
"""
import hashlib,json,queue,subprocess,socket,sys,threading,time,traceback,urllib.request
from pathlib import Path
from PIL import Image,ImageChops
from vncdotool import api
from vnc_real_input_audit import k,t,pause
import vnc_real_input_audit as v

OUT=Path("build/falcon-capture-studio").absolute()
OUT.mkdir(parents=True,exist_ok=True)
LOG=OUT/"capture-kernel.log"
REPORT=OUT/"capture-results.json"
MP4=OUT/"Falcon-Capture-Studio-Real-Falco-QEMU.mp4"
QEMU_VNC="127.0.0.1::5913"
RES={"result":"RUNNING","recorder":"Falcon Capture Studio v1, genuine guest framebuffer via QEMU RFB",
     "resolution":"1280x720","fps":4,"shots":[],
     "workflow":[],"codec":"H.264 via ffmpeg (no audio)"}
def events():return LOG.read_bytes() if LOG.exists() else b""
def mark(token,after=0,timeout=65):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        if token in events()[after:]:return
        time.sleep(.2)
    raise AssertionError(f"Missing real guest event {token!r} after {after}, tail={events()[-240:]!r}")
def wait_tcp(port):
    until=time.monotonic()+15
    while time.monotonic()<until:
        try:
            with socket.create_connection(("127.0.0.1",port),timeout=.4):return
        except OSError:time.sleep(.2)
    raise TimeoutError(f"QEMU auxiliary TCP port {port} not listening")
def capture_movie(ready,stop,shots,errors):
    """Second RFB observer; it NEVER sends keyboard/mouse control events."""
    images=[]
    hashes=set()
    snap_index=0
    temp=OUT/"capture-current.png"
    try:
        ffmpeg=subprocess.Popen([
          "ffmpeg","-y","-hide_banner","-loglevel","error",
          "-f","rawvideo","-pixel_format","rgb24","-video_size","1280x720",
          "-framerate","4","-i","pipe:0","-an",
          "-c:v","libx264","-preset","ultrafast","-crf","19",
          "-pix_fmt","yuv420p",str(MP4)],
          stdin=subprocess.PIPE,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        try:
            with api.connect(QEMU_VNC,timeout=22) as recorder:
                ready.set()
                while not stop.is_set() and len(images)<130:
                    started=time.monotonic()
                    recorder.captureScreen(str(temp))
                    with Image.open(temp) as f:
                        image=f.convert("RGB").resize((1280,720),Image.Resampling.LANCZOS)
                    current_hash=hashlib.sha256(image.tobytes()).hexdigest()
                    hashes.add(current_hash)
                    images.append(image)
                    ffmpeg.stdin.write(image.tobytes())
                    try:
                        while True:
                            name=shots.get_nowait()
                            filename=OUT/name
                            image.save(filename,format="PNG")
                            RES["shots"].append(filename.name)
                    except queue.Empty:pass
                    wait=0.25-(time.monotonic()-started)
                    if wait>0:time.sleep(wait)
                # Final screenshot request always gets the last real RFB frame.
                while True:
                    try:name=shots.get_nowait()
                    except queue.Empty:break
                    image=images[-1] if images else None
                    if image:
                        image.save(OUT/name,format="PNG")
                        RES["shots"].append(name)
        finally:
            if ffmpeg.stdin:
                ffmpeg.stdin.close()
            try:
                ffmpeg.wait(timeout=45)
            except subprocess.TimeoutExpired:
                ffmpeg.kill();ffmpeg.wait()
        RES["frames_encoded"]=len(images)
        RES["distinct_frames"]=len(hashes)
        RES["recording_seconds"]=round(len(images)/4,2)
        if ffmpeg.returncode!=0 or len(images)<8 or len(hashes)<3:
            raise AssertionError(
                f"Capture invalid: encoder status={ffmpeg.returncode}, "
                f"frames={len(images)}, unique={len(hashes)}, stderr="
                f"{ffmpeg.stderr.read(1500)!r}")
    except Exception as exc:
        errors.append(str(exc))
        ready.set()
    finally:
        if temp.exists():temp.unlink()

def main():
    v.LOG=LOG
    for f in [LOG,REPORT,MP4]:
        if f.exists():f.unlink()
    gf=(OUT/"https-gateway.log").open("wb")
    gateway=subprocess.Popen([sys.executable,"-u","tools/falco_native_gateway.py",
       "--bind","127.0.0.1","--port","18446"],stdout=gf,stderr=subprocess.STDOUT)
    guest=None
    try:
        wait_tcp(18446)
        with urllib.request.urlopen("http://127.0.0.1:18446/falcon/native/",timeout=35) as resp:
            sample=resp.read()
            assert resp.status==200 and resp.headers["X-Falco-Native-Verified"]=="yes"
            assert b"Contour" in sample and b"FalconOS" in sample
        RES["workflow"].append("Live real falconos.tech CA-verified HTTPS preflight")
        cmd=["qemu-system-x86_64","-accel","tcg","-cpu","max","-m","1024","-smp","1",
             "-cdrom","build/FalconOS.iso","-boot","d","-display","none","-vga","std",
             "-global","VGA.vgamem_mb=256",
             "-netdev","user,id=net","-device","rtl8139,netdev=net",
             "-vnc","127.0.0.1:13,share=force-shared","-monitor","none","-serial","none",
             "-debugcon",f"file:{LOG}","-global","isa-debugcon.iobase=0xe9","-no-reboot"]
        with (OUT/"qemu-stderr.log").open("wb") as qerr:
            guest=subprocess.Popen(cmd,stderr=qerr,stdout=subprocess.DEVNULL)
            wait_tcp(5913)
            with api.connect(QEMU_VNC,timeout=25) as driver:
                mark(b"L",timeout=95)
                for tag in (b"T",b"A",b"K",b"D",b"U"):
                    off=len(events());k(driver,"enter");mark(tag,off)
                t(driver,"falcon")
                for tag in (b"P",b"Q",b"O"):
                    off=len(events());k(driver,"enter");mark(tag,off)
                off=len(events());k(driver,"enter")
                mark(b"aU",off,75);mark(b"H",off,75)
                k(driver,"esc")
                k(driver,"f2")
                t(driver,"Capture")
                off=len(events());k(driver,"enter")
                mark(b"zUn1",off);mark(b"CR",off)
                RES["workflow"].append("Opened FIRST-PARTY Capture Studio app ID 20 in the real guest")
                pause(1.5)
                off=len(events());k(driver,"space")
                mark(b"FCAP/1 START fps=4",off,25)
                record_start_offset=off
                RES["workflow"].append("Started recording using real Capture Studio Space control")
                ready=threading.Event();stop=threading.Event()
                shots=queue.Queue();errors=[]
                movie=threading.Thread(target=capture_movie,
                      args=(ready,stop,shots,errors),daemon=True)
                movie.start()
                if not ready.wait(timeout=25):raise TimeoutError("Own MP4 recorder never attached to VNC")
                if errors:raise RuntimeError(errors[0])
                # Actual app stays recording as desktop and browser change.
                shots.put("Falcon-Capture-Studio-Control-Real-QEMU.png")
                k(driver,"f2");t(driver,"Falco")
                off=len(events());k(driver,"enter")
                mark(b"zNn2",off);mark(b"gN",off)
                k(driver,"f4")
                t(driver,"falconos");k(driver,"/");t(driver,"tech")
                off=len(events());k(driver,"enter")
                mark(b"vY",off,90);mark(b"nR",off,40)
                pause(2.0)
                shots.put("Falcon-Capture-Falco-Actual-Site.png")
                RES["workflow"].append("Recorded real falconos.tech rendered in Falco after typed URL")
                off=len(events());k(driver,"2");mark(b"s1",off,25)
                pause(2);shots.put("Falcon-Capture-Falco-Features.png")
                off=len(events());k(driver,"3");mark(b"s2",off,25)
                pause(2);shots.put("Falcon-Capture-Falco-Architecture.png")
                # Return to native app and stop through its own control.
                k(driver,"f2");t(driver,"Capture")
                off=len(events());k(driver,"enter")
                mark(b"CR",off,40)
                # The fixed 96-frame RAM buffer stops safely on capacity.
                # Do not toggle Space again if the guest already stopped it.
                if b"FCAP/1 STOP" in events()[record_start_offset:]:
                    RES["workflow"].append("Guest RAM frame capacity stopped recording safely")
                else:
                    off=len(events());k(driver,"space")
                    mark(b"FCAP/1 STOP",off,25)
                    RES["workflow"].append("Stopped the actual guest recorder using Space")
                pause(1.2)
                shots.put("Falcon-Capture-Studio-Finished.png")
                stop.set();movie.join(timeout=55)
                if movie.is_alive():raise TimeoutError("Capture MP4 encoder did not complete")
                if errors:raise AssertionError("Capture worker failed: "+str(errors))
                for mandatory in ["Falcon-Capture-Falco-Actual-Site.png",
                                  "Falcon-Capture-Falco-Features.png"]:
                    if not (OUT/mandatory).exists():
                        raise AssertionError("Missing genuine Capture Studio scene screenshot "+mandatory)
                if not MP4.exists() or MP4.stat().st_size<12000:
                    raise AssertionError("No genuine playable H.264 video")
                probe=subprocess.run(["ffprobe","-v","error",
                     "-select_streams","v:0","-show_entries","stream=codec_name,width,height,nb_frames",
                     "-of","json",str(MP4)],text=True,capture_output=True,check=True)
                meta=json.loads(probe.stdout)["streams"][0]
                if meta["codec_name"]!="h264" or meta["width"]!=1280 or meta["height"]!=720:
                    raise AssertionError("MP4 video validation failed "+str(meta))
                RES["codec_verified"]=meta
                RES["result"]="PASS"
    finally:
        if guest:
            guest.terminate()
            try:guest.wait(timeout=6)
            except subprocess.TimeoutExpired:guest.kill()
        gateway.terminate()
        try:gateway.wait(timeout=4)
        except subprocess.TimeoutExpired:gateway.kill()
        gf.close()
        REPORT.write_text(json.dumps(RES,ensure_ascii=False,indent=2))
if __name__=="__main__":
    try:main()
    except Exception as e:
        RES["result"]="FAIL";RES["error"]=str(e)
        REPORT.write_text(json.dumps(RES,ensure_ascii=False,indent=2))
        traceback.print_exc()
        raise
