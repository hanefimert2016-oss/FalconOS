# Signed Marketplace package preview (host-only, QEMU)

**Status:** opt-in security prototype. The normal FalconOS guest does not yet
enforce publisher identities independently. Do not call this a production app
store, sandbox, or end-to-end secure-update mechanism.

## Trust model

* An operator chooses a trusted Ed25519 **public** key on the host.
* The corresponding **private** key must stay in a secure host directory and
  must NEVER be placed in this Git repository, in the guest, in a workflow
  artifact, or in a published GitHub Release.
* `FAPP/1` package bytes, including app ID and version metadata, are signed
  exactly as uploaded; the signature is a raw 64-byte detached `.sig` asset.
* `SHA-256` is still checked, but only the signature + pinned public key
  establishes publisher authenticity.
* With `--trusted-publisher-key`, the host bridge rejects downloads without
  a matching detached signature **before sending the BEGIN/CHUNK protocol**.
* A signed release may be served from approved HTTPS GitHub Releases and
  static Marketplace catalogs, but any missing or altered signature fails
  closed. Normal mode remains compatible with legacy unsigned releases.

## Operator setup (do this on a trusted host; never in GitHub Actions)

Generate a publisher key once, in a secure host-only folder (examples):

```sh
mkdir -p "$HOME/.config/falconos/publisher"
chmod 700 "$HOME/.config/falconos/publisher"
openssl genpkey -algorithm Ed25519 -out "$HOME/.config/falconos/publisher/private.pem"
chmod 600 "$HOME/.config/falconos/publisher/private.pem"
openssl pkey -in "$HOME/.config/falconos/publisher/private.pem" -pubout \
  -out "$HOME/.config/falconos/publisher/public.pem"
```

Review or safely distribute the public key fingerprint through a trusted
channel. Never download the trust key from the same untrusted Release as the
application you intend to verify.

Validate locally without uploading anything:

```sh
python3 tools/publish_app.py --package path/to/hello-world.app.pkg --check-only
```

After verifying the package and authenticated `gh` identity, opt in to
publishing its package, checksum and detached signature together:

```sh
python3 tools/publish_app.py \
  --package path/to/hello-world.app.pkg \
  --signing-key "$HOME/.config/falconos/publisher/private.pem" \
  --yes
```

This command creates a **public GitHub Release** if authenticated and if
that tag does not already exist. It does not boot or control a VM. This has
not been executed against an actual public Release in CI: tests use a mocked
GitHub CLI and real temporary cryptographic key material.

In a disposable QEMU session, start the existing serial Marketplace bridge
with the trust key (on the host). Do not enable publishing unless separately
intended:

```sh
python3 tools/marketplace_bridge.py \
  --socket build/falcon-market.sock \
  --trusted-publisher-key "$HOME/.config/falconos/publisher/public.pem"
```

## Verification evidence

- Test-first red run (signature-only mode missing):
  https://github.com/hanefimert2016-oss/FalconOS/actions/runs/41116358742
- Signed-only verification tests and 70 Python regressions pass:
  https://github.com/hanefimert2016-oss/FalconOS/actions/runs/41116538092
- Test-first red run (offline signer missing):
  https://github.com/hanefimert2016-oss/FalconOS/actions/runs/41116827716
- Offline signing/Release asset tests and **73 Python regressions** pass,
  alongside real UEFI ISO build and QEMU framebuffer:
  https://github.com/hanefimert2016-oss/FalconOS/actions/runs/41117061607

## Remaining security work

Native guest cryptographic verification and publisher-key rotation/revocation;
per-app permissions; capability isolation; secure channel/session binding
between guest and host; hardware boot-chain verification; signed A/B updates,
rollback and real-device validation. No real physical disks were accessed.
