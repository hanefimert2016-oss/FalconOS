#!/usr/bin/env python3
"""QEMU serial Marketplace bridge. Downloads need no authentication.

Publishing is DISABLED unless --enable-publish is set on the host.
The guest never sees GitHub credentials. Explicit guest confirmation is
also required, and only a valid FAPP/1 package can become a GitHub release.
No downloaded scripts ever execute on the host.
"""
import argparse
import hashlib
import json
import re
import socket
import time
import urllib.request
import subprocess
import tempfile
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


REPO = "hanefimert2016-oss/FalconOS-Marketplace"

def publish_release(raw, metadata):
    """Create exactly one immutable GitHub Release via authenticated host gh.

    No guest-provided command/URL/repository reaches subprocess arguments.
    gh auth login (or scoped GH_TOKEN) is required outside the guest.
    """
    validate_package(raw, metadata["id"])
    tag = f'app-{metadata["id"]}-v{metadata["version"]}'
    asset = f'{metadata["id"]}-v{metadata["version"]}.app.pkg'
    status = subprocess.run(["gh", "auth", "status"], timeout=20,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if status.returncode:
        raise ValueError("Host GitHub CLI not authenticated; run gh auth login")
    existing = subprocess.run(["gh", "release", "view", tag, "--repo", REPO],
                              capture_output=True, text=True, timeout=25)
    if existing.returncode == 0:
        raise ValueError("Version already exists in GitHub Releases; increment app-version")
    with tempfile.TemporaryDirectory(prefix="falcon-publish-") as directory:
        pkg = Path(directory) / asset
        pkg.write_bytes(raw)
        sidecar = Path(directory) / (asset + ".sha256")
        sidecar.write_text(hashlib.sha256(raw).hexdigest() + "  " + asset + "\n",
                           encoding="ascii")
        cmd = ["gh", "release", "create", tag, str(pkg), str(sidecar),
               "--repo", REPO, "--title", metadata["name"],
               "--notes", "FAPP/1 script published from FalconOS CodeDium/Discover.",
               "--latest=false"]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=70)
        if result.returncode:
            raise ValueError("GitHub release failed: " + result.stderr[:250])
    return tag

def publish_transfer_complete(upload, publisher):
    if upload is None:
        raise ValueError("Upload END without BEGIN")
    raw = bytes(upload["data"])
    if len(raw) != upload["size"] or hashlib.sha256(raw).hexdigest() != upload["sha"]:
        raise ValueError("Upload length or SHA-256 mismatch")
    info = validate_package(raw, upload["id"])
    if info["version"] != upload["version"]:
        raise ValueError("Version mismatch")
    if publisher is None:
        raise ValueError("Host publishing disabled: restart bridge with --enable-publish")
    return publisher(raw, info)

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

def serve(sock, publisher=None):
    cache = {}
    upload = None
    while True:
        command = read_line(sock)
        try:
            if command.startswith("UP|"):
                fields = command.split("|")
                if len(fields) != 5 or not APP_ID.fullmatch(fields[1]) or \
                    not VERSION.fullmatch(fields[2]) or not fields[3].isdigit() or \
                    not (70 <= int(fields[3]) <= MAX_BYTES) or \
                    not re.fullmatch(r"[0-9a-f]{64}", fields[4]):
                    raise ValueError("Invalid upload announcement")
                upload = {"id": fields[1], "version": fields[2],
                          "size": int(fields[3]), "sha": fields[4],
                          "data": bytearray()}
            elif command.startswith("DAT|"):
                if upload is None:
                    raise ValueError("Data without an upload")
                encoded = command[4:]
                if not (2 <= len(encoded) <= 32 and len(encoded) % 2 == 0) or \
                    not re.fullmatch(r"[0-9a-f]+", encoded):
                    raise ValueError("Invalid upload data")
                data = bytes.fromhex(encoded)
                if len(upload["data"]) + len(data) > upload["size"]:
                    raise ValueError("Upload overrun")
                upload["data"].extend(data)
            elif command == "UPEND":
                finished = upload
                upload = None
                tag = publish_transfer_complete(finished, publisher)
                send(sock, "PUBOK|" + tag)
            elif command == "LIST":
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
            if command.startswith(("UP|", "DAT|")) or command == "UPEND":
                upload = None
                send(sock, "PUBERR|upload-rejected")
            else:
                send(sock, "ERR|download failed")

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--socket", default="build/falcon-market.sock")
    p.add_argument("--enable-publish", action="store_true",
                   help="Explicitly authorize authenticated GitHub Release uploads from this VM")
    args = p.parse_args()
    path = Path(args.socket)
    print("Connecting to QEMU serial socket:", path, flush=True)
    while not path.exists():
        time.sleep(0.2)
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.connect(str(path))
        if args.enable_publish:
            print("WARNING: GitHub publishing enabled: guest-confirmed FAPP/1 releases only.",
                  flush=True)
        serve(sock, publisher=publish_release if args.enable_publish else None)

if __name__ == "__main__":
    main()
