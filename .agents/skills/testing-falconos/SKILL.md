# Testing FalconOS in QEMU headless

FalconOS is a bare-metal x86 kernel — there is no shell, no SSH and no
way to script it from the *inside*. Every test has to drive it from
outside, through QEMU's machine monitor.

This skill captures the harness that has worked across v2, v3 and v4.

## When to use

Any time you need to:
- Verify the kernel boots at a given resolution
- Send specific key sequences (F1, F2, arrow keys, Esc, ASCII chars)
- Capture evidence screenshots of the framebuffer
- Click in the framebuffer (mouse_move + mouse_button)

Do **not** use this for unit tests — there are none. All FalconOS
testing is end-to-end visual.

## Build

```bash
cd /home/ubuntu/repos/FalconOS
make clean && make iso RES=fhd     # 1920x1080 (default)
make clean && make iso RES=hd      # 1280x800
make clean && make iso RES=2k      # 2560x1440
```

`RES` flows from the Makefile through `-DFB_W` / `-DFB_H` into both
`multiboot2.asm` (framebuffer request) and `gfx.c` (BSS back buffer
size). The two **must** match — that is why resolution is build-time,
not runtime.

## Boot headlessly with a control socket

```bash
rm -f /tmp/falcon-mon.sock
qemu-system-i386 \
  -m 256M -no-reboot -no-shutdown -display none \
  -vga std -global VGA.vgamem_mb=32 \
  -serial null \
  -cdrom build/FalconOS.iso \
  -monitor unix:/tmp/falcon-mon.sock,server,nowait &
sleep 5
```

For `RES=2k` raise VRAM: `-global VGA.vgamem_mb=64`. The default 16 MiB
is enough for HD/FHD but **not** for 2K (back buffer is ~14.7 MiB at
1440p plus the QEMU `vga` device's own buffer).

## Send keys + capture screen

All monitor commands are line-based plain text. Use `socat` (already in
the environment) over the UNIX socket:

```bash
echo "sendkey f1" | socat - UNIX-CONNECT:/tmp/falcon-mon.sock
echo "screendump /tmp/shot.ppm" | socat - UNIX-CONNECT:/tmp/falcon-mon.sock
convert /tmp/shot.ppm /tmp/shot.png   # ImageMagick
```

Batch sequences — pipe several lines into one socat session, with
`sleep` between commands so the kernel has time to render:

```bash
( echo "sendkey f2"; sleep 1
  echo "screendump /tmp/lp.ppm"; sleep 1
  echo "sendkey right"; sleep 1
  echo "sendkey ret"; sleep 1
  echo "screendump /tmp/app.ppm"; sleep 1
) | socat - UNIX-CONNECT:/tmp/falcon-mon.sock
```

QEMU `sendkey` token names: `f1`..`f12`, `ret` (Enter), `esc`, `spc`,
`tab`, `up`/`down`/`left`/`right`, single ASCII letters/digits, plus
`shift-X` etc. for combos.

For mouse: `mouse_move dx dy` (relative — push to corner first with
large negative values, then to absolute target), `mouse_button 1` for
left-click, `mouse_button 0` to release.

## FalconOS-specific control reference

| Key | Action |
|---|---|
| `F1` | Toggle Personal ↔ Developer kernel |
| `F2` | Open / close Launchpad (Personal only) |
| Arrow keys | Launchpad: move cursor; dock: shift highlighted tile |
| `Enter` / `Space` | Launchpad: launch selected app |
| `Esc` | Close active app window or Launchpad |
| Letters/digits | Forwarded to the active app's `input(key)` callback (Terminal echo, Calculator math, etc.) |

Launchpad apps array order (`kernel/apps.c` `APPS[]`) — needed for
arrow-key cursor math:

```
0  Home           4  Terminal     8  Calendar
1  Files          5  Calculator   9  Gallery
2  Clock          6  Settings    10  Browser
3  Stats          7  Notes       11  About
```

Grid is 4 columns × 3 rows. `right`/`left` step ±1; `up`/`down` step
±4 (one row).

## REPL (Developer Kernel)

After F1 the bottom card is a REPL. Commands: `help`, `clear`, `time`,
`regs`, `peek <hex>[count]`, `poke <hex> <byte>`, `mem <hex>` (re-anchor
MEM panel), `apps`, `panic`, `echo <msg>`. Use `peek 0x100000 16` to
cross-check the MEM panel byte-for-byte.

## Common pitfalls

- **Don't `cat` the PPM.** `screendump` writes raw 24-bit PPM. Always
  convert with ImageMagick (`convert in.ppm out.png`) — the `read`
  tool only renders PNG/JPG.
- **Give the kernel ~1 second between keypresses.** `sendkey` returns
  immediately; the kernel needs a frame to redraw before the next
  screenshot reflects the new state.
- **Selection rings on light-themed Launchpad are subtle.** If unsure
  whether the cursor moved, crop both screenshots to the relevant tile
  area and compare side-by-side rather than eyeballing the full grid.
- **F1 closes Launchpad as a side-effect.** If you press F1 while
  Launchpad is open, it closes Launchpad **and** toggles the kernel —
  one key press, two effects.
- **No CI is configured** in this repo. `pr_checks` always returns
  0/0/0. Verification is purely visual.
- **Don't forget `pkill -f qemu-system` between resolution changes.**
  A stale QEMU bound to the monitor socket will silently steal your
  keys. Always kill + remove the socket file before relaunching.

## Devin Secrets Needed

None. Everything runs locally on the VM.

## Sanity-check command

If in doubt, this one-liner verifies the toolchain is intact:

```bash
cd /home/ubuntu/repos/FalconOS && \
  make clean >/dev/null && \
  make iso RES=fhd 2>&1 | tail -1
# expected: [OK] ISO   build/FalconOS.iso  (RES=fhd)
```
