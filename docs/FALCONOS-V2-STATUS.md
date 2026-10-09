# FalconOS v2 — verified state and architecture migration plan

Last reviewed: 2026-10-09. This branch is **experimental**, and may only
be used in QEMU. Nothing has been merged into the stable default branch.

## Implemented in feature/marketplace-codedium

- Bootable GRUB ISO and a graphical software-rendered shell.
- Safer settings persistence constrained to an existing dedicated MBR
  type-0xFA partition. No implicit writes to disk sector zero.
- A real public **GitHub Releases** application registry in
  hanefimert2016-oss/FalconOS-Marketplace.
- FAPP/1 ASCII-script packages, built from reviewed source and distributed as
  a single versioned **.app.pkg** plus .sha256 sidecar.
- HTTPS asset download **only via an opt-in host-assisted QEMU serial bridge**.
  The bridge checks the published sidecar; the guest recomputes SHA-256 and
  validates an allowlist of non-destructive shell commands before use.
- Persistent installed-package cache: up to 48 slots (4 KiB each) inside
  the same validated dedicated partition. Offline apps are listed and launch.
- Update detection for installed packages based on version mismatch;
  manual update and remove.
- CodeDium local editor with cursor motion, save/run/export and one persistent
  4 KiB project slot when the dedicated partition is selected.
- Real SHFS entries shown in Files, safe path length checks, Launchpad
  registration and pagination for downloaded apps.
- GitHub Actions tests: Python bridge tests, C mock ATA persistence tests,
  C native Marketplace protocol tests, Multiboot2 validation, ISO build,
  graphical QEMU smoke, and scripted keyboard/UI walkthrough.
- CodeDium Web sources are in Marketplace/site/index.html. Browser export
  works without access tokens; contributors submit files via GitHub fork/PR.

## NOT implemented — do not present as shipping

1. **No native guest TCP/IP, DNS, or HTTPS** — see #21. The host COM1 bridge
   is the only working download transport.
2. **No independent native ELF apps, memory isolation, ring 3, or a real
   process scheduler** — see #20. Current FAPP apps interpret a restricted
   list of Terminal commands inside the kernel.
3. **No journaled general-purpose filesystem** — see #22. The current RAM
   SHFS and 48 fixed disk slots are a bounded prototype.
4. **No production-ready NVMe / xHCI / GPU acceleration** — see #23.
5. **No true concurrent windows or app process SDK** — see #24.
6. **No app publisher signatures or permission grant UI** — see #25.
   Checksums detect changes but do not establish publisher identity.
7. **GitHub Pages may not be live**: enabling the Pages site requires the
   repo owner's GitHub Settings -> Pages -> GitHub Actions. The GitHub
   Actions token was denied permission to create a Pages site.
8. **No production or physical-hardware safety certification**. Never attach
   a user's Windows/Linux SSD for these experiments.

## QEMU run and acceptance checks

On an Arch Linux development machine, install gcc, make, nasm, grub, xorriso,
mtools, qemu-desktop and python; then:

    git clone --branch feature/marketplace-codedium \
      https://github.com/hanefimert2016-oss/FalconOS.git
    cd FalconOS
    make run-market RAM=4096 CPUS=2

The build creates a separate **sparse throwaway 4 GiB demo disk**. Never
reuse a physical-disk path as the demo image. The browser code publisher is
source-first: contributions go through reviewed GitHub pull requests.

Unit tests can run on the host:

    python3 -m unittest discover -s tests -p 'test_*.py' -v
    make iso

CI artifacts include FalconOS ISO and QEMU screenshots. A screenshot or
successful ISO compile is NOT proof of end-to-end internet downloading:
a real GET/verify/install/run/reboot test remains required (#25).

## Architecture dependency order

First implement page allocator + ring 3 and system calls (#20), in parallel
with deterministic QEMU block and VirtIO-Net tests (#21, #23). Then implement
a journaled app filesystem (#22). Add independent UI window/app processes
(#24) and publisher signatures, permissions and SDK (#25) before accepting
general-purpose third-party native packages.
