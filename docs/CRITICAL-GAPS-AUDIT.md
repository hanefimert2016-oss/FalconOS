# FalconOS v2 — critical missing-features audit
Updated 2026-10-09. Experimental branch only; never use a real disk.

## Fixed or implemented in this iteration
- [x] Fixed boot-time SHFS reset by Marketplace restoration, which could erase
  files replayed by PFS1. Regression in tests/market_storage_test.c.
- [x] Real RTL8139 + ARP + IPv4 + ICMP, UDP DNS and single-client TCP with
  native HTTP in QEMU. Live non-TLS network tests passed.
- [x] BearSSL 0.6 + audited-CA trust anchor compile, domain verification,
  X.509 chain validation, RTC and fail-closed HTTPS backend; experimental ISO
  compilation passed. NOT YET live HTTPS authenticated.
- [x] Prepared HTTPS-native Marketplace catalog and two deterministic FAPP
  packages in the separate FalconOS-Marketplace repository, with SHA-256
  source-verification CI passing.
- [x] Added an opt-in native Market HTTPS catalog / package download path,
  requiring TLS + SHA-256 + FAPP allowlist checks. THIS PATH IS NOT VERIFIED
  END TO END AND MUST NOT BE ENABLED FOR REAL USERS.
- [x] Existing earlier milestone: PFS1 dual-copy 4KiB slots, FVM/1 instances.

## P0 — blocking release
1. **TLS / HTTPS**: Real QEMU TLS1.2 handshake and known-CA positive test, wrong
   SAN and untrusted CA negative tests must pass. TLS currently builds but
   actual handshake stalls following server handshake / client key exchange.
2. **Native Marketplace**: Turn direct HTTPS feature on only after TLS E2E,
   verify static catalog over TLS, verify downloaded package SHA-256, detect
   malicious IDs, versions and scripts; test reboot app launch/offline caching.
   The default COM1 host bridge remains until these conditions are met.
3. **Filesystem safety**: PFS1 max 64 slots x 4096 bytes; no dynamic allocation,
   atomic rename, complete journal/fsck, storage migration, backup or
   interruption/restart durability coverage. Never attach a physical drive.
4. **User process isolation**: FVM/1 is cooperative bytecode inside the kernel,
   not ELF processes, Ring 3, syscalls, per-process page tables or preemption.
5. **Network hardening**: TCP is single-client and stop-and-wait; incomplete
   congestion control, peer-window handling, reassembly, keepalive and
   connection concurrency. DNS uses one plain UDP resolver, lacks randomized
   source ports, DNSSEC, retries and robust multiple-answer handling.
6. **Application trust**: SHA-256 checks transmitted content, but native
   packages have no publisher signature or revoked-key policy.
7. **Integration CI flakiness**: Existing GUI HMP scripted Store navigation
   sometimes misses the launch command after desktop login; CI must reliably
   verify Market open/install/run, not merely guest boot.

## P1 — after P0
- DHCP, configurable DNS/gateway, IPv6, TCP/IP sockets, TLS 1.3 and trusted
  root refresh with verified update signatures.
- Native browser HTTP response buffering and content parsing, stream-to-disk
  for packages larger than 4KiB, safe redirect policy and download cancellation.
- File permissions and quotas, independent user homes, safe process crash
  isolation, a verified application ABI/loader and syscall rights.
- NVMe/AHCI, modern xHCI USB, HD audio and laptop GPU display acceleration;
  only then real hardware tests and installation.

## Accepted verification gate
- Both QEMU DNS/TCP/HTTP and native HTTPS positive/negative tests GREEN.
- Marketplace both direct HTTPS integration and offline+reboot integration GREEN.
- Corrupted-disk recovery, unexpected power-loss tests and stable filesystem
  migration GREEN.
- Permission-isolation and malformed-code fuzzer suites GREEN.

Production-ready status: **NO**. Successful ISO compilation != safe operating
system or functioning authenticated HTTPS.
