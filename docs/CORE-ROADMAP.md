# FalconOS Core Infrastructure — experimental milestone
Date: 2026-10-09. Branch: feature/core-storage-net-usermode.
**Use QEMU with a new throwaway disk. Never attach a physical SSD.**

## Priorities (in order)
1. Durable application / user-file storage.
2. Real guest-native networking, never fabricated ping or leases.
3. Independent application processes.
4. Hardware drivers only after the first three are production-ready.

## Implemented in this experimental branch

### PFS1 durable SHFS prototype
- Opt-in existing MBR type-0xFA partition; never writes to disk LBA0.
- Dedicated partition-relative sector interval 1024–2303.
- 64 logical entries, dual copy-on-write records (10 sectors/copy).
- Each file max 4096 bytes; SHA-256, generation, tombstone deletion,
  fallback to previous copy after torn/corrupted write.
- Dirty file entries flushed incrementally from GUI event loop.
- Existing Marketplace cache and CodeDium project offsets are preserved.
- Native mocked-disk regression covers persistence, max size, corruption,
  deletion and RAM-only mode.

### Actual native QEMU NIC
- RTL8139 PCI I/O/DMA RX/TX, ARP, IPv4, ICMP.
- Static QEMU user networking: 10.0.2.15/24, gateway 10.0.2.2.
- Terminal ping and ARP report actual protocol results, not fake success.
- Wire-format tests and separate genuine QEMU gateway echo test.

### Native DNS, TCP, HTTP, experimental HTTPS
- UDP DNS queries sent by the FalconOS RTL8139 driver to the QEMU
  resolver (10.0.2.3); DNS A responses parsed with bounds and transaction
  checks. QEMU DNS E2E has passed.
- TCP single-client stack: SYN/SYN-ACK/ACK handshake, checksums, sequence
  checking, ordered data, retransmissions, FIN/RST and bounded timeouts.
- Guest-native HTTP/1.0 GET is verified against an actual QEMU host server.
  Commands: dns example.com, ping 10.0.2.2, http example.com /.
- HTTPS is implemented as an **opt-in BearSSL 0.6 integration**:
  pinned-SHA source bootstrap, build-time PEM trust anchors, hostname and
  certificate-chain validation, UTC RTC validation and mandatory CPU RDRAND.
  Commands: https example.com / (only TLS-enabled ISO).
- The TLS-enabled ISO **compiles successfully**; TLS live QEMU handshake
  remains **UNVERIFIED / FAILING** at this milestone. Do NOT claim working
  native HTTPS downloads or enable Marketplace downloads from this path.
- Enable test-only TLS build with make iso ENABLE_BEARSSL=1; set
  TLS_CA_BUNDLE=/path/to/explicit/audited/roots.pem. No plain HTTP
  downgrade is permitted when TLS fails.

### Independent virtual applications: FVM/1
- Four cooperatively scheduled virtual machines, each with its own
  stack, globals, PC, instruction count and sleeping state.
- 16 instructions/frame/instance, 50,000-instruction lifetime limit.
- PUSH, ADD, SUB, MUL, DUP, DROP, LOAD, STORE, PRINT, SLEEP, JMP, JZ, HALT.
- Included sample /home/falcon/demo.fvm, Terminal commands:
  vm start /home/falcon/demo.fvm
  vm list
  vm kill 0
- FVM/1 is a kernel-hosted software virtual machine, NOT ELF or Ring 3.

## NOT COMPLETED — do not present as shipping
- PFS1 is not a complete journaled, dynamic, general-purpose filesystem;
  no multimegabyte files, fsck, encryption or power-loss certification.
- Networking still lacks DHCP, DNSSEC, production-grade TCP multi-session
  handling, browser support and a **QEMU-E2E-verified native HTTPS client**.
  Marketplace still uses host COM1 bridge; the experimental BearSSL-backed
  HTTPS client must not replace it until the positive/negative TLS tests pass.
- User-mode ELF, true isolated address spaces, Ring 3, hardware-enforced
  memory protection, syscalls and preemptive process scheduler not implemented.
- Production NVMe, USB xHCI and GPU acceleration deferred.
- Tests in QEMU do not justify safe use on a real user's disk.

## Reproduce
On Arch Linux, install GCC, NASM, GRUB, xorriso, mtools, Python and QEMU.
Then clone the above experimental branch and run:

    make run-market RAM=4096 CPUS=2

Opt into the dedicated disposable QEMU disk in the installer.
DO NOT pass a device path (/dev/nvme*, /dev/sd*) to experimental QEMU.

## Conditions for actual completion before hardware phase
1. Filesystem block allocator + journal/replay + fsck, variable file lengths,
   many reboot, interrupted-write and app-update/remove integrity tests.
2. Native ARP/DHCP/DNS/TCP/TLS and HTTPS, end-to-end verified downloads in guest,
   certificate verification and positive/negative network CI.
3. Native ELF loader, Ring-3 kernel/user page isolation, syscall ABI,
   timer-driven scheduling, crash isolation and permission model.
4. Only afterwards NVMe/AHCI, USB xHCI, graphics acceleration.
