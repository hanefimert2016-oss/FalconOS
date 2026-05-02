# FalconOS

> A bare-metal **x86_64** operating system with **two kernels in one**:
> switch between a macOS-styled *Personal Kernel* and a hacker-grade
> *Developer Kernel* live, with a single key-press.

```
      ___---___
    //  (   )  \\         F a l c o n O S
   /  (  ~~~  )  \
  |  / O\   /O \  |        FalconOS 1
  |  |   \ /   |  |       64-bit long mode
   \ |  _/~~~\_  | /
    \|_/       \_|/        << Born  to  Fly >>
         |   |
        /|   |\
       / |___| \
      ~~~~~~~~~~~
```

## What is it?

FalconOS is a self-hosted, ~84 kB freestanding **64-bit** kernel that
boots through GRUB (Multiboot2) into a configurable linear framebuffer
(HD / 1080p / 2K), identity-maps the first 4 GiB of physical RAM with
2-MiB huge pages, and renders its entire UI in software — no BIOS, no
DOS, no host OS, no external libraries.

Two kernels live inside the same binary:

| Mode                    | What you get |
|-------------------------|--------------|
| **Personal Kernel**     | A "dolu dolu" desktop — top menu bar, 6 information widgets (Weather, Calendar, System, Now-Playing, Recents, Quick), pinnable desktop shortcuts down the left edge, a Big-Sur dock with 7 visible tiles, Launchpad (F2). No more centred breathing circle. |
| **Developer Kernel**    | Live CPU register snapshot, scrollable memory inspector, BIOS memory-map panel, scrolling kernel log, interactive REPL — everything refreshes every frame |

Hit **F1** at any time to flip between them. **F2** in Personal mode
opens the **Launchpad** (a full-screen 4 × 4 grid of all built-in
apps). Inside the Launchpad, press **P** on a tile to pin / unpin it
to the desktop. **Esc** closes the active app or Launchpad.

## What's new in **FalconOS 1**

This is the **canonical 1.0 release**. Everything below is built into a
single ISO that boots on real hardware (BIOS + UEFI) or in QEMU, with
no external runtime, no libc, and no host OS dependency.

- **Liquid-Glass theme + theme picker** — five preset themes ship in
  the box (`Lumen`, `Nox`, `Liquid`, `Nordic`, `Rose Gold`) and can be
  switched at runtime from `Settings ▸ Theme`. The new "Liquid" preset
  pushes the Aero blur radius and tint further for a true frosted-
  glass / refraction look across every panel and modal.
- **Window manager** — every app window can now be **dragged from the
  title bar**, **resized from the bottom-right corner**, **minimised**
  to the dock, **maximised** to fill the workspace, or **closed** via
  the macOS-style traffic-light buttons. State is per-window, no
  global override.
- **POSIX shell in Terminal** — Terminal app now hosts a real shell
  that understands `cd`, `ls`, `cat`, `echo`, `pwd`, `clear`, `help`,
  variable assignment (`X=value`), `$X` expansion, simple pipes
  (`a | b`), output redirection (`> file`), and `if/then/fi` +
  `for ... in ... do ... done`. Built-in commands plus a small set of
  GNU-style coreutils — enough to run shipped FalconOS shell scripts.
- **Differentiated Developer Kernel UI** — the dev mode is no longer
  the same shell with different colours: it's an IDE-style layout
  with a left navigator pane (CPU / Memory / Map / Disk), a top tool
  strip (Build / Run / Step / Stop), and the bottom REPL is now a
  full-width drawer. Personal and Developer kernels feel like two
  different operating systems in one binary.
- **Real-hardware boot** — the GRUB hybrid ISO is verified BIOS +
  UEFI capable; `dd` it to a USB stick and it boots a 64-bit PC
  directly with no extra packaging.
- **Improved installer flow** — the language picker now shows a
  highlighted accent pip per language (visible "you are here" cue),
  and the password step asks twice (with live-confirmed match) before
  committing the PBKDF2 hash. Strength meter still live (Weak / OK /
  Strong, in 5 languages).
- **More packages in the Store** — `app-paint`, `app-music`,
  `app-snake`, `app-dosbox-lite`, `theme-nordic`, `theme-rosegold`,
  `lib-png`, `lib-zlib` ship as built-in catalogue entries; install /
  remove from the GUI Store or via `prg install <pkg>` in the
  Developer Kernel REPL.
