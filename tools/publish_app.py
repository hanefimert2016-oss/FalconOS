#!/usr/bin/env python3
"""Publish a local FAPP/1 .app.pkg using the authenticated host GitHub CLI.

This is a standalone Arch/Linux command: no ISO build, NASM, QEMU, serial
socket or running FalconOS instance. Published Releases are public.
"""
import argparse
import os
from pathlib import Path
import sys

from marketplace_bridge import MAX_BYTES, REPO, publish_release, validate_package


def load_package(path):
    """Read and validate the complete package before any GitHub operation."""
    if not path.is_file():
        raise ValueError("Package does not exist: " + str(path))
    with path.open("rb") as stream:
        raw = stream.read(MAX_BYTES + 1)
    if len(raw) > MAX_BYTES:
        raise ValueError("FAPP/1 .app.pkg exceeds 4096-byte limit")
    if not raw.startswith(b"FAPP/1\nid="):
        raise ValueError("Expected an FAPP/1 package beginning with id metadata")
    try:
        app_id = raw.split(b"\n", 2)[1][3:].decode("ascii")
    except UnicodeError as exc:
        raise ValueError("App ID must be ASCII") from exc
    metadata = validate_package(raw, app_id)
    return raw, metadata


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Publish an existing FAPP/1 .app.pkg to GitHub without booting FalconOS")
    parser.add_argument("--package", help="Path to an existing .app.pkg file")
    parser.add_argument("--check-only", action="store_true",
                        help="Validate locally; do not create a GitHub Release")
    parser.add_argument("--yes", action="store_true",
                        help="Confirm publishing without an interactive prompt")
    args = parser.parse_args(argv)

    package_name = args.package or os.environ.get("PKG")
    if not package_name:
        if not sys.stdin.isatty():
            parser.error("Package path required: make run-market-publish PKG=/path/app.app.pkg")
        try:
            package_name = input("Enter path to your .app.pkg file: ").strip()
        except EOFError:
            parser.error("Missing package path")
    if not package_name:
        parser.error("No package selected")

    try:
        path = Path(package_name).expanduser()
        raw, info = load_package(path)
        tag = f"app-{info['id']}-v{info['version']}"
        print(f"Validated {path} ({len(raw)} bytes, FAPP/1)")
        print(f"App: {info['name']}  ID: {info['id']}  Version: {info['version']}")
        print(f"Public GitHub destination: {REPO}  Release: {tag}")
        if args.check_only:
            print("PASS: valid FAPP/1 package. Nothing was uploaded.")
            return 0
        if not args.yes:
            if not sys.stdin.isatty():
                parser.error("Interactive confirmation required (use --yes deliberately in automation)")
            try:
                response = input("Create public GitHub Release now? Type PUBLISH: ").strip()
            except EOFError:
                response = ""
            if response != "PUBLISH":
                print("Cancelled. Nothing was published.")
                return 0
        result = publish_release(raw, info)
        print(f"Published: https://github.com/{REPO}/releases/tag/{result}")
        return 0
    except (OSError, ValueError) as exc:
        print("Publish failed: " + str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
