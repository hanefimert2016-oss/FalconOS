<div align="center">

# FalconOS 1 — *Born To Fly*

A 64-bit bare-metal operating system written from scratch.
GUI shell, multi-user auth, package manager, real disk persistence,
Liquid Glass theming, deterministic Jarvis assistant, vendored
BearSSL TLS engine.

**Full documentation, screenshots, downloads, and changelog → [falconos.tech](https://falconos.tech/)**

</div>

---

## Quick start

```bash
# 1. install build deps  (Debian / Ubuntu)
sudo apt-get install -y nasm gcc make grub-pc-bin grub-common xorriso \
                        mtools qemu-system-x86 python3

# 2. build the ISO  (≈ 5.5 MB)
make iso

# 3. boot it in QEMU  (8 GiB RAM, persistent qcow2 disk)
make run
```

The ISO is **one file** that boots HD / FHD / 2K via the GRUB menu, runs
on real x86_64 hardware (BIOS or UEFI) and on every major hypervisor
(QEMU, VirtualBox, VMware, Hyper-V).

## What ships in the box

|  |  |
| --- | --- |
| **Boot** | 3D "FalconOS 1 / Born To Fly" wordmark splash, single ISO for HD/FHD/2K |
| **Install wizard** | 11 UI languages (TR EN DE FR ES IT PT RU AR ZH JA), keyboard layouts (TR-Q / TR-F / US), disk picker over ATA-PIO, password confirm with live mismatch warning |
| **Desktop** | Windows 11-style centred icon taskbar, top panel with 4 workspaces + Jarvis pill, animated Liquid Glass v3 (specular sweep), 5 themes (Lumen / Nox / Liquid / Nordic / Rose Gold), 7 accent colours |
| **Apps** | 18 built-ins — Files, Terminal, Editor, Settings, Notes, Calc, Music, Falco browser, Mağaza (Pardus-style two-pane), Jarvis chat, Calendar, Clock, Image Viewer, Tic-Tac-Toe, Snake, Stopwatch, Help drawer |
| **Filesystem** | Hierarchical shfs (`/`, `/home/falcon/...`, `/etc`, `/tmp`, `/usr`, `/var/prg`); real `mkdir -p / touch / cp / mv / rm -r`; Files app + Terminal share the same store; install state persists across reboots |
| **Shell** | ≈ 110 POSIX commands (`ls / cd / pwd / cat / grep / awk / sed / find / wget / ip / ps / df / tar / md5sum / tree / xxd / cowsay …`) |
| **Jarvis** | ≈ 56 deterministic intents (no LLM). Switches themes, installs packages, reports network, opens apps, answers status questions |
| **Store** | 158 packages (apps / dev tools / themes / fonts / locales / games / libs); install state saved to disk |
| **Multi-user** | ≤ 8 users, PBKDF2 password hashing, lock-screen, ATA-backed `users.bin`/`creds.bin` |
| **Network** | Real PCI virtio-net probe + MAC injection. `ip` / `arp` / `route` / `ifconfig` / `wget` route through real driver state |
| **TLS** | **Vendored BearSSL 0.6** (MIT, 25 K LoC, constant-time, zero-malloc) linked into the kernel ELF — 200 `br_*` symbols, `libbearssl.a` is 791 KB |

For the full feature list, architecture diagrams, in-depth screenshots,
boot videos, package screenshots, and the deferred-features roadmap,
visit **[falconos.tech](https://falconos.tech/)**.

## Architecture (short version)

* **boot**: GRUB 2 multiboot2 → 64-bit long mode → handover to `kernel/main.c`
* **kernel**: freestanding C, no libc, no malloc, no dynamic loader
* **drivers**: PIT, RTC, PIC, ATA-PIO, PS/2 keyboard + mouse, USB stub, virtio-net (PCI probe), framebuffer (VBE / GOP)
* **graphics**: software 2D pipeline (`gfx_*`), separable Gaussian blur cascade, 7-tap Liquid Glass, alpha compositing
* **filesystem**: shfs hierarchy persisted in a FALCONFS superblock on the install disk
* **TLS**: vendored BearSSL + freestanding shim (`vendor/bearssl-shim/`) so the library compiles under `-nostdinc -mno-sse`

Source layout, line counts, design rationale, and the v1.3-tls roadmap
(virtio-net virtqueues, bareTCP stack, Mozilla root bundle, end-to-end
HTTPS GET) are documented in detail on **[falconos.tech](https://falconos.tech/)**.

## Controls (cheat-sheet)

| Key | Action |
| --- | --- |
| **F1** | Help drawer |
| **F2** | Launchpad |
| **F3** | Jarvis chat |
| **F12** | Power menu |
| **Ctrl + Alt + ← / →** | Switch workspace |
| **Esc** | Close modal / back |
| **Super** | Taskbar focus |

## Honest deferred list (what isn't real yet)

* **HTTPS to a live server** — BearSSL is linked but the TCP/IP stack
  and real virtio-net virtqueue setup are queued for the next commit-set
  on the [`feat/falconos-1.3-tls`](https://github.com/hanefimert2016-oss/FalconOS/tree/feat/falconos-1.3-tls)
  branch.  `wget https://...` returns `TLS engine: BearSSL 0.6 (vendored)
  / status: TLS linked, bareTCP transport pending` instead of a fake
  200 OK.
* **LLM Jarvis** — there is no model in the ISO; Jarvis is a deterministic
  intent engine with ~56 hand-written handlers.
* **EXE / AppImage runtime** — `prg install foo.exe` copies the file but
  does not execute it (no Win32 / glibc personality).
* **CJK / Cyrillic / Arabic glyphs in body text** — the installer headline
  renders them, but the kernel body font is still Latin-only.

More detail and the full roadmap: **[falconos.tech](https://falconos.tech/)**.

## License

MIT.  All third-party code (BearSSL, GRUB stubs, the optional Mozilla
NSS bundle) ships with its own original licences preserved in
`vendor/*/LICENSE` and `LICENSE`.

---

<div align="center">

[falconos.tech](https://falconos.tech/) · [Releases](https://github.com/hanefimert2016-oss/FalconOS/releases) · [Issues](https://github.com/hanefimert2016-oss/FalconOS/issues) · [v1.3-tls branch](https://github.com/hanefimert2016-oss/FalconOS/tree/feat/falconos-1.3-tls)

</div>