- **Native driver labels** — the About panel and on-screen status
  strips no longer use "Linux"-prefixed names. Drivers are documented
  as native FalconOS subsystems (`ATA / 2 disks`, `Keymap: 13 keys`).
  Source attribution for driver code lives in `linux/README.md` only.
- **Doubled PBKDF2 (50 k → 100 k rounds)** + persistent fail counter
  + `failed_attempts / last_login_uptime / last_fail_uptime` survive
  reboots so the throttle window is not bypassable by power-cycling.
- **Better drivers** — proper `Shift / Ctrl / Alt / CapsLock` modifier
  state, extended-prefix scancodes (`Home / End / PgUp / PgDn / Del`),
  and a macOS-style mouse acceleration curve (1:1 for fine motion,
  quadratic boost capped at 16× for fast flicks). Double-click is
  detected within a 350 ms window.
- **All carry-over from previous releases** — Aero frosted-glass on
  every panel and every modal sheet, RTC + timezone (13 cities),
  5 languages (EN / TR / DE / FR / ES) with locale-aware date and
  number formatting, multi-user (≤ 8 accounts) with disk-persistent
  records, x86_64 long mode, two kernels in one binary.

## Legacy: v5.2 — "Aero"

- **Frosted-glass transparency (Aero)** — every translucent panel
  now actually blurs the pixels behind it instead of using a flat
  alpha overlay. The dock, top menu bar, lockscreen password pill,
  Launchpad backdrop, app window chrome and every glass widget all
  go through `gfx_aero_round_rect()`, which runs a two-pass
  separable box blur over the live back buffer (radius capped at 8,
  scratch buffer 1920 × 480 in BSS) and then tints the result.
  Toggle globally from Settings ▸ "Aero" if you're on a slow box —
  fallback is the previous flat panel look.
- **Real wall-clock + timezone** — the menu bar and lockscreen now
  show the actual time of day from the CMOS RTC (MC146818 reader in
  `kernel/rtc.c`, handles BCD/binary, 12h/24h, update-in-progress
  polling). The timezone is configurable: Settings ▸ "Timezone"
  cycles 13 curated cities from Honolulu (UTC-10) to Sydney
  (UTC+10), including UTC+5:30 for Mumbai. Date is rolled across
  midnight / month / year boundaries (incl. leap February) when the
  offset crosses a day.
- **5 languages, locale-aware formatting** — Türkçe and English get
  full coverage; Deutsch / Français / Español are at baseline
  (installer, lockscreen, settings labels, login welcome). New
  `TX(en, tr, de, fr, es)` helper in `kernel/locale.c` falls back to
  English on missing translations. Date formatting is locale-correct
  too: MM/DD/YYYY for English, DD.MM.YYYY for German, DD/MM/YYYY
  everywhere else, with localised month abbreviations (`Oca / Jan /
  Ene` etc.). The number grouping helper switches `1,234.56` ↔
  `1.234,56` as appropriate.
- **First-run welcome banner** — after the installer finishes, the
  desktop shows a one-shot greeting card with the user's name in
  their language ("Welcome, mert!" / "Hosgeldin, mert!" / etc.) and
  the three keyboard shortcuts that get a normal user productive
  immediately (F2 / F1 / Esc). Dismissed by clicking *OK* — the
  dismissal is persisted to disk so it never reappears.
- **Smarter keyboard defaults** — picking a non-Turkish language in
  the installer auto-selects US-QWERTY instead of TR-Q. Existing
  Turkish-speaking users still get TR-Q out of the box.

## What's new in v5 — "Aurora"

- **64-bit (x86_64) long mode** — boot stub flips the CPU into long
  mode, builds a 4-level page table, identity-maps 4 GiB with 2-MiB
  huge pages, loads a 64-bit GDT, and tail-calls into a System-V AMD64
  C entry point. Kernel itself is a 64-bit ELF.
- **Multi-user system (up to 8 accounts)** — every user has its own
  name, accent colour, and password hash. The first user created
  becomes the system **default**: every cold boot opens the lock
  screen focused on that user (auto-login target). ←/→ on the lock
  screen switches between user avatars.
