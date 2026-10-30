# FalconOS V2 staged hardening — verified progress (2026-10-30)

This is an engineering status record, not a declaration of stable release readiness.
Source base: public `v2` commit `b9962ff09782886fd8e1ab7bba4e6ee324742bc9`.
Development branch: `work/v2-sequential-hardening-20261030`.
Do not attach physical Windows/Linux disks or enable unverified native Marketplace paths.

## Stage 1 — strict automated verification foundation: COMPLETED (VM scope)

Added `.github/workflows/v2-strict-reliability.yml`. It makes all of the
following mandatory: full Python regression suite; host-compiled MiniWeb,
navigation, JFS2 and media tests; real UEFI ISO build; OVMF/QEMU graphical
boot with non-blank framebuffer evidence. No continue-on-error exception
was added. Initial green run:
https://github.com/hanefimert2016-oss/FalconOS/actions/runs/41107182031

This does NOT certify hardware mouse input, physical installation or
security of general-purpose applications.

## Stage 2 — kernel/filesystem: PARTIAL

Resolved an actual JFS2 fail-open condition for a committed transaction whose
payload checksum is wrong. A new regression in `tests/jfs_dynamic_test.c`
first failed with `JFS FAIL 81: !jfs_ready()`:
https://github.com/hanefimert2016-oss/FalconOS/actions/runs/41107547151

The correction in `kernel/jfs_dynamic.c` marks the log read-only for
corrupt committed transactions while preserving recovery of a torn,
uncommitted append. Full verification green:
https://github.com/hanefimert2016-oss/FalconOS/actions/runs/41107697344

NOT COMPLETE: general process scheduler, isolated native ELF apps and
syscall/permission ABI, general-purpose filesystem with log reclamation,
large file support, full fsck and disk power-loss certification.

## Stage 3 — actual hardware controller validation: PARTIAL (QEMU ONLY)

Added `tools/qemu_uefi_controller_e2e.py` and
`.github/workflows/v2-uefi-controller-probes.yml` to exercise actual
controller commands under isolated QEMU/OVMF with disposable virtual media.
The initial xHCI check failed with `Bx`: PCI discovered the device, but
the firmware-assigned MMIO BAR could not be reached with the bootloader's
0–4 GiB identity map.

A guest PCI trace established the exact 64-bit BAR:
`[BAR:000000C0:00000004]` = physical `0x000000C000000000`.
The test-only kernel now creates supervisor-only uncached high canonical
MMIO windows using static PML4/PDPT/2-MiB PDE structures, and exports the
**mapped virtual MMIO address** separately from the raw BAR.
This implementation is only enabled for the explicit
`FALCON_QEMU_XHCI_CMD_TEST` and `FALCON_QEMU_NVME_ID_TEST` build flags.
Production preview builds still reject inaccessible high BARs.

Acceptance test requires the *real* high BAR to be present (not a
convenient low-MEM32 fallback) and successful command-ring completion.
The same verified CI run completed both the xHCI NOOP command and
NVMe Admin Identify controller/namespace, with no namespace reads/writes:
https://github.com/hanefimert2016-oss/FalconOS/actions/runs/41114907758

All 65 Python regressions, native JFS2 checks and real QEMU framebuffer
boot remained green:
https://github.com/hanefimert2016-oss/FalconOS/actions/runs/41114907780

NEXT: a properly scoped, general PCI ioremap/MMIO allocator including
physical resource validation and explicit unmap/TLB handling; real USB
enumeration/HID/hub/hotplug and NVMe namespace read/write correctness
against only *disposable virtual disks*; MSI-X and real-hardware validation.
Controller-only QEMU probes are **not** drivers that make FalconOS safe for
installation on a real laptop.

## Stages 4–7 — NOT COMPLETED

4. Signed Marketplace packages, scoped permissions, verified native
   updating and physical-disk A/B rollback.
5. Standards-compatible Falco HTML/CSS/JavaScript, images, HTTPS and
   web security model.
6. Real independent GUI applications, unbounded/safe window lifecycle,
   complete Codedium SDK and audio/video.
7. Bare-metal compatibility, 24-hour soak, power-loss and recovery tests,
   accessibility and physical user-input certification.

Do not label any stage as complete merely because a file or screenshot
exists. Keep unverified checks red and do not merge into the stable release.

## Accepted evidence boundary

- **Verified now:** native Python/C regressions, JFS2 fail-closed red/green
  tests, real QEMU UEFI ISO framebuffer, actual **64-bit high MMIO BAR**
  xHCI command completion, and NVMe Admin Identify in isolated QEMU.
- **NOT verified:** production ioremap, NVMe storage operations, USB
  HID/hub runtime, full native HTTPS/browser, general Ring 3 application
  system, physical device support or stable-install safety.
