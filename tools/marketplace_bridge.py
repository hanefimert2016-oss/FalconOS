#!/usr/bin/env python3
"""Opt-in QEMU UART <> GitHub Releases bridge for FalconOS FAPP/1.

Uses the host HTTPS stack because the guest has no TCP/TLS driver yet.
Only lists and downloads packages from a pinned public repository.
Never runs downloaded code on the host. Does not need GitHub credentials.
"""
import argparse
import hashlib
import json
import re
import socket
import time
import urllib.request
from pathlib import Path

API = "https://api.github.com/repos/hanefimert2016-oss/FalconOS-Marketplace/releases?per_page=100"
ALLOWED = {"echo", "date", "uname", "uptime", "whoami", "pwd", "ls", "help", "cal", "hwinfo", "free", "df", "clear"}
MAX_BYTES = 4096
APP_ID = re.compile(r"[a-z][a-z0-9-]{1,31}\Z")
NAME = re.compile(r"[A-Za-z0-9 .,:!?+/_()\-]{2,75}\Z")
VERSION = re.compile(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[a-z0-9.-]+)?\Z")

def request_json(url):
    req = urllib.request.Request(url, headers={"User-Agent": "FalconOS-Bridge/1.0", "Accept": "application/vnd.github+json"})
    with urllib.request.urlopen(req, timeout=15) as response:
        return json.load(response)

def request_bytes(url):
    if not (url.startswith("https://github.com/hanefimert2016-oss/FalconOS-Marketplace/releases/download/")
            or url.startswith("https://api.github.com/repos/hanefimert2016-oss/FalconOS-Marketplace/releases/assets/")):
        raise ValueError("untrusted asset URL")
    req = urllib.request.Request(url, headers={"User-Agent": "FalconOS-Bridge/1.0"})
    with urllib.request.urlopen(req, timeout=25) as response:
        raw = response.read(MAX_BYTES + 1)
    if len(raw) > MAX_BYTES:
        raise ValueError("package larger than 4096 bytes")
    return raw

