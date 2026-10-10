# FalconOS Marketplace and Codedium integration

## What works
- GitHub Marketplace packages are actually hosted as versioned Releases.
- In QEMU, the native Store requests the current app catalog from a small
  opt-in Python bridge connected to COM1. The bridge downloads real
  GitHub Release assets via host HTTPS and passes bounded chunks over serial.
- The guest calculates SHA-256, checks package syntax and a restricted
  command allowlist, then installs and launches the script via Terminal.
- When a dedicated, correctly partitioned FalconOS test disk is selected,
  up to 48 apps persist in checksummed fixed-size partition slots.
- Codedium can edit scripts, save to RAM, run allowlisted commands and
  export a .app.pkg in its RAM Desktop folder. The web Codedium Studio in
  the Marketplace repo can export a host-downloadable package and submit
  a source PR for publishing.

## Run on Arch Linux
Install QEMU, GCC, NASM, GRUB and xorriso, then on this branch:

    make run-market

Or start the guest with `make run-disk` and run this in another terminal:

    make market-bridge

The first launch creates `build/falcon-safe.raw`, a 4 GiB sparse image.
Choose that FalconOS partition during setup for app persistence, and do
not attach your actual Windows/Linux SSD to these experimental builds.
In the guest open **Store** and press **R**. Choose a release with the
arrow keys, press Enter to GET, then press Enter again to RUN.
Press **U** to download a newer release; **D** removes an installed app. Press **C** for Codedium.
Codedium: F5 save, F6 run, F7 create a local .app.pkg in guest RAM.

## Important limitations
The guest does not yet have native TCP/TLS. The HTTPS bridge is opt-in
and works only in a QEMU-style environment with the configured serial
socket. Bare-metal internet downloading is not supported.
The guest storage format is not a general-purpose file system, and
48 slots x 4 KiB is only an MVP. Executable ELF application packages
are not supported. Apps can currently run only restricted built-in
shell commands. No third-party native code is run in ring 3.
Do not treat a successful ISO compile as proof of QEMU desktop usability.

## Legacy prg compatibility
The old compile-time prg catalog is retained as internal metadata, but
unavailable non-built-in entries are no longer permitted to claim successful
installation. The graphical Store is exclusively Release-backed. Package
publication means reviewed FAPP/1 sources followed by an immutable Release.

## Native Codedium custom app identity
Create or edit a project in Codedium. At the top of `project.fsh` add
these exact ASCII source comments (edit them for each application):

    # app-id: my-app
    # app-name: My App
    # app-version: 1.0.0
    # app-summary: A small and safe FalconOS script
    clear
    echo Hello from my app

Press F5 to save, F6 to run and F7 to package into
`/home/falcon/Desktop/code.app.pkg`. Packages are limited to 4096 bytes
and only the allowlisted built-in commands can execute. This is not an
ELF/native binary compiler. For publishing, move the package to a host,
import it into the web Codedium source editor, fork the Marketplace repo,
and submit a reviewed PR. Device-to-host file transfer is not yet built in.

## Tests
GitHub Actions builds the ISO, executes native mocked-storage and parser tests,
boots QEMU, and runs a **gating** GUI + COM1 integration test that simulates a
GitHub Release from a local fixture and verifies guest catalog, installation,
and Terminal launch. This does NOT verify guest-native TCP/HTTPS or an actual
GitHub download. Do not pass real physical disks to QEMU.
