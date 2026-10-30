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

### Cooperative Ring 3 ELF task hardening (QEMU only)

Added `kernel/ring3_caps.c`, `tests/ring3_capabilities_test.c` and a
mandatory host-compiled capability policy check. Only a kernel-granted
bitmask permits diagnostic output or clock ticks in scheduled ELF tasks;
exit, getpid and yield remain available, and unknown syscall numbers
retain ENOSYS. Untrusted ELF metadata cannot grant capabilities.

A new test-only UEFI/CPL3 workflow runs **two genuine ELF64 tasks** with
different capabilities and checks four yield/resume/exit slices, exactly
one permitted diagnostic syscall and two denied syscalls. It exposed a
real assembly bug: the INT 0x80 handler confused normal syscall return
values of 1 or 2 (getpid and clock) with exit/yield instructions.
`boot/isr.asm` now uses the *original syscall number* to choose these
actions. Red failing run:
https://github.com/hanefimert2016-oss/FalconOS/actions/runs/41118808982
Green real-CPL3 run:
https://github.com/hanefimert2016-oss/FalconOS/actions/runs/41119057949

An additional test required ended tasks' kernel-owned code, stack,
saved register frames and capability masks to be zeroed. It initially
failed after normal task completion:
https://github.com/hanefimert2016-oss/FalconOS/actions/runs/41119282249
`ring3_discard_task()` now clears these snapshots on normal exit,
failure and scheduler budget exhaustion. Green:
https://github.com/hanefimert2016-oss/FalconOS/actions/runs/41119572004

Finally, a separate UEFI build verifies the original two-task ELF
cooperative scheduler still yields, resumes and exits, after a new
kernel-only `K[` diagnostic delimiter prevented firmware-text false
positives. Both real CPL3 tests green:
https://github.com/hanefimert2016-oss/FalconOS/actions/runs/41119876267

Latest mandatory native/Python/UEFI regressions and separate QEMU
controller probes also green:
https://github.com/hanefimert2016-oss/FalconOS/actions/runs/41119876387
https://github.com/hanefimert2016-oss/FalconOS/actions/runs/41119876480

NOT COMPLETE: **per-task CR3 address spaces**, preemptive scheduling,
general ELF apps, full syscall/permissions ABI, native user-mode app
loader and process lifecycle; storage log reclamation, larger files,
full fsck and disk power-loss certification. Only kernel-owned task
snapshots are zeroized; this does not imply all freed physical pages
are scrubbed or that FalconOS can run arbitrary third-party binaries.

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

## Stage 4 — Marketplace trust and updater: PARTIAL

The host-assisted QEMU Marketplace fetch path now uses an explicit
`AssetRedirectHandler` in `tools/marketplace_bridge.py`. Valid HTTPS
GitHub-owned asset/CDN locations remain usable; redirects to plaintext,
arbitrary domains, private IPs, credential-bearing destinations and custom
ports are rejected before the HTTP client follows them. The handler also
limits redirect chains to four.

Tests were added before the change and first failed due to the missing
redirect guard:
https://github.com/hanefimert2016-oss/FalconOS/actions/runs/41115374304

The implementation then passed both new security cases and all 67 Python
tests, native JFS2 and media checks, and real QEMU boot:
https://github.com/hanefimert2016-oss/FalconOS/actions/runs/41115531091

A second test-first milestone added host-pinned **Ed25519 detached
signatures** and an explicitly opt-in signed-only download mode. The host
checks a 64-byte .sig against the operator's trusted public PEM **before**
sending a package to the guest; unsigned and tampered packages are refused.
An offline host signer produces the matching .sig alongside a published
Release when the operator explicitly selects `--signing-key`.
No private keys are committed or generated in the repo.

- Red verification tests: 
  https://github.com/hanefimert2016-oss/FalconOS/actions/runs/41116358742
- Red host signing tests:
  https://github.com/hanefimert2016-oss/FalconOS/actions/runs/41116827716
- Verified green: **73/73 Python tests**, real OpenSSL Ed25519 signing and
  verification, mocked GitHub Release upload and real UEFI/QEMU boot:
  https://github.com/hanefimert2016-oss/FalconOS/actions/runs/41117061607
- Operator guide: `docs/MARKETPLACE-ED25519-PREVIEW.md`.

NOT COMPLETE: guest-native publisher signature enforcement, key rotation and
revocation, signed-only mode as the safe default, actual production Release
publication tests, permission grants, secure guest/host session binding,
and signed A/B physical-disk updates. SHA-256 alone does not prove identity.

## Stages 5–7 — NOT COMPLETED

5. Standards-compatible Falco HTML/CSS/JavaScript, images, HTTPS and
   web security model.
6. Real independent GUI applications, unbounded/safe window lifecycle,
   complete Codedium SDK and audio/video.
7. Bare-metal compatibility, 24-hour soak, power-loss and recovery tests,
   accessibility and physical user-input certification.

Do not label any stage as complete merely because a file or screenshot
exists. Keep unverified checks red and do not merge into the stable release.

## Accepted evidence boundary

- **Verified now:** 73 Python regressions, native C checks, JFS2
  fail-closed red/green tests, real QEMU UEFI ISO framebuffer, actual
  **64-bit high MMIO BAR** xHCI command completion, NVMe Admin Identify,
  HTTPS Marketplace asset redirect restrictions, and **opt-in host-side**
  pinned Ed25519 verification and offline Release-signing test coverage.
- **NOT verified:** production ioremap, NVMe storage operations, USB
  HID/hub runtime, full native HTTPS/browser, general Ring 3 application
  system, physical device support or stable-install safety.
