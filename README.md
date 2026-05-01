# FalconOS

> A bare-metal x86 operating system with **two kernels in one**: switch
> between a macOS-styled *Personal Kernel* and a hacker-grade *Developer
> Kernel* live, with a single key-press.

```
      ___---___
    //  (   )  \\         F a l c o n O S
   /  (  ~~~  )  \
  |  / O\   /O \  |       v2.0  · DUAL KERNEL EDITION
  |  |   \ /   |  |
   \ |  _/~~~\_  | /
    \|_/       \_|/        << Born  to  Fly >>
         |   |
        /|   |\
       / |___| \
      ~~~~~~~~~~~
```

## What is it?

FalconOS is a self-hosted, ~26 kB freestanding x86 kernel that boots via
GRUB (Multiboot2) into a 1024 × 768 × 32-bit linear framebuffer and
renders its own UI in software — no BIOS, no DOS, no host OS.

Two modes coexist inside the same binary:

| Mode | What you get |
|------|--------------|
| **Personal Kernel** | macOS-inspired home screen — concentric breathing logo, glass-style dock with 5 circular app tiles, up-time card, top status pill |
| **Developer Kernel** | Live CPU register snapshot, scrollable memory inspector (Up / Down), kernel log viewer — everything refreshes every frame |

Hit **F1** at any time to flip between them — there is no context switch,
just a different render path inside the same dispatcher.

## Screenshots

| Personal Kernel | Developer Kernel |
|-----------------|------------------|
| ![Personal](https://app.devin.ai/attachments/b1d6abea-ac3c-4b22-ab82-3e1fb31bd9d2/screen2.png) | ![Developer](https://app.devin.ai/attachments/bdea4d55-2a39-47fc-8998-d3948d9c827d/screen3.png) |
| macOS-style home screen, glass dock, breathing logo | live registers, memory inspector, kernel log |

Captured live from QEMU booting the GRUB ISO produced by `make iso`.

## Architecture (≈ 1 000 LOC)

```
boot/
  multiboot2.asm         GRUB-compatible header + 32-bit entry stub
  grub.cfg               one-liner GRUB menu
linker.ld                kernel loaded at 1 MiB
kernel/
  falcon.h               public types, theme palette, module API
  cpu.c                  inb / outb / rdtsc + tiny libc subset
  gfx.c                  software renderer: AA circle, rounded rect, text
  font_data.c            8 × 16 bitmap font (auto-generated, ASCII 0x20-0x7E)
  kbd.c                  polled PS/2 keyboard, no IDT/PIC
  main.c                 entry, multiboot2 framebuffer parse, dispatcher
  personal.c             Personal Kernel UI (≈ 100 lines)
  dev.c                  Developer Kernel UI (≈ 130 lines)
tools/
  genfont.py             regenerates font_data.c from DejaVu Sans Mono
```

Design choices favouring brevity:

- **No IDT / PIC / paging / heap.** We ride GRUB's flat 32-bit GDT and
  poll the keyboard at frame rate.
- **Software-rendered UI** with a 3 MiB off-screen back buffer. Every
  frame is drawn from scratch and flipped in one `gfx_present()` call.
- **2× super-sampled circle** rasteriser keeps AA primitives below 30
  lines while looking crisp at any radius.
- **No libc** — `cpu.c` provides the few helpers we need
  (`k_strlen`, `k_strcpy`, `k_itoa`, `k_memset`, `k_memcpy`).

## Build & boot

### Requirements (Debian / Ubuntu)

```bash
sudo apt install -y gcc nasm grub-pc-bin grub-common xorriso mtools \
                    qemu-system-x86 fonts-dejavu-core python3-pil
```

### Build the kernel and ISO

```bash
make            # build/falcon.elf       (~26 kB)
make iso        # build/FalconOS.iso     (bootable GRUB ISO)
make run        # boot the ISO in QEMU (SDL window)
make run-fb     # boot kernel.elf directly via QEMU -kernel  (faster)
make font       # regenerate kernel/font_data.c from DejaVu
make clean
```

### Boot on real hardware

```bash
sudo dd if=build/FalconOS.iso of=/dev/sdX bs=4M status=progress oflag=sync
```

> ⚠️  Real-hardware boot has been tested only via Multiboot2 + GRUB; UEFI
> systems must boot through the GRUB EFI image.

## Controls

| Key                | Action                                        |
|--------------------|-----------------------------------------------|
| **F1**             | Toggle Personal ↔ Developer kernel            |
| ← / →              | (Personal) navigate the dock                  |
| ↑ / ↓              | (Developer) page the memory inspector         |
| **L**              | (Developer) push a manual log line            |

## Roadmap

- [ ] Mouse cursor + click hit-testing on the dock
- [ ] PIT-driven smooth animation timing
- [ ] FAT12 read-only "Files" app for the Personal Kernel
- [ ] Tiny built-in REPL for the Developer Kernel
- [ ] UEFI loader (gnu-efi)

## License

MIT — see [LICENSE](LICENSE)