def validate_package(raw, app_id):
    if len(raw) > MAX_BYTES or b"\x00" in raw or not raw.isascii():
        raise ValueError("invalid or oversized package")
    text = raw.decode("ascii")
    if not text.startswith("FAPP/1\n") or "\r" in text:
        raise ValueError("invalid FAPP/1 header")
    meta, sep, script = text.partition("\n\n")
    if not sep:
        raise ValueError("missing code")
    lines = meta.splitlines()
    if len(lines) != 5:
        raise ValueError("invalid metadata")
    vals = {}
    for line in lines[1:]:
        key, eq, value = line.partition("=")
        if not eq or key in vals or key not in ("id", "name", "version", "summary"):
            raise ValueError("invalid manifest")
        vals[key] = value
    if set(vals) != {"id", "name", "version", "summary"} or vals["id"] != app_id:
        raise ValueError("wrong app")
    if not APP_ID.fullmatch(vals["id"]) or not VERSION.fullmatch(vals["version"]):
        raise ValueError("invalid id/version")
    if not NAME.fullmatch(vals["name"]) or not NAME.fullmatch(vals["summary"]):
        raise ValueError("invalid name/summary")
    if not script.endswith("\n"):
        raise ValueError("script must end with newline")
    for number, line in enumerate(script.splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        if len(line) > 180 or line.split(maxsplit=1)[0] not in ALLOWED:
            raise ValueError(f"invalid command on line {number}")
        if any(x in line for x in (";", "&&", "||", "|", ">", "<", "$", chr(96), "\\", "&")):
            raise ValueError(f"prohibited script operator on line {number}")
    return vals

def semver_key(version):
    """Return SemVer-compatible ordering, rejecting unsupported huge versions."""
    m = re.fullmatch(r"([0-9]+)\.([0-9]+)\.([0-9]+)(?:-([a-z0-9.-]+))?", version)
    if not m or len(version) > 24:
        raise ValueError("invalid or oversized release version")
    core = []
    for part in m.groups()[:3]:
        if (len(part) > 1 and part.startswith("0")) or int(part) > 0xFFFFFFFF:
            raise ValueError("invalid numeric component")
        core.append(int(part))
    suffix = m.group(4)
    if suffix is None:
        return (*core, 1, ())
    fields = suffix.split(".")
    if any(not f for f in fields):
        raise ValueError("empty prerelease identifier")
    identifiers = []
    for f in fields:
        if f.isdigit():
            if len(f) > 1 and f[0] == "0":
                raise ValueError("prerelease numeric leading zero")
            identifiers.append((0, int(f)))
        else:
            identifiers.append((1, f))
    return (*core, 0, tuple(identifiers))


def releases():
    result = {}
    for page in range(1, 6):
        batch = request_json(API + f"&page={page}")
        if not batch:
            break
        for release in batch:
            if release.get("draft") or release.get("prerelease"):
                continue
            for a in release.get("assets", []):
                filename = a.get("name", "")
                m = re.fullmatch(r"([a-z][a-z0-9-]{1,31})-v([0-9]+\.[0-9]+\.[0-9]+(?:-[a-z0-9.-]+)?)\.app\.pkg", filename)
                if not m or a.get("size", MAX_BYTES + 1) > MAX_BYTES:
                    continue
                try:
                    key = semver_key(m.group(2))
                except ValueError:
                    continue
                existing = result.get(m.group(1))
                if existing and key <= semver_key(existing["version"]):
                    continue
                if release.get("tag_name") != f"app-{m.group(1)}-v{m.group(2)}":
                    continue
                sha_asset = next((x for x in release.get("assets", [])
                                  if x.get("name") == filename + ".sha256"), None)
                if not sha_asset:
                    continue
                result[m.group(1)] = {"asset": a, "checksum_asset": sha_asset,
                                       "version": m.group(2), "release": release}
    return dict(sorted(result.items())[:48])

def send(sock, message):
    wire = (message + "\n").encode("ascii")
    for byte in wire:
        sock.sendall(bytes((byte,)))
        time.sleep(0.004)  # 16550 RX FIFO = 16 bytes; guest polls ~50 FPS

def read_line(sock):
    data = bytearray()
    while True:
        chunk = sock.recv(1)
        if not chunk:
            raise ConnectionError("guest disconnected")
        if chunk == b"\n":
            return data.decode("ascii", errors="replace").strip("\r")
        if len(data) >= 160:
            raise ValueError("command too long")
        data.extend(chunk)

def send_acknowledged(sock, line):
    send(sock, line)
    sock.settimeout(12)
    try:
        answer = read_line(sock)
    finally:
        sock.settimeout(None)  # idle guest may stay open indefinitely
    if answer != "ACK":
        raise ValueError("guest rejected packet: " + answer[:100])

def serve(sock):
    cache = {}
    while True:
        command = read_line(sock)
        try:
            if command == "LIST":
                cache = releases()
                for app_id, entry in list(cache.items())[:48]:
                    asset = entry["asset"]
                    name = entry["release"].get("name", app_id)
                    name = re.sub(r"[^A-Za-z0-9 .,_-]", "", name)[:38] or app_id
                    send_acknowledged(sock, "CAT|" + app_id + "|" + entry["version"] + "|" + name)
                send(sock, "DONE")
            elif command.startswith("GET|"):
                app_id = command[4:]
                if not APP_ID.fullmatch(app_id):
                    raise ValueError("invalid requested app")
                if app_id not in cache:
                    cache = releases()
                entry = cache.get(app_id)
                if not entry:
                    raise ValueError("release not found")
                raw = request_bytes(entry["asset"]["browser_download_url"])
                info = validate_package(raw, app_id)
                if info["version"] != entry["version"]:
                    raise ValueError("manifest version mismatch")
                digest = hashlib.sha256(raw).hexdigest()
                checksum = request_bytes(entry["checksum_asset"]["browser_download_url"])
                published = checksum.decode("ascii").split()[0]
                if not re.fullmatch(r"[0-9a-f]{64}", published) or published != digest:
                    raise ValueError("Published Release checksum does not match package")
                # SHA-256 is independently checked again by the guest.
                send_acknowledged(sock, "BEGIN|" + app_id + "|" + str(len(raw)) + "|" + digest)
                for start in range(0, len(raw), 16):
                    send_acknowledged(sock, "CHUNK|" + raw[start:start + 16].hex())
                send_acknowledged(sock, "END")
            elif command == "ACK":
                continue
            else:
                send(sock, "ERR|unknown command")
        except (OSError, TimeoutError, ValueError, KeyError, json.JSONDecodeError) as exc:
            print("Bridge error:", exc, flush=True)
            send(sock, "ERR|download failed")

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--socket", default="build/falcon-market.sock")
    args = p.parse_args()
    path = Path(args.socket)
    print("Connecting to QEMU serial socket:", path, flush=True)
    while not path.exists():
        time.sleep(0.2)
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.connect(str(path))
        serve(sock)

if __name__ == "__main__":
    main()
