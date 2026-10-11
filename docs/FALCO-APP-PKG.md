# FalconOS native executable document: .app.pkg

**Target:** user downloads a supported `.app.pkg` URL into FalconOS, opens Files → Downloads, double-clicks the file. A separate native Contour WM application window opens. There is no Linux AppImage and no Chromium in this workflow.

## File format (v1)

FalconOS's first-party executable document format is `FAPP/1`, an ASCII manifest and restricted script. A `.app.pkg` filename alone does not make a file executable.

```text
FAPP/1
id=my-falcon-app
name=My Falcon App
version=1.0.0
summary=A safe FalconOS script application

clear
echo Hello from my FalconOS app
uname
```

Package limits: 4095 bytes max, ID 2–32 lowercase ASCII or hyphens, name up to 40 bytes, strict version format, ASCII commands with an explicit allowlist. Disallowed instructions cause the **entire package to be refused before any command executes**.

## Actual native workflow

1. With the Falco local TLS companion running, paste a full HTTPS link ending in `.app.pkg` into **Falco** or **Browser**. The URL must be a public hostname on HTTPS. The companion uses the Linux host's CA trust store, blocks private addresses and HTTP redirects, and passes bytes to the FalconOS guest.
2. An inert copy is written to `/home/falcon/Downloads/<filename>.app.pkg`, with a size, header, length and format check in the guest. The download never executes immediately.
3. Open **Dosyalar** → **Downloads**. Double-click the row, or select the file and press Enter.
4. FalconOS validates the full FAPP/1 manifest and every interpreter command; it runs the commands in FalconOS's existing allowlisted shell interpreter and displays the **real output in an individual native app window** with the program's own title and Close control.
5. Installed packages from the GitHub Marketplace are also copied to Downloads with a safe deterministic `.app.pkg` name after their SHA-256 verification, preserving the Marketplace installation record.

**Important limitations:** This is not native ELF/PE execution. FAPP/1 does not yet support sandboxed independent graphical processes, arbitrary dynamic libraries, binary program instructions or Windows executables. The GUI runner is a first step toward app launch UX, not a claim to general native app compatibility. Network companions are optional host services when running the guest in QEMU; physical installs still need a fully guest-native HTTPS and persistent filesystem path.

## Verification

`.github/workflows/falco-pkg-native-doubleclick.yml` tests actual GitHub release downloading and two **real PS/2 mouse clicks over VNC**, never a mock file manager. The release ISO is published to workflow artifacts only when both the double-click test and Falco direct-URL test pass.

The independent site renderer is in `kernel/falco_native_layout.c` and contains no Chromium, WebKit or JavaScript engine. It reproduces the known Contour design visually rather than claiming complete web-standards compatibility.