- **PBKDF2-HMAC-SHA256 password hashing** — passwords are *never*
  stored as plaintext. Each account carries a 16-byte random salt
  (TSC-seeded RNG) and a 32-byte PBKDF2-HMAC-SHA256 hash stretched
  50 000 rounds. Verification uses constant-time comparison, and
  every transient password buffer (in the installer, in the lock
  screen, and inside `users_verify()`) is wiped with a volatile
  `k_explicit_bzero()` so plaintext doesn't linger in BSS. The lock
  screen also throttles brute-force attempts: after 3 wrong tries it
  freezes new submissions for 5 seconds. See `kernel/auth.c` for the
  full SHA-256 + HMAC + PBKDF2 implementation (RFC 2898 §5.2,
  FIPS 198-1, FIPS 180-4 — clean-room, no libcrypto).
- **Disk-backed user database (FalconFS)** — the entire `settings_t`
  (including all 8 user records, salts and hashes) is written to
  LBA0–3 of the primary IDE disk on every change, with a custom
  `'FALC'`-magic superblock and a Fletcher-16 checksum. Cold reboots
  restore the user list and skip the installer. See `kernel/diskdb.c`
  + `linux/ata_pio.c` (LBA28 PIO read/write).
- **Multi-step installer wizard** — language (Türkçe / English), theme
  (Lumen / Nox), accent colour (5 presets), **keyboard layout
  (TR-Q / TR-F / US-QWERTY)**, then a repeating *user create* loop
  (name + password) until the operator picks "Hayır, bitir".
- **Switchable keyboard layout** — TR-Q (default for Turkish), TR-F
  (typewriter heritage) and US-QWERTY scancode tables in `kernel/kbd.c`.
  The layout selected in the installer is stored in
  `SET.kbd_layout`; Settings ▸ Klavye düzeni cycles between them at
  runtime.
- **Lock screen with multi-user picker** — avatar strip with one
  circle per active user; the system-default user is highlighted by
  a green pip and is auto-focused. ←/→ scrolls users, type to enter
  the password (masked, caret blink), Enter calls
  `users_verify()` (PBKDF2 compare) — wrong password triggers a
  shake animation, correct password records `SET.active_user` and
  hands off to the desktop.
- **Dark theme "Nox"** — full counterpart to the v4 "Lumen" light
  theme. Every render call goes through a runtime palette so flipping
  `SET.theme = THEME_DARK` changes the entire shell on the next frame.
- **Desktop widgets (no centre circle)** — 6 information cards in a
  3 × 2 grid replace the v4 hero animation: Weather, Calendar mini,
  System, Now-Playing, Recents, Quick actions.
- **Desktop shortcuts** — pin any app to the wallpaper from inside
  the Launchpad with `P`. Pinned apps render as icon tiles down the
  left edge and launch with a single click.
- **Runtime Settings app** — theme, accent, language, dock size,
  animations toggle, viewport (letterbox) resolution, password change,
  lock-now action.
- **`prg` package manager + Store app** — unified package manager
  with a Linux-style CLI shape (`prg list / search / info / install /
  remove / installed`). Built-in catalog of 16 packages (6 OS
  built-ins, 10 user-installable extras across themes, apps, libs,
  Linux-compat). The Store app browses, installs and uninstalls
  packages live.
- **Linux integration (real, not mocked)** — three concrete pieces of
  Linux code are wired into the kernel:
  1. **Linux UAPI shim** (`linux/uapi.h`) — clean-room
     reimplementation of the small subset of `<linux/types.h>`,
     `<linux/ata.h>` and `<linux/hid.h>` that the kernel uses.
     Future Linux drivers can drop in unmodified.
  2. **`libata`-style ATA PIO driver** (`linux/ata_pio.c`) —
     port-style probe + `IDENTIFY DEVICE` + LBA28 PIO read/write
     against the primary IDE controller, modelled after Linux's
     `drivers/ata/libata-core.c`. Detected drives + model strings are
     surfaced in Settings ▸ About.
  3. **PS/2 ↔ Linux HID keymap** (`linux/hid_keymap.c`) — Linux
     `KEY_*` codes mapped from PS/2 scancode set 1, modelled after
     `drivers/input/keyboard/atkbd.c`.

  All three are clean-room (specifications + public headers, no Linux
  source code copied) and ship under the same MIT licence as the rest
  of FalconOS.

## Boot flow

