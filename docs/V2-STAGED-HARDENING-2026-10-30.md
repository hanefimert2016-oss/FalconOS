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

## Stage 3 — actual hardware controller validation: BLOCKED

Added `tools/qemu_uefi_controller_e2e.py` and
`.github/workflows/v2-uefi-controller-probes.yml` to test real controller
instructions against a disposable QEMU/OVMF VM, never against a device path.
The xHCI probe obtains an actual failure marker from the guest:
https://github.com/hanefimert2016-oss/FalconOS/actions/runs/41108152690

A two-stage diagnostic narrowed the cause to the xHCI device being present
but `pci_extended_mmio(2)` not accepting its MMIO mapping. Trace is `Bx`,
not a successful `X` command completion:
https://github.com/hanefimert2016-oss/FalconOS/actions/runs/41108758001

NEXT: inspect full 64-bit BAR placement and memory map; implement an
actual checked ioremap/MMIO mapping mechanism and verify it independently
under UEFI before enabling bus mastering. Only then retry xHCI command
and NVMe Identify tests. Passing controller smoke does not equal a USB
HID/MSC driver or safe physical laptop use.

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

- **Verified now:** native regression checks and real QEMU boot, JFS2
  fail-closed regression red/green, xHCI PCI/MMIO failure diagnosis.
- **Not verified now:** a complete xHCI HID path, full native HTTPS
  browser, general Ring 3 application system, physical drivers or
  stable-install safety.
