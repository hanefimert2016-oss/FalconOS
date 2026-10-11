# FalconOS .app.pkg — native executable documents

## What the user gets

FalconOS calls **.app.pkg** its own executable document type.

1. Open **Falco** or **Browser**, enter an HTTPS URL ending in `.app.pkg`.
2. The site’s TLS certificate and all redirect destinations are checked by
   the optional local VM host HTTPS companion (port 18444). No HTTP fallback.
   The data is downloaded but **never auto-executed**.
3. Open **Files → Downloads**. The file has the `A` application marker.
4. Double-click the file (two actual PS/2 clicks), or select and hit Enter.
5. FalconOS validates `FAPP/1` metadata and the entire body against its
   allowlisted commands **before** executing it. Valid scripts run in the
   FalconOS Terminal; invalid files are rejected.

Discover/Marketplace also downloads real GitHub Release `.app.pkg` assets,
checks the publisher-provided SHA-256 against the bytes fetched, and creates a
copy in Downloads. Files can open that copy without contacting GitHub again.

## Native format v1

```text
FAPP/1
id=hello-world
name=Hello World
version=1.0.0
summary=FalconOS native hello-world

# authorized application script
echo Hello FalconOS
uname
```

- **Filename:** `*.app.pkg`. Double-click association runs **inside**
  FalconOS, never on the Linux/Windows host.
- **Body:** FalconOS shell-command subset, max 4095 bytes, printable ASCII,
  bounded line length. The extension alone never grants execution.
- **Security:** a locally downloaded URL has a CA-checked HTTPS transport but
  is **not independently signed by the publisher**; verify the source first.
  Marketplace additionally authenticates its release digest. A downloaded
  file cannot directly execute arbitrary x86_64 ELF, Windows PE, or Linux
  executables. It cannot write privileged host files.
- **Current limitation:** FAPP/1 programs are interpreted terminal scripts,
  not native GUI programs or ABI-compatible Windows .exe. An isolated
  graphical application API and process sandbox are still in development.
- **Offline:** existing Marketplace packages persist on the safe 0xFA
  FalconFS partition and are replayed in Downloads when installed packages
  are restored. Browser downloads outside Marketplace are currently RAM
  downloads and will need file persistence before claiming durability across
  reboots.

## Build & tests

```sh
python3 -m venv .venv
.venv/bin/pip install pillow vncdotool
PATH="$PWD/.venv/bin:$PATH" make iso ENABLE_BEARSSL=1 ENABLE_BOOT_GLB=0   EXTRA_CFLAGS='-DFALCON_QEMU_BROWSER_TEST -DFALCON_QEMU_UI_GALLERY'
.venv/bin/python tools/verify_falco_pkg_download.py
.venv/bin/python tools/vnc_fapp_pkg_doubleclick_e2e.py
.venv/bin/python tools/vnc_falco_direct_pkg_e2e.py
```

These tests use real QEMU and PS/2 inputs, fetch actual public GitHub Release
bytes, verify digests, click the Files row twice and inspect the genuine guest
Terminal. Testing separate input/keyboard layouts is important: a US RFB
keyboard sending `/` to a guest configured as Turkish Q creates different
physical scancodes. Choose matching layouts for reliable URL entry.

The **Falco visual page** is unrelated to package execution. Its current
non-Chromium custom Contour renderer has a subset of layout primitives and is
not a standards-complete general CSS/JavaScript browser.
