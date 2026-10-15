# FalconOS safe disk persistence

Older FalconOS wrote settings to LBA0 through LBA3. On an ordinary disk,
this overwrites the partition table. This release no longer does so.

- FalconOS reads MBR LBA0 but never writes to LBA0.
- Only an existing dedicated primary MBR type 0xFA partition is accepted.
- The partition must start at or after LBA2048 and hold at least 8 sectors.
- GPT, overlapping, malformed, missing, and ambiguous partitions are rejected.
- The installer defaults to Secure session (no persistent writes).
- The OS does not partition, format, or modify other partitions.
- Existing old LBA0 settings are not imported automatically.
- The settings remain unencrypted; do not treat this as disk encryption.

Run 'make run-disk' to create build/falcon-safe.raw, a new sparse 4 GiB
raw QEMU disk with a single dedicated FalconOS partition.
Select the valid partition explicitly in the FalconOS installer.

The old build/falcon.img is never used or deleted by these new targets.
'make clean' preserves your image. 'make wipe-disk' deletes only the new
build/falcon-safe.raw image, intentionally resetting the test session.

To create another throwaway QEMU disk:
  python3 tools/make_safe_disk.py --image build/other-safe.raw --size 4G

Do not use the disk image script on a physical disk or mounted filesystem.
FalconOS still lacks native NVMe, AHCI and production-grade filesystem support.

Host-side tests:
  python3 -m unittest discover -s tests -p 'test_*.py'
  gcc -std=gnu11 -Wall -Wextra -Werror -Ikernel tests/diskdb_test.c kernel/diskdb.c -o /tmp/falcon-diskdb-test
  /tmp/falcon-diskdb-test
