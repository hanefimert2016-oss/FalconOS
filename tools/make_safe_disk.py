#!/usr/bin/env python3
"""Create a new sparse raw QEMU disk with a dedicated MBR FalconOS partition.

Never opens an existing image for writing. NOT for physical block devices.
"""
import argparse
import os
from pathlib import Path
import re
import struct

PART_START = 2048
LBA28_LIMIT = 0x10000000


def parse_size(value: str) -> int:
    match = re.fullmatch(r"([1-9][0-9]*)([KMG]?)", value.strip().upper())
    if not match:
        raise ValueError("size must be e.g. 4G, 512M, or a byte count")
    number = int(match.group(1)) * (1024 ** {"": 0, "K": 1, "M": 2, "G": 3}[match.group(2)])
    sectors, rem = divmod(number, 512)
    if rem or not (PART_START + 8 <= sectors <= LBA28_LIMIT):
        raise ValueError("image must be sector aligned, larger than 1 MiB and <= 128 GiB")
    return number


def make_image(path: Path, size: int) -> None:
    if path.exists() or path.is_symlink():
        raise FileExistsError(f"refusing to overwrite existing disk: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    sectors = size // 512
    mbr = bytearray(512)
    struct.pack_into("<B3sB3sII", mbr, 446, 0, b"\0" * 3, 0xFA, b"\0" * 3,
                     PART_START, sectors - PART_START)
    mbr[510:512] = b"\x55\xAA"
    with path.open("xb") as target:
        target.truncate(size)
        target.seek(0)
        target.write(mbr)
        target.flush()
        os.fsync(target.fileno())


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument("--size", default="4G")
    args = parser.parse_args()
    if args.image.resolve().is_relative_to(Path("/dev")):
        parser.error("block-device paths are forbidden")
    size = parse_size(args.size)
    make_image(args.image, size)
    print(f"[OK] Sparse FalconOS-partitioned QEMU disk: {args.image} ({args.size})")


if __name__ == "__main__":
    main()
