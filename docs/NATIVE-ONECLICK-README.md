# FalconOS Native One-Click Edition (Linux)

**No Chromium, Playwright, WebKit, Selenium, or browser screenshot streaming.**

## What is actually implemented

- **Falco NativeLayout/1**: own C renderer draws DOM-like semantic elements in the 2560×1440 framebuffer using FalconOS's pixel/font primitives. No 320×180/640×360 upscaling. F9 opens it; arrow keys scroll; F5 reloads; F3 goes back.
- The optional host Python-stdlib gateway validates the public site's HTTPS certificate and hostname, then sends a **strict, bounded FALCO/1 semantic document** to the guest. No images or untrusted scripts execute. Actual CSS parsing/cascade, JavaScript DOM changes, forms and pixel-perfect third-party website rendering **are not implemented**. The layout is FalconOS's own visual interpretation of real HTML, not a screenshot or fabricated mock-up.
- **Wallpaper Settings**: choose among Aurora, Midnight Blue, Violet and Sunset. Procedural wallpapers are drawn at full native resolution. Your choice is saved on a valid FalconFS 0xFA virtual partition and older account/password storage can be imported.
- **Lock screen**: new wallpaper-backed interface. A default account intentionally created *without* a password auto-signs in on first boot. Password-protected accounts still require a password; pressing "Lock screen now" still locks even a passwordless account and asks for Enter.
- **Installer**: Yes/No secondary account prompt defaults to **No**; Enter accepts.
- **One-click Linux launcher**: a downloadable AppImage bundles the FalconOS ISO, native renderer bridge, Marketplace bridge and boot script. It launches QEMU and writes its **virtual-only** data to `~/.local/share/FalconOS/`. It never formats a physical disk.

## Running the downloaded AppImage

Download `FalconOS-Native-Desktop-x86_64.AppImage` from the verified GitHub Actions artifact or release. On Arch Linux with QEMU and Python 3 already installed: mark the downloaded AppImage executable in your file manager if asked, then double-click it. The application opens the FalconOS desktop in QEMU; no manual terminal commands to boot the VM.

**System dependencies:** Linux x86_64, Python 3 and `qemu-system-x86_64` with GTK or SDL display backend. The first-run VM helpers use Python's standard library. Downloads from browsers may not retain the executable filesystem bit; this is an OS security requirement and cannot be circumvented by the package.

Open **Falco**, press **F9** for the own native semantic visual engine. Go to **Settings → Wallpaper** to cycle through the four backgrounds.

You can launch the source build with `make run-falco-native ENABLE_BEARSSL=1 ENABLE_BOOT_GLB=0` after installing build dependencies. Chromium is not used anywhere in this branch.

## Security and constraints

Cloudflare bot verification belongs to the site owner: FalconOS does not impersonate a human, solve CAPTCHAs or bypass challenges. The native view refuses a host HTTPS error page and requires actual title/body markers for FalconOS Contour. The gateway listens only on loopback or a private VM network and is hardwired to the single public site. Marketplace GitHub downloads remain checked; no guest scripts run on the Linux host.

This release is a **prototype**, not a general-purpose replacement for Firefox/Chromium or a complete HTML5/CSS3/ECMAScript browser. You can use the old BearSSL text view (F6) to diagnose native TLS. For modern complex sites a complete self-written engine is a larger project.
