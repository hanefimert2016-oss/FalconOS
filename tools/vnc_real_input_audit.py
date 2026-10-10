#!/usr/bin/env python3
"""Interact with actual guest display through RFB/VNC mouse & keys, never QMP/HMP injection."""
from pathlib import Path
import json
import re
import subprocess
import time
import socket
from vncdotool import api

B=Path('build').absolute()
B.mkdir(exist_ok=True)
LOG=B/'vnc-hid-guest.log'
REPORT=B/'vnc-hid-results.json'
RES={'keyboard':[],'mouse':{},'result':'RUNNING','errors':[]}

def raw():return LOG.read_bytes() if LOG.exists() else b''
def mark(token,after=0,deadline=45):
    end=time.monotonic()+deadline
    while time.monotonic()<end:
        if token in raw()[after:]:return
        time.sleep(.12)
    raise AssertionError('Native kernel marker missing '+repr(token)+' tail='+repr(raw()[-190:]))
def latest_pos():
    matches=re.findall(rb'p(\d+),(\d+);',raw())
    return tuple(map(int,matches[-1])) if matches else None
def pause(s=.35):time.sleep(s)
def k(c,key):
    c.keyPress(key)
    pause(.28)
def t(c,s):
    for ch in s:
        c.keyPress('space' if ch==' ' else ch)
        pause(.18)
def click(c):
    at=len(raw())
    c.mouseDown(1)
    pause(.30)
    c.mouseUp(1)
    pause(.55)
    chunk=raw()[at:]
    return chunk
def shot(c,name):
    c.captureScreen(str(B/(name+'.png')))
def notes(c):
    k(c,'f2')
    t(c,'Dosyalar')
    off=len(raw())
    k(c,'enter')
    mark(b'zBn1',off)
    mark(b'gB',off)
    shot(c,'FalconOS-VNC-Files')
    RES['keyboard'].append('Files: launch')
    k(c,'f2')
    for _ in range(8):k(c,'backspace')
    t(c,'Notlar')
    off=len(raw())
    k(c,'enter')
    mark(b'zHn2',off)
    mark(b'gH',off)
    pause(1)
    shot(c,'FalconOS-VNC-Notes-Before')
    off=len(raw())
    t(c,'falconos vnc test')
    end=time.monotonic()+45
    while raw()[off:].count(b't')<len('falconos vnc test') and time.monotonic()<end:
        pause(.15)
    n=raw()[off:].count(b't')
    RES['notes_char_count']=n
    if n<len('falconos vnc test'):
        raise AssertionError('Only '+str(n)+' note characters processed')
    pause(1.2)
    shot(c,'FalconOS-VNC-Notes-After')
    RES['keyboard'].append('Notes: 17 characters accepted by editor')
def mouse(c):
    # RFB PointerEvent is routed through the ordinary display backend to guest PS/2.
    # The monitor is not connected at all; mouse_move/ input-send-event cannot be used.
    vx,vy=300,300
    old=len(raw())
    c.mouseMove(vx,vy)
    pause(.5)
    mark(b'p',old,deadline=12)
    goal=(1989,315)  # 2560x1440, 2nd 1180x760 window Close centre
    RES['mouse']['initial_guest_pos']=latest_pos()
    for n in range(90):
        pos=latest_pos()
        if not pos:raise AssertionError('Guest PS/2 pointer did not move')
        delta_x=goal[0]-pos[0]
        delta_y=goal[1]-pos[1]
        if abs(delta_x)<=12 and abs(delta_y)<=12:
            break
        # Guest PS/2 scales larger host pointer movement 2.5x,
        # and small movements ~1.5x; converge without packet overflow.
        sx=max(-24,min(24,round(delta_x/(2.5 if abs(delta_x)>25 else 1.5))))
        sy=max(-24,min(24,round(delta_y/(2.5 if abs(delta_y)>25 else 1.5))))
        if sx==0 and delta_x:sx=1 if delta_x>0 else -1
        if sy==0 and delta_y:sy=1 if delta_y>0 else -1
        vx=max(6,min(2550,vx+sx))
        vy=max(6,min(1425,vy+sy))
        before=len(raw())
        c.mouseMove(vx,vy)
        pause(.13)
        if b'p' not in raw()[before:]:
            pause(.25)
    RES['mouse']['final_guest_pos']=latest_pos()
    RES['mouse']['goal']=goal
    shot(c,'FalconOS-VNC-Pointer-Before-Close')
    if any(abs(g-v)>14 for g,v in zip(goal,latest_pos())):
        raise AssertionError('Cannot aim guest pointer at close hitbox: '+str(latest_pos()))
    got=click(c)
    RES['mouse']['pressed_in_ps2']=b'mL' in got
    RES['mouse']['window_close_event']=b'kX' in got
    shot(c,'FalconOS-VNC-Window-Closed')
    if b'mL' not in got: raise AssertionError('RFB button did not reach PS/2 driver')
    if b'kX' not in got: raise AssertionError('RFB button reached guest but missed WM Close')
    RES['result']='PASS: RFB keyboard, notes, pointer motion and Close'

def main():
    if LOG.exists():LOG.unlink()
    cmd=['qemu-system-x86_64','-accel','tcg','-m','1024','-smp','1','-boot','d',
        '-cdrom','build/FalconOS.iso','-display','none','-vga','std',
        '-global','VGA.vgamem_mb=256','-vnc','127.0.0.1:7',
        '-monitor','none','-serial','none',
        '-debugcon','file:'+str(LOG),'-global','isa-debugcon.iobase=0xe9','-no-reboot']
    with (B/'vnc-qemu-stderr.log').open('wb') as fd:
        proc=subprocess.Popen(cmd,stdout=subprocess.DEVNULL,stderr=fd)
        try:
            # api.connect schedules a Twisted connection asynchronously: wait
            # for the actual RFB TCP listener before returning a client proxy.
            listener=False
            for _ in range(120):
                try:
                    with socket.create_connection(('127.0.0.1',5907),timeout=.5):
                        listener=True
                        break
                except OSError:
                    if proc.poll() is not None:raise RuntimeError('QEMU exited before VNC')
                    pause(.25)
            if not listener:raise RuntimeError('QEMU VNC TCP/5907 unavailable')
            with api.connect('127.0.0.1::5907',timeout=20) as c:
                shot(c,'FalconOS-VNC-Initial-Screen')
                mark(b'L',deadline=75)
                for m in (b'T',b'A',b'K',b'D',b'U'):
                    off=len(raw());k(c,'enter');mark(m,off);RES['keyboard'].append(m.decode())
                t(c,'falcon')
                for m in (b'P',b'Q',b'O'):
                    off=len(raw());k(c,'enter');mark(m,off);RES['keyboard'].append(m.decode())
                k(c,'right')
                off=len(raw());k(c,'enter');mark(b'W',off)
                off=len(raw());k(c,'enter');mark(b'H',off)
                k(c,'esc')
                pause(1)
                shot(c,'FalconOS-VNC-Desktop')
                notes(c)
                mouse(c)
        finally:
            REPORT.write_text(json.dumps(RES,indent=2,ensure_ascii=False))
            proc.terminate()
            try:proc.wait(timeout=6)
            except subprocess.TimeoutExpired:proc.kill()
if __name__=='__main__':
    try: main()
    except Exception as exc:
        RES['result']='FAIL'
        RES['errors'].append(str(exc))
        REPORT.write_text(json.dumps(RES,indent=2,ensure_ascii=False))
        raise
