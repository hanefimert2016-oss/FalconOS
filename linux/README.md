# `linux/` — Linux compatibility layer

This directory contains FalconOS's Linux compatibility shims.

## What's actually here

| File            | Purpose                                                                       |
|-----------------|-------------------------------------------------------------------------------|
| `uapi.h`        | Linux-style UAPI types and constants (`ATA_*`, `KEY_*_LX`, `BLK_STS_*`).      |
| `ata_pio.c`     | ATA PIO disk driver, structured after Linux's `drivers/ata/libata-core.c`.    |
| `hid_keymap.c`  | PS/2 set-1 → Linux keycode map, modelled on `drivers/input/keyboard/atkbd.c`. |

## What this is

Architecturally this layer makes FalconOS **API-compatible** with parts of
Linux: function names, register-bit constants and probe state machines
mirror Linux exactly so that future ports of real Linux drivers can be
dropped in with minimal source changes.

The `prg` package manager (see `kernel/prg.c`) treats `linux-uapi`,
`ata-pio` and `hid-keymap` as built-in packages, so `prg list compat`
shows them as installed and immutable.

## What this is NOT

This layer does **not** load `.ko` kernel modules or run unmodified
Linux ELFs.  Doing that requires a full Linux syscall ABI, libc, and
fault-handling/scheduler infrastructure that FalconOS deliberately
ships without.  Each file in this directory is a clean-room
implementation written from public hardware specifications
(AT-Attachment, PS/2, USB-HID 1.11) — no Linux kernel source code was
copied.

If you do want to bring in actual Linux source code in the future, the
filenames + symbol layout here mean any subsequent port-in compiles
directly on top.
