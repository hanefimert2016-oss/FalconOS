# Testing FalconOS (bare-metal QEMU)

FalconOS is a bare-metal x86 kernel that boots via GRUB Multiboot2 and runs in a 1024x768x32 framebuffer. There are no unit tests — testing means driving the running kernel with a real keyboard / mouse / clock and inspecting framebuffer screenshots.

## Build & run

```bash
make iso             # produces build/FalconOS.iso (~6 MB)
make run             # qemu-system-i386 -cdrom build/FalconOS.iso -m 128M ...
```

For automated testing, drive QEMU via QMP rather than computer-use clicking. Launch with:

```bash
qemu-system-i386 \
  -cdrom build/FalconOS.iso \
  -m 128M -vga std -no-reboot \
  -qmp unix:/tmp/qmp.sock,server,nowait \
  -display sdl &
```

Use `-display sdl` (or `gtk`) when you want to record. Use `-display none` for headless screendump-only runs.

Wait ~5 s after spawn for GRUB + boot splash + main loop to settle before sending events.

## Driving QEMU via QMP

Minimal Python driver:

```python
import socket, json, time
s = socket.socket(socket.AF_UNIX); s.connect('/tmp/qmp.sock')
s.recv(4096)
def cmd(c):
    s.send((json.dumps(c)+'\n').encode()); time.sleep(0.05)
    return s.recv(8192).decode().strip()
cmd({"execute":"qmp_capabilities"})
```

### Keyboard

```python
cmd({"execute":"send-key","arguments":{"keys":[{"type":"qcode","data":"f1"}]}})
# multi-key: pass several entries in `keys` to type a string
```

Useful qcodes: `f1`, `ret`, `esc`, `spc`, letters `a..z`, digits `0..9`, `up`, `down`, `left`, `right`, `backspace`.

### Screendump (PPM, convert to PNG with ImageMagick)

```python
cmd({"execute":"screendump","arguments":{"filename":"/tmp/shot.ppm"}})
```

```bash
convert /tmp/shot.ppm /tmp/shot.png
```

### Mouse (PS/2 — relative deltas only)

QEMU's PS/2 mouse model has **no absolute positioning**. You can only send relative deltas:

```python
cmd({"execute":"input-send-event","arguments":{"events":[
    {"type":"rel","data":{"axis":"x","value":dx}},
    {"type":"rel","data":{"axis":"y","value":dy}},
]}})
```

**Workaround for absolute targeting:** the kernel clamps the cursor to `(0..FB.w-1, 0..FB.h-1)`. To reach a known absolute coordinate `(tx, ty)`:

1. Hammer large negative deltas (e.g. 30 calls of `(-50, -50)`) to clamp at `(0, 0)`.
2. Walk to target with smaller positive deltas summing to roughly `(tx, ty)`. Use chunks of ~30 px and `time.sleep(0.04)` between to avoid coalescing into a single PS/2 packet that the IRQ handler drops.

Left click:

```python
cmd({"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"button":"left","down":True}}]}})
time.sleep(0.05)
cmd({"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"button":"left","down":False}}]}})
```

## Dock tile coordinates (Personal Kernel)

For a 1024x768 framebuffer, 5 dock tiles, tile=64, gap=18:

- `dock_w = 5*64 + 4*18 + 32 = 424`
- `dx = (1024 - 424) / 2 = 300`
- `dy = 768 - 92 - 28 = 648`
- Tile `i` center: `(dx + 16 + i*82 + 32, dy + 14 + 32) = (348 + i*82, 694)`
  - Home `i=0`: `(348, 694)`
  - Files `i=1`: `(430, 694)`
  - Clock `i=2`: `(512, 694)`  ← keyboard default
  - Stats `i=3`: `(594, 694)`
  - About `i=4`: `(676, 694)`

If the layout in `kernel/personal.c` changes, recompute these from the dock geometry there.

## Adversarial assertions that prove v3 features

Design tests so a broken implementation produces a visibly different screenshot. The high-signal ones for this project:

- **PIT IRQ0 alive:** take two screenshots ~3 s apart with **no input events** between them; the top-right HH:MM:SS pill must advance by ≥ 2 s. A frozen `hlt` loop (no IRQ0) or a frame-counter regression both fail this.
- **REPL ↔ MEM consistency:** in Developer Kernel, run `peek 0x100000 16` and assert the 16 hex bytes in the LOG row equal row 0 of the MEM panel exactly. A broken parser, wrong addr passing, or wrong memory pointer all produce mismatches.
- **F1 hot-switch:** Personal screenshot must show blue dot + "Personal Kernel" + dock + breathing logo. After F1, screenshot must show green dot + "Developer Kernel" + 5 panels + no dock.
- **MMAP parser:** Developer MMAP panel must show `RAM 127 MB` (for `-m 128M`) and a row `0x100000 available`. Hardcoded fallbacks would print different numbers.
- **Mouse over keyboard:** keyboard `dock_idx` default is **Clock (2)**. Move the mouse to a different tile (best: About at idx 4 — far right) and click — opening About instead of Clock proves the IRQ12 click path is honored over the keyboard selection.
- **App close on Esc:** any open app must dismiss on `esc`, returning to the dock home.

## Things to skip

- **`panic` REPL command:** triggers `int 3` and halts the kernel. Implementation is wired up but verifying it prevents the rest of the test sequence — manually reset and run it last if you need it covered.
- **Boot splash visual:** the fade-in window is ~700 ms but GRUB's startup delay is variable, so reliably catching it via QMP screendump is brittle. Treat it as implicitly verified: if any later screenshot renders, the splash transitioned cleanly.
- **Right / middle mouse buttons:** only left-click is wired into the apps framework.

## Recording

Only start a screen recording when the test plan covers GUI interactions (this app: yes — boot, F1, REPL, mouse, app windows). Record QEMU running in `-display sdl` / `gtk`, not `-display none`. Maximize the QEMU window before the first annotation — a half-covered window in the recording confuses the user.

Use `annotate_recording` per phase: `setup` for boot, `test_start` per assertion, `assertion` (passed/failed) right after each screendump validates.

## Devin Secrets Needed

None. The repo has no external services, no auth, no API keys. Everything runs locally in QEMU.
