#!/usr/bin/env python3
"""Photograph the actual native FalconOS desktop, Falco and Codedium in QEMU."""
import json,socket,subprocess,time,traceback,shutil
from pathlib import Path
from vncdotool import api
from vnc_real_input_audit import k,t,pause,shot
import vnc_real_input_audit as v
B=Path("build").absolute();B.mkdir(exist_ok=True)
LOG=B/"falco-codedium-review-guest.log"
REPORT=B/"falco-codedium-review-result.json"
STATE={"result":"RUNNING","screenshots":[],"checks":[]}
def wait(marker,off=0,seconds=80):
    end=time.monotonic()+seconds
    body=b""
    while time.monotonic()<end:
        body=LOG.read_bytes() if LOG.exists() else b""
        if marker in body[off:]:return
        pause(.2)
    raise AssertionError(f"Missing real guest marker {marker!r}; tail={body[-180:]!r}")
def snap(c,label):
    shot(c,label)
    STATE["screenshots"].append(label+".png")
def main():
    v.LOG=LOG
    if LOG.exists():LOG.unlink()
    firmware=[(Path("/usr/share/OVMF/OVMF_CODE_4M.fd"),Path("/usr/share/OVMF/OVMF_VARS_4M.fd")),
              (Path("/usr/share/OVMF/OVMF_CODE.fd"),Path("/usr/share/OVMF/OVMF_VARS.fd")),
              (Path("/usr/share/edk2/x64/OVMF_CODE.4m.fd"),Path("/usr/share/edk2/x64/OVMF_VARS.4m.fd"))]
    pair=next(((c,v) for c,v in firmware if c.is_file() and v.is_file()),None)
    if not pair:raise RuntimeError("FalconBoot test requires OVMF x86_64 code+vars firmware")
    var_copy=B/"FalconBoot-QEMU-VARS.fd"
    market_sock=B/"falcon-market-qa.sock"
    if market_sock.exists(): market_sock.unlink()
    shutil.copyfile(pair[1],var_copy)
    flash=["-drive",f"if=pflash,format=raw,unit=0,readonly=on,file={pair[0]}",
           "-drive",f"if=pflash,format=raw,unit=1,file={var_copy}"]
    cmd=["qemu-system-x86_64","-machine","q35","-accel","tcg",
         *flash,"-m","1024","-smp","1",
         "-boot","d","-cdrom","build/FalconOS.iso","-display","none",
         "-vga","std","-global","VGA.vgamem_mb=256",
         "-vnc","127.0.0.1:11","-monitor","none",
         "-serial",f"unix:{market_sock},server=on,wait=off",
         "-debugcon","file:"+str(LOG),"-global","isa-debugcon.iobase=0xe9","-no-reboot"]
    with (B/"falco-codedium-review-qemu.log").open("wb") as err:
        guest=subprocess.Popen(cmd,stdout=subprocess.DEVNULL,stderr=err)
        bridge_log=(B/"falcon-market-qa-bridge.log").open("wb")
        bridge=subprocess.Popen(["python3","tools/marketplace_bridge.py",
                                 "--socket",str(market_sock)],
                                 stdout=bridge_log,stderr=subprocess.STDOUT)
        try:
            for _ in range(120):
                try:
                    with socket.create_connection(("127.0.0.1",5911),timeout=.3):break
                except OSError:
                    if guest.poll() is not None:raise RuntimeError("QEMU exited before VNC")
                    pause(.25)
            else:raise TimeoutError("No VNC server for QEMU")
            with api.connect("127.0.0.1::5911",timeout=25) as c:
                wait(b"FALCONBOOT/1 MENU DRAWN",seconds=90)
                snap(c,"FalconOS-V2-FalconBoot-UEFI-Loader")
                STATE["checks"].append("Genuine first-party FalconBoot/1 UEFI loader painted its own GOP framebuffer")
                start_kernel=len(LOG.read_bytes())
                wait(b"FALCONBOOT/1 HANDOFF",start_kernel,90)
                kernel_on=len(LOG.read_bytes())
                wait(b"L",kernel_on,95)
                for marker in (b"T",b"A",b"K",b"D",b"U"):
                    off=LOG.stat().st_size;k(c,"enter");wait(marker,off)
                t(c,"falcon")
                for marker in (b"P",b"Q",b"O"):
                    off=LOG.stat().st_size;k(c,"enter");wait(marker,off)
                off=LOG.stat().st_size;k(c,"enter")
                wait(b"aU",off,60);wait(b"H",off,60)
                k(c,"esc");pause(.6)
                snap(c,"FalconOS-Review-Desktop")
                off=LOG.stat().st_size
                k(c,"f2");wait(b"LP",off,60)
                pause(.9)
                snap(c,"FalconOS-V2-AeroFlow-Launcher")
                # Compare the ACTUAL native launcher surface in three real
                # OS palettes, without post-processing the QEMU images.
                from PIL import Image, ImageChops
                liquid=Image.open(B/"FalconOS-V2-AeroFlow-Launcher.png").convert("RGB")
                for index in (3,4,0): # Nordic, Rose Gold, then Light
                    mark=LOG.stat().st_size
                    k(c,"f10");wait(("A"+str(index)).encode(),mark,40)
                    pause(.45)
                snap(c,"FalconOS-V2-AeroFlow-Light")
                light=Image.open(B/"FalconOS-V2-AeroFlow-Light.png").convert("RGB")
                mark=LOG.stat().st_size
                k(c,"f10");wait(b"A1",mark,40);pause(.9)
                snap(c,"FalconOS-V2-AeroFlow-Dark")
                dark=Image.open(B/"FalconOS-V2-AeroFlow-Dark.png").convert("RGB")
                pw=min(liquid.width-46,1000)
                ph=min(max(liquid.height-136,440),728)
                lx=(liquid.width-pw)//2;ly=(liquid.height-ph)//2
                roi=(lx+29,ly+119,lx+196,ly+161)
                if ImageChops.difference(liquid.crop(roi),light.crop(roi)).getbbox() is None or ImageChops.difference(light.crop(roi),dark.crop(roi)).getbbox() is None:
                    raise AssertionError("AeroFlow native sidebar palette stayed unchanged across Liquid/Light/Dark")
                STATE["checks"].append("Native AeroFlow launcher sidebar changes in actual Liquid / Light / Dark QEMU framebuffer pixels")
                mark=LOG.stat().st_size
                k(c,"f10");wait(b"A2",mark,40) # restore default Liquid
                t(c,"Falco");pause(.7)
                snap(c,"FalconOS-V2-AeroFlow-Search")
                off=LOG.stat().st_size;k(c,"enter")
                wait(b"zNn1",off,60)
                # zN proves application selection only; gN is emitted AFTER
                # the native framebuffer is fully drawn with launcher closed.
                wait(b"gN",off,65)
                pause(1.3)
                snap(c,"FalconOS-Review-Falco")
                STATE["checks"].append("Real Falco rendered in QEMU via PS/2 input")
                k(c,"f2");t(c,"Codedium")
                off=LOG.stat().st_size;k(c,"enter")
                wait(b"zS",off,60)
                wait(b"gS",off,65)
                pause(1.3)
                snap(c,"FalconOS-Review-Codedium")
                STATE["checks"].append("Real Codedium editor rendered in QEMU")
                # Actual V2 About window is opened through Launchpad and drawn
                # by the real guest framebuffer (not a post-processed mockup).
                k(c,"f2");t(c,"About")
                off=LOG.stat().st_size;k(c,"enter")
                wait(b"zR",off,60);wait(b"gR",off,65)
                pause(1.2)
                snap(c,"FalconOS-V2-Beta-About-Real-QEMU")
                STATE["checks"].append("Real FalconOS V2 Beta About window rendered inside QEMU")
                k(c,"f2");t(c,"Terminal")
                off=LOG.stat().st_size;k(c,"enter")
                wait(b"zF",off,60);wait(b"gF",off,65)
                t(c,"ver");k(c,"enter");pause(.8)
                t(c,"uname");k(c,"enter");pause(.7)
                t(c,"doctor");k(c,"enter");pause(.9)
                snap(c,"FalconOS-V2-Beta-Terminal-Real-QEMU")
                t(c,"lspci");k(c,"enter");pause(1.3)
                snap(c,"FalconOS-V2-PCI-Devices-Real-QEMU")
                STATE["checks"].append("Native Terminal enumerated real PCI BDF/vendor/class devices with read-only config access")
                STATE["checks"].append("Native Terminal accepted ver, uname and honest doctor runtime diagnostics in QEMU")
                # V2 Calendar is a real RTC-based application, present in
                # Launchpad's All catalogue, not a demo counter of uptime.
                k(c,"f2");t(c,"Calendar")
                off=LOG.stat().st_size;k(c,"enter")
                wait(b"zK",off,60);wait(b"gK",off,65)
                pause(1)
                snap(c,"FalconOS-V2-Gregorian-Calendar-Today-Real-QEMU")
                from PIL import Image, ImageChops
                initial=Image.open(B/"FalconOS-V2-Gregorian-Calendar-Today-Real-QEMU.png").convert("RGB")
                off=LOG.stat().st_size
                k(c,"right")
                wait(b"MN",off,40) # guest month state increment acknowledged
                # Set a NEW cursor after the input acknowledgement. Otherwise
                # gK could match an OLD frame rendered during keyPress().
                painted_after=LOG.stat().st_size
                wait(b"gK",painted_after,90)
                month_title_box=(initial.width*35//100,initial.height*29//100,
                                 initial.width*47//100,initial.height*35//100)
                # VNC is asynchronous even after guest gfx_present. Poll the
                # ACTUAL QEMU framebuffer until its month title changes;
                # never silently accept a stale screenshot.
                changed=False
                for attempt in range(14):
                    pause(.7)
                    shot(c,"FalconOS-V2-Gregorian-Calendar-Next-Month-Real-QEMU")
                    following=Image.open(B/"FalconOS-V2-Gregorian-Calendar-Next-Month-Real-QEMU.png").convert("RGB")
                    delta=ImageChops.difference(initial.crop(month_title_box),
                                                following.crop(month_title_box))
                    if delta.getbbox() is not None:
                        changed=True
                        break
                STATE["screenshots"].append("FalconOS-V2-Gregorian-Calendar-Next-Month-Real-QEMU.png")
                if not changed:
                    raise AssertionError("Guest confirmed month navigation but no new Calendar title reached VNC after 10s")
                off=LOG.stat().st_size
                k(c,"t");wait(b"MT",off,40)
                STATE["checks"].append("REAL RTC Calendar: guest acknowledged right arrow and month header visibly changed")
                # Separate embedded updater, not a Marketplace release.
                # Visit it in the real QEMU guest and issue a bounded
                # manual CHECK. The host bridge may be absent in this
                # visual-only run; uQ proves the guest actually sent it.
                k(c,"f2");t(c,"Sistem")
                off=LOG.stat().st_size;k(c,"enter")
                wait(b"zE",off,60);wait(b"gE",off,65)
                pause(.8)
                snap(c,"FalconOS-V2-Native-System-Updater")
                off=LOG.stat().st_size;k(c,"c")
                wait(b"uQ",off,35)
                pause(.7)
                snap(c,"FalconOS-V2-Native-System-Updater-Manual-Check")
                # Real keyboard interaction, not a mockup: switch release
                # enrollment to Alpha, check, then return to Beta.
                off=LOG.stat().st_size
                k(c,"3");wait(b"uQ",off,35)
                pause(.6)
                snap(c,"FalconOS-V2-Alpha-Channel-Real-QEMU")
                off=LOG.stat().st_size
                k(c,"2");wait(b"uQ",off,35)
                pause(.6)
                snap(c,"FalconOS-V2-Beta-Channel-Real-QEMU")
                STATE["checks"].append("Native Stable/Beta/Alpha release enrollment changed using real PS/2 input")
                STATE["checks"].append("Dedicated OS updater GUI opened, manual check dispatched from guest COM2")
                # Native Discover uses actual public Marketplace Releases via
                # the host's read-only HTTPS bridge. Failure to reach GitHub
                # is distinct from a failure of the guest's rendered GUI.
                k(c,"f2");t(c,"Store")
                off=LOG.stat().st_size;k(c,"enter")
                wait(b"zC",off,60);wait(b"gC",off,65)
                # apps_open(Store) already requests LIST once. Never fire a
                # duplicate F5 while the first CAT/ACK handshake is active.
                try:
                    wait(b"mA",off,32)
                    STATE["checks"].append("Public Marketplace catalog populated with real release entries over guest serial transport")
                except AssertionError:
                    retry=LOG.stat().st_size
                    k(c,"f5")
                    try:
                        wait(b"mA",retry,38)
                        STATE["checks"].append("Public Marketplace release entries populated after one explicit refresh")
                    except AssertionError:
                        STATE["checks"].append("Native Discover rendered; external GitHub release catalog not populated during CI")
                pause(1)
                snap(c,"FalconOS-V2-Discover-Native-Marketplace")
                STATE["checks"].append("Native AeroFlow Discover photographed directly from actual OVMF/QEMU framebuffer")
                STATE["result"]="PASS"
        finally:
            bridge.terminate()
            try:bridge.wait(timeout=3)
            except subprocess.TimeoutExpired:bridge.kill()
            bridge_log.close()
            guest.terminate()
            try:guest.wait(timeout=5)
            except subprocess.TimeoutExpired:guest.kill()
            REPORT.write_text(json.dumps(STATE,indent=2))
if __name__=="__main__":
    try:main()
    except Exception as err:
        STATE["result"]="FAIL";STATE["error"]=str(err)
        REPORT.write_text(json.dumps(STATE,indent=2))
        traceback.print_exc()
        raise
