# FalconOS V2 native visual/runtime iteration — 14 October 2026

This change set is experimental and lives on
`feature/v2-visual-runtime-refresh-20261014`. The stable
`FalconOS-1-release` branch is unchanged. Do NOT boot against physical drives.

## Scope already implemented in this branch

- FalconBoot/1 now reads `/FALCONOS/BOOT.BMP` from its read-only UEFI ESP.
  This is a full-color representative frame baked from the existing original
  `assets/boot/falconos_boot_intro_animation.glb`, not generated concept art.
  The separate kernel framebuffer player still runs the original 48-frame
  animation. This is a *preview frame in the loader*, not real-time GPU glTF.
- MiniWeb's bounded native renderer accepts basic combined/tag/class/id and
  grouped selectors, a simple cascade, CSS `rgb()`, multiple inline scripts,
  and additional explicitly allowlisted DOM literal mutations.
  It does **not** run arbitrary JavaScript or implement browser-standard DOM.
- Files uses real FalconFS/SHFS file associations. Users can Enter/double-click
  UTF-8-ish text files for a read-only preview and decode uncompressed BMP/24
  files with validated sizes. `.app.pkg` retains the original strict FAPP/1
  checks and interpreter; unfamiliar binary extensions are **never executed**.
- Native terminal now has a bounded previous-command recall with Up/Down.
- Dedicated GitHub Actions QA builds the UEFI ISO, tests the browser parser and
  image decoder, verifies the actual embedded boot image, and boots QEMU using
  OVMF (not legacy SeaBIOS).

## Additional native runtime work

- `Falcon Media` is a real native image/PCM inspection app, replacing the
  misleading procedural `Video` simulation at its stable built-in ID. It
  enumerates existing SHFS files; it does not invent screenshots or media.
  It paints decoded uncompressed BMP/24 and binary PPM/P6 pixels. It
  parses RIFF WAVE chunks, validates mono/stereo 8/16-bit PCM metadata
  and draws the original audio samples as a waveform; **it does not
  play audio** because a verified audio output driver is still missing.
- Files also previews PPM and WAV data and recognizes more source-code
  extensions. Unknown binary types are never treated as applications.
- F8 in Files opens a text document in Codedium; the editor tracks the real
  source filename, supports editing/saving without replacing another file,
  and rejects binary/oversized source or unsaved-document loss.
- F9 / Ctrl+N creates a blank `untitled.fsh` source buffer in Codedium
  and the new native toolbar exposes New / Save / Run / Export / Publish.
  Saving files is separate from executing restricted FAPP commands.
- FalconBoot now displays three genuine UEFI keyboard choices:
  HD 720p, FHD 1080p and QHD 1440p (where GOP supports the chosen mode).
  Up/Down moves the selected row and Enter boots immediately; unattended
  boots default to FHD after five seconds. The original dragon GLB
  preview remains visible above this actual boot menu. No installer
  wizard is used as a substitute for boot selection.

These features must not be labeled as fully functional MP4/video playback,
a general native scripting VM, a complete GPU driver, or safe physical
storage installation.

## Outstanding blocking work — not shipping yet

| Area | Current state | Release gate |
| --- | --- | --- |
| UEFI/boot | Original GLB frame, 48-frame kernel animation, UEFI display-mode menu | Capture/verify selected modes in QEMU; firmware/hardware compatibility |
| Hardware graphics | UEFI GOP + software framebuffer | Real vendor GPU modesetting, driver tests, memory safety, framebuffer transitions |
| App engine | FAPP scripts in restricted interpreter, FVM experiments | Ring 3 per-process isolation, capabilities, native loader/ABI, robust app lifecycle |
| Falco | MiniWeb intentionally limited | HTML5 tokenizer/layout, CSS modern layout and verified sandboxed JS, HTTPS independently tested |
| Multimedia | BMP/PPM preview + genuine WAV/PCM file parsing and waveform | Real audio playback, compressed video codecs, streaming and tested device drivers |
| FScript/Codedium | Real SHFS file opening, saving, blank files, safe FAPP scripts | Complete isolated VM/language, full editor features, tested packaging and debugging |
| Files/storage | SHFS/PFS/JFS prototypes | Crash-safe journal/migrations, fsck, quota, safe install/rollback |
| Updater | Host-assisted, checksum-verifying QEMU ISO stage | Signed metadata, rollback and safe native update; no drive rewriting |
| Marketplace | Existing tiny reviewed FAPP packages, limited catalog | Signed publisher packages, install/uninstall/reboot E2E, independent GUI apps |
| Window manager | Existing in-kernel window slots and controls | Per-app surfaces/processes, focus/resize/DPI/snap regressions |
| Security | No verified general native user process isolation | Ring 3, syscall ABI, permissions, memory isolation, fuzzing |

The existing `docs/CRITICAL-GAPS-AUDIT.md` documents additional technical
release blockers: TLS/HTTPS E2E, signatures, robust TCP/IP and real device
drivers. ISO compilation is never proof of physical-hardware compatibility.

## Reproduce CI locally on Arch Linux

On a disposable VM only:

```bash
git clone -b feature/v2-visual-runtime-refresh-20261014 https://github.com/hanefimert2016-oss/FalconOS.git
cd FalconOS
python3 -m venv .venv
.venv/bin/pip install 'numpy<3' trimesh opencv-python-headless pillow
PATH="$PWD/.venv/bin:$PATH" make iso ENABLE_BOOT_GLB=1 ENABLE_BEARSSL=0
python3 tools/qemu_smoke.py --boot-seconds 20
```

Requires QEMU, OVMF, clang/lld, nasm, xorriso, mtools, GCC/objcopy,
and the other project build prerequisites. The smoke tool invokes a
headless, isolated UEFI VM and creates an unretouched framebuffer PNG
when the guest actually renders usable pixels.