```
GRUB Multiboot2
    │
    ▼
boot/multiboot2.asm  (32-bit)
    ├─ build PML4 / PDPT / PD  (identity-map 4 GiB)
    ├─ enable PAE + IA32_EFER.LME
    ├─ enable CR0.PG  (long mode active)
    ├─ load 64-bit GDT, far-jump
    ▼
kernel/main.c long_start (64-bit)
    ├─ parse Multiboot2 framebuffer + memory map
    ├─ install IDT / PIC / PIT / mouse / Linux-compat / Settings
    ├─ boot splash (~700 ms, palette-aware fade)
    │
    ├─ first boot? ─ yes ─▶  installer wizard
    │                          (lang → theme → accent → password → owner)
    │
    ├─ lock screen  (owner avatar + password input)
    │
    └─ desktop loop @ 50 FPS
         ├─ Personal Kernel  ──── F1 ──▶ Developer Kernel
         │     ├─ uptime card
         │     ├─ resolution card
         │     ├─ widgets_render()       6-card grid
         │     ├─ desktop_pins_render()  left-edge shortcuts
         │     └─ dock + apps
         └─ Developer Kernel
               ├─ CPU / MEM / MMAP / LOG panels
               └─ REPL prompt
```

## Build & boot

### Requirements (Debian / Ubuntu)

```bash
sudo apt install -y gcc nasm grub-pc-bin grub-common xorriso mtools \
                    qemu-system-x86 fonts-dejavu-core python3-pil
```

> The kernel builds with the host's native 64-bit gcc using
> `-m64 -ffreestanding -mno-red-zone -mcmodel=kernel -mno-sse -mno-sse2`,
> so no x86_64-elf-gcc cross compiler is required.

### Build the kernel and ISO

```bash
# default — 1920 × 1080
make iso                  # build/FalconOS.iso

# explicit resolution
make iso RES=hd           # 1280 × 800
make iso RES=fhd          # 1920 × 1080  (default)
make iso RES=2k           # 2560 × 1440

make            # just build/falcon.elf  (~84 kB ELF64)
make font       # regenerate kernel/font_data.c from DejaVu
make clean
```

### Run in QEMU

```bash
# ---- ephemeral (no disk; installer wizard runs every cold boot) ----------
make run                  # SDL window (default RES=fhd, x86_64)
make run RES=hd           # SDL window at 1280 × 800
make run RES=2k           # SDL window at 2560 × 1440
make run-headless         # no window — useful for screenshots / CI
make run-fb               # boot the ELF directly via -kernel (faster iter)

# ---- persistent disk: user accounts + settings survive reboots -----------
# `make run-disk` creates a 64 MiB raw IDE drive at build/falcon.img on the
# first run, then attaches it as the primary master. The kernel writes the
# whole user database (incl. PBKDF2 hashes) to LBA0–3 on every change.
make run-disk             # SDL window  + persistent disk
make run-disk-headless    # no window   + persistent disk
make wipe-disk            # delete build/falcon.img → installer next time

# ---- manually attach a disk image to a one-off run -----------------------
qemu-system-x86_64 -cdrom build/FalconOS.iso -m 512M \
    -drive file=build/falcon.img,format=raw,if=ide,index=0 \
    -no-reboot -no-shutdown -display sdl -vga std -global VGA.vgamem_mb=64
```

### First boot — what to expect

1. **Boot splash** — animated rings + "starting FalconOS 1", ≈ 700 ms.
2. **Installer wizard** (only on the very first cold boot, or after
   `make wipe-disk`):
   1. Language ─ Türkçe / English
   2. Theme ─ Light (Lumen) / Dark (Nox)
   3. Accent ─ Blue / Purple / Green / Pink / Graphite
   4. Keyboard ─ TR-Q / TR-F / US-QWERTY
   5. User name (24 chars max, type → Enter)
   6. User password (24 chars max — type → Enter; PBKDF2 hashed)
   7. *"Add another user?"* — Yes opens steps 5–6 again, No commits
      `SET.installed = true` and writes the whole settings + user
      table to LBA0 via `diskdb_save()`.
3. **Lock screen** — focused on the system-default user (the first
   one created); ←/→ to switch user, type the password, Enter to
   unlock. The desktop shell starts when `users_verify()` succeeds.
4. **Desktop** — top menu bar, six widgets, dock, Launchpad on F2.
   `Settings ▸ Diske kaydet` writes any subsequent changes back to
   the disk; on the next cold boot the wizard is skipped entirely.

### Boot on real hardware (USB stick → bare-metal PC)

The `grub-mkrescue` build produces a **hybrid ISO** that contains both
an `isolinux`-style BIOS bootblock and an EFI System Partition with
GRUB's `bootx64.efi`, so the same image works on:

