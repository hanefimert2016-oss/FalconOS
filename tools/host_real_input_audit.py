#!/usr/bin/env python3
"""X11/XTest -> QEMU GTK -> FalconOS PS/2. Monitor only takes screenshots, NOT input."""
import json, re, socket, subprocess, time
from pathlib import Path
from qemu_smoke import ppm_to_png

B=Path('build').absolute();B.mkdir(exist_ok=True)
LOG=B/'host-real-hid-debug.log';MON=B/'host-real-hid-monitor.sock'
REPORT=B/'host-real-hid-report.json'
STATE={'keyboard_steps':[],'mouse_click_events':[],'mouse_close_verified':False,'errors':[]}

def read():return LOG.read_bytes() if LOG.exists() else b''
def wait_mark(m,after=0,t=50):
    deadline=time.monotonic()+t
    while time.monotonic()<deadline:
        if m in read()[after:]:return
        time.sleep(.14)
    raise AssertionError('Guest event '+repr(m)+' missing; tail '+repr(read()[-140:]))
def xdo(*a,pause=.4):
    subprocess.run(['xdotool',*map(str,a)],check=True,stdout=subprocess.DEVNULL)
    time.sleep(pause)
def key(k):xdo('key','--clearmodifiers',k,pause=.46)
def typed(t):xdo('type','--clearmodifiers','--delay','110',t,pause=.45)
def shot(sock,name):
    ppm=B/(name+'.ppm');png=B/(name+'.png')
    if ppm.exists():ppm.unlink()
    sock.sendall(('screendump '+str(ppm)+'\n').encode('ascii'))
    until=time.monotonic()+12
    while not ppm.exists():
        if time.monotonic()>until:raise RuntimeError('Screenshot not created: '+name)
        time.sleep(.1)
    ppm_to_png(ppm,png)
def window():
    end=time.monotonic()+45
    while time.monotonic()<end:
        for q in (['search','--onlyvisible','--name','QEMU'],['search','--onlyvisible','--class','qemu']):
            v=subprocess.run(['xdotool',*q],text=True,capture_output=True)
            if v.stdout.strip():return v.stdout.split()[-1]
        time.sleep(.3)
    raise RuntimeError('GTK QEMU window not found')
def click():
    start=len(read());xdo('click','1',pause=.5);part=read()[start:]
    pts=re.findall(rb'kP(\d+),(\d+);',part)
    if b'mL' not in part or not pts:raise RuntimeError('Host mouse failed to reach PS/2 WM: '+repr(part[-180:]))
    x,y=map(int,pts[-1]);STATE['mouse_click_events'].append([x,y])
    return x,y,part

def main():
    for f in (MON,LOG):
        if f.exists():f.unlink()
    cmd=['qemu-system-x86_64','-accel','tcg','-m','1024','-smp','1',
         '-cdrom','build/FalconOS.iso','-boot','d','-display','gtk,grab-on-hover=on',
         '-vga','std','-global','VGA.vgamem_mb=256','-serial','none',
         '-monitor','unix:'+str(MON)+',server=on,wait=off',
         '-debugcon','file:'+str(LOG),'-global','isa-debugcon.iobase=0xe9','-no-reboot']
    with (B/'qemu-gtk-stderr.log').open('wb') as err:
        proc=subprocess.Popen(cmd,stdout=subprocess.DEVNULL,stderr=err)
        try:
            wid=window()
            xdo('windowfocus','--sync',wid,pause=1)
            xdo('mousemove','--window',wid,600,430,pause=.8)
            wait_mark(b'L',t=75)
            for m in (b'T',b'A',b'K',b'D',b'U'):
                off=len(read());key('Return');wait_mark(m,off);STATE['keyboard_steps'].append(m.decode())
            typed('falcon')
            for m in (b'P',b'Q',b'O'):
                off=len(read());key('Return');wait_mark(m,off);STATE['keyboard_steps'].append(m.decode())
            key('Right')
            off=len(read());key('Return');wait_mark(b'W',off)
            off=len(read());key('Return');wait_mark(b'H',off)
            key('Escape')
            wait_end=time.monotonic()+12
            while not MON.exists():
                if time.monotonic()>wait_end:raise RuntimeError('Screenshot socket absent')
                time.sleep(.1)
            with socket.socket(socket.AF_UNIX) as sock:
                sock.connect(str(MON));sock.settimeout(12)
                shot(sock,'FalconOS-Host-Desktop')
                key('F2');typed('Files')
                shot(sock,'FalconOS-Host-Launchpad-Filtered')
                off=len(read());key('Return');wait_mark(b'zBn1',off)
                shot(sock,'FalconOS-Host-Files')
                STATE['keyboard_steps'].append('Files opened with host XTest keys')
                key('F2')
                for _ in range(5):key('BackSpace')
                typed('Notes')
                off=len(read());key('Return');wait_mark(b'zHn2',off)
                shot(sock,'FalconOS-Host-Notes-Before')
                typed('FalconOS HID keyboard test')
                shot(sock,'FalconOS-Host-Notes-After')
                STATE['keyboard_steps'].append('Notes typed with host XTest keys')
                xdo('mousemove','--window',wid,900,620,pause=.85)
                x,y,_=click()
                shot(sock,'FalconOS-Host-Mouse-First-Click')
                # Notes is second cascading window. Close target is (1860,405).
                for attempt in range(28):
                    dx,dy=1860-x,405-y
                    if abs(dx)<14 and abs(dy)<14:
                        _,_,chunk=click()
                        if b'kX' in chunk:STATE['mouse_close_verified']=True
                        break
                    step_x=max(-45,min(45,round(dx/2.5)))
                    step_y=max(-45,min(45,round(dy/2.5)))
                    if not step_x and not step_y:break
                    xdo('mousemove_relative','--sync','--',step_x,step_y,pause=.24)
                    x,y,chunk=click()
                    if b'kX' in chunk:
                        STATE['mouse_close_verified']=True;break
                shot(sock,'FalconOS-Host-After-Mouse-Close')
                if not STATE['mouse_close_verified']:
                    raise AssertionError('Host mouse reached PS/2, but clicking window Close failed')
        finally:
            REPORT.write_text(json.dumps(STATE,indent=2),encoding='utf8')
            proc.terminate()
            try:proc.wait(timeout=5)
            except subprocess.TimeoutExpired:proc.kill()
if __name__=='__main__':
    try:main()
    except Exception as e:
        STATE['errors'].append(str(e));REPORT.write_text(json.dumps(STATE,indent=2))
        raise
