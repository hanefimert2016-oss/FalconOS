# FalconOS native app.pkg support (no Chromium, no AppImage)

FalconOS ships its own `.app.pkg` application file type. This file lives
INSIDE the FalconOS guest OS (NOT Linux/Windows/macOS) and is associated with
its real built-in Files app.

## Opening a downloaded app, without installing extra software

1. Open **Falco** from FalconOS Launchpad.
2. Paste a real HTTPS URL ending in `.app.pkg` into its search/address box.
   FalconOS requests the exact package bytes using its CA-verified host HTTPS
   companion if available. The file downloads into **Files → Downloads**.
   Downloading NEVER executes the program.
3. Open **Files → Downloads**. **Double-click** the downloaded file. The
   first click selects it, the second click runs the verified FAPP/1 program
   in a dedicated FalconOS application window. Enter also launches the
   currently selected valid package.

Example published file:
https://github.com/hanefimert2016-oss/FalconOS-Marketplace/releases/download/app-hello-world-v1.0.0/hello-world-v1.0.0.app.pkg

Alternatively **Discover/Store** downloads a SHA-256-checked Marketplace
release and places the app in Downloads. **Codedium → F7 Export** creates
`/home/falcon/Desktop/code.app.pkg`, which opens with the same double-click
association.

## Security and scope

The current `.app.pkg` format is **FAPP/1**, a bounded, ASCII, metadata +
allowlisted shell instruction package. Before any instruction runs, the guest
checks the complete file (format, ID, version, name, size, commands and
forbidden operators). Unsupported/binary/unsafe files do **not** run. The
native app window displays program output; FAPP/1 has no arbitrary native
machine code or complete custom widgets yet. It is FalconOS's executable
document format, **not** a renamed Windows PE .exe, a Linux ELF, or
a native binary equivalent. A future native executable ABI would require
Ring3 isolation, executable loader, permissions and signature handling.

HTTPS is verified by a trusted companion when FalconOS is running in a
supported VM. The guest transport is local plain TCP, not end-to-end TLS.
For packages downloaded through Discover the existing SHA-256 workflow
validates the published release, but untrusted internet FAPP files can only
use the limited allowlisted commands. This is not a proof of human identity
to Cloudflare; no anti-bot evasion is implemented.

## Falco native website rendering

This branch has no Chromium, WebKit or Playwright runtime. The Falco site
preview uses independently written native framebuffer painting, manually
faithful to the real public Contour CSS color/layout design tokens and
original Manrope typeface glyphs baked at compile time. F9 opens the native
Contour visual; keys 1–4 navigate its sections. The real site content
must be verified before the preview is shown. It remains a **site-specific
CSS subset**, not a standards-compliant general browser: arbitrary CSS,
images, JS DOM, forms, Cloudflare challenges and full page behavior
are not yet faithfully rendered. We never claim pixel-identical HTML5.
In particular it is not a Chromium screenshot stream.

## Setup, lockscreen, personalization

- No password created on the default account → automatic first-boot login
  into the desktop. A protected account must still supply its password.
- Creating a second user defaults to **No**.
- Four selectable native wallpaper themes (Aurora, Midnight, Violet, Sunset)
  are persisted on an explicitly selected safe disk. The launcher-stamped
  test virtual disk is auto-selected, but a physical disk is never
  auto-formatted.

## Test evidence

The GitHub workflow `.github/workflows/fapp-doubleclick.yml` launches
actual QEMU and sends RFB keyboard and PS/2 mouse events. It exports a real
Codedium package, navigates Files, precisely double-clicks the file,
and asserts that its own FalconOS application window opens.

The dedicated `falco-app-download.yml` workflow verifies the entire
internet download → Files → double-click → application window path with
the actual public GitHub release.

The native OS workflow verifies wallpaper persistence and password rules
over real QEMU boots. The GitHub artifact is a bootable FalconOS ISO plus
individual PNG evidence; **no AppImage is built or published**.