- 64-bit BIOS / Legacy boot PCs and laptops
- 64-bit UEFI PCs (Secure Boot must be **disabled** — the kernel is
  not signed)
- Most virtual machines (QEMU, VirtualBox, VMware Workstation)

#### Linux / macOS host

```bash
# 1. Identify your USB stick (DO NOT pick the wrong device — this wipes it).
lsblk                                          # Linux
diskutil list                                  # macOS

# 2. Write the ISO directly. The hybrid layout means no extra packaging.
sudo dd if=build/FalconOS.iso of=/dev/sdX bs=4M status=progress oflag=sync   # Linux
sudo dd if=build/FalconOS.iso of=/dev/diskN bs=4m                            # macOS
sync
```

#### Windows host

Use **Rufus** (https://rufus.ie) — open `build/FalconOS.iso`, pick the
USB device, leave "DD image" mode selected when Rufus prompts. Click
*Start*, wait, eject.

#### Boot it

1. Plug the USB stick in, power-cycle the machine.
2. Tap your boot-menu hotkey (F12 / F10 / Esc — vendor-specific).
3. Pick the USB stick.
4. GRUB loads → kernel splash → installer wizard on first boot.

#### Hardware requirements

| Component       | Minimum                                            |
|-----------------|----------------------------------------------------|
| CPU             | x86_64 (CPUID.80000001:EDX bit 29 = 1)             |
| RAM             | 256 MiB at HD, 512 MiB at FHD, 1 GiB at 2K         |
| Storage         | 64 MiB free (any IDE / SATA disk; FALC superblock) |
| Display         | VESA-compatible, ≥ 1024 × 768 at 32-bpp            |
| Keyboard        | PS/2 or USB (auto-emulated)                        |
| Mouse           | PS/2 or USB (auto-emulated). Optional.             |

If long mode is unavailable the boot stub `hlt`s with a clear panic
banner instead of triple-faulting.

## Controls

| Key            | Mode       | Action                                          |
|----------------|------------|-------------------------------------------------|
| **F1**         | desktop    | Toggle Personal ↔ Developer kernel              |
| **F2**         | Personal   | Open / close the Launchpad                      |
| **Esc**        | any        | Close active app or Launchpad                   |
| ← / → / ↑ / ↓  | Personal   | Navigate dock / Launchpad                       |
| **Enter**      | Launchpad  | Open the highlighted app                        |
| **P**          | Launchpad  | Pin / unpin the highlighted app to the desktop  |
| Mouse click    | Personal   | Click pinned shortcut, dock tile or Launchpad   |
| Mouse right    | Personal   | Pin / unpin the dock tile under the cursor      |
| typing         | installer  | Password & owner-name input                     |
| typing         | lockscreen | Password input                                  |
| ↑ / ↓          | Developer  | Page the memory inspector (± 0x80 / press)      |
| **L**          | Developer  | Push a manual log line                          |
| typing         | Developer  | Forwarded to the REPL prompt                    |
| typing         | Apps       | Forwarded to the active app                     |

## prg package manager

`prg` is a unified package manager that ships with FalconOS. Its CLI
deliberately mirrors `apt` / `pacman` so future Linux-package
compatibility layers drop in seamlessly. Inside the Developer Kernel
REPL:

```
prg list                   # all known packages
prg installed              # only installed ones
prg search <term>          # substring filter on name + summary
prg info <pkg>             # show metadata for one package
prg install <pkg>          # mark installed
prg remove  <pkg>          # mark removed (built-ins refuse)
```

The Personal Kernel ships a graphical **Store** app that wraps the
same package database with category filters, install / uninstall
buttons and a "yüklü" badge.

## Built-in apps

| # | App        | What it does                                                |
|---|------------|-------------------------------------------------------------|
| 1 | Home       | Quick-link cards (Recent / System / Network / Theme)        |
| 2 | Files      | Mock file browser with a striped 12-row table              |
| 3 | Clock      | Analog dial + digital readout, driven by PIT IRQ0           |
| 4 | Stats      | tsc / uptime / ticks / RAM / FB resolution + pulse bar      |
| 5 | Terminal   | Fake bash prompt that echoes typed input                    |
| 6 | Calculator | 4-function arithmetic with a clickable keypad               |
| 7 | Settings   | Theme, accent, language, **keyboard layout**, dock size,    |
|   |            | animations, widgets, viewport, password change, **default**  |
|   |            | **user picker, save-to-disk, lock now**                     |
| 8 | Notes      | Free-form text pad with caret blink                         |
| 9 | Calendar   | Month grid with "today" highlighted from uptime             |
|10 | Gallery    | Lumen / Nox palette swatches with hex codes                 |
|11 | Browser    | Mock URL bar + bookmark cards                               |
|12 | Store      | prg package browser — search, install, uninstall            |
|13 | About      | Version, native subsystem summary, ATA probe results        |

## Driver source attribution

FalconOS drivers ship as native subsystems with their own names
(`ATA / 2 disks`, `Keymap: 13 keys`). The `linux/` directory in the
repo is **source attribution only** — it documents which Linux public
specifications and clean-room patterns the FalconOS drivers were
modelled after, so the licence story stays correct:

```
linux/
  README.md             ── what was modelled and from where, with credits
  uapi.h                ── clean-room subset of <linux/types.h> et al.
  ata_pio.c             ── libata-style PIO driver pattern (no copy)
  hid_keymap.c          ── PS/2 set-1 → keycode table
```

None of these files copy Linux source code. They are MIT-licensed
like the rest of FalconOS, and the runtime never advertises itself
as "Linux"-anything to the user.

## Architecture (≈ 5 900 LOC)

```
boot/
  multiboot2.asm     32-bit GRUB header + long-mode bootstrap
  isr.asm            32 exception + 16 IRQ stubs (IDT thunks)
  grub.cfg           one-liner GRUB menu
linker.ld            kernel loaded at 1 MiB (ELF64)
kernel/
  falcon.h           public types, runtime palette, module API
  cpu.c              inb / outb / inw / outw / insw / outsw / rdtsc + libc
  gfx.c              software renderer (AA circle, glass cards, text,
                     viewport letterbox)
  font_data.c        8 × 16 bitmap font (auto-generated)
  gdt.c              GDT no-op on x86_64 (boot stub already loaded)
  idt.c              IDT setup + dispatch table
  pic.c              8259A remap (IRQs 32-47)
  pit.c              100 Hz timer, HH:MM:SS uptime
  kbd.c              IRQ1 PS/2 keyboard (async, ring buffer)
  mouse.c            IRQ12 PS/2 mouse + cursor + right-click edge
  mmap.c             Multiboot2 memory-map parser
  settings.c         runtime SET + PAL() palette dispatcher + T()
                     + diskdb_load() bootstrap on init
  auth.c             SHA-256 + HMAC-SHA256 + PBKDF2 + TSC-seeded RNG
                     (RFC 2898 §5.2 / FIPS 198-1 / FIPS 180-4)
  users.c            multi-user database (≤ 8 accounts, default user
                     promotion, constant-time hash compare)
  diskdb.c           FalconFS superblock + Fletcher-16 checksum
                     persisting SET to LBA0–3 via libata-style PIO
  installer.c        first-boot wizard (lang/theme/accent/kbd/users)
  lockscreen.c       multi-user picker + PBKDF2 verify + shake-on-error
  widgets.c          6-card desktop widget grid
  desktop_pins.c     pinned-app shortcuts down the left edge
  prg.c              package database + CLI primitives
  main.c             entry, framebuffer parse, mode dispatcher,
                     menu bar, F1/F2 hot-keys, boot splash, modal loops
  personal.c         Personal Kernel — uptime + res cards, widgets,
                     pins, dock
  launchpad.c        Full-screen 4 × 4 app grid (F2) with P-pin
  apps.c             apps + window chrome + per-app input (Settings,
                     Store, About now palette- and Linux-aware)
  dev.c              Developer Kernel — CPU / MEM / MMAP / LOG / REPL
  repl.c             Interactive command parser
linux/
  uapi.h             Linux UAPI shim (types, ATA, HID)
  ata_pio.c          libata-style ATA PIO driver
  hid_keymap.c       PS/2 set-1 → Linux KEY_* table
tools/
  genfont.py         regenerates font_data.c from DejaVu Sans Mono
```

## Roadmap

- [ ] FAT12 read-only "Files" app backed by a real disk image
- [ ] Userland processes + cooperative scheduler
- [ ] UEFI loader (gnu-efi)
- [ ] PCI bus walk + AHCI driver
- [ ] Real Linux driver port — `drivers/input/keyboard/atkbd.c` →
      hooked through `linux/hid_keymap.c`
- [ ] Networking stack (RTL8139 + minimal TCP)

## License

MIT — see [LICENSE](LICENSE)
