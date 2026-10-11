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

Download `FalconOS-Native-Desktop-x86_64.AppImage` from the verified GitHub Actions artifact or release. On a regular Linux x86_64 desktop, download the single .AppImage from this GitHub Release. If your file manager requires the standard Linux executable/trust permission for newly downloaded files, enable it once in File Properties. Then double-click the AppImage to start FalconOS; QEMU, the required Python runtime and QEMU BIOS are included inside the AppImage. The application opens the FalconOS desktop in QEMU; no manual terminal commands to boot the VM.

**System requirements:** Linux x86_64, a working desktop display and compatible glibc. The downloadable AppImage includes its own QEMU, GTK GUI plugin, QEMU BIOS, Python 3.12 and required non-glibc shared libraries. Linux may still require approving execution of any downloaded AppImage; the package does not bypass this OS security setting. This is not a Windows .exe.

Open **Falco**, press **F9** for the own native semantic visual engine. Go to **Settings → Wallpaper** to cycle through the four backgrounds.

You can launch the source build with `make run-falco-native ENABLE_BEARSSL=1 ENABLE_BOOT_GLB=0` after installing build dependencies. Chromium is not used anywhere in this branch.

## Security and constraints

Cloudflare bot verification belongs to the site owner: FalconOS does not impersonate a human, solve CAPTCHAs or bypass challenges. The native view refuses a host HTTPS error page and requires actual title/body markers for FalconOS Contour. The gateway listens only on loopback or a private VM network and is hardwired to the single public site. Marketplace GitHub downloads remain checked; no guest scripts run on the Linux host.

This release is a **prototype**, not a general-purpose replacement for Firefox/Chromium or a complete HTML5/CSS3/ECMAScript browser. You can use the old BearSSL text view (F6) to diagnose native TLS. For modern complex sites a complete self-written engine is a larger project.


## Contour visual engine and license note

The real `https://falconos.tech/styles.css` style sheet is the design reference:
near-black gradient hero (`#050d13`, `#08161d`, `#0c2328`), mint
(`#70f1bc`), desktop preview and two-column split. FalconOS implements
this as original native C software-drawing primitives. The displayed
text is gated on a certificate-verified real HTML response and a Contour
identity check; this is neither a screenshot relay nor Chromium/WebKit.

The optional exact Manrope typeface for build-time rasterization is pinned to
Google Fonts commit `8f9a401dbb3793e0d1264b15d96aa253f05280f5`
and SHA-256 `d0639be45d0af36e798172419d7bd173c4bd4f29e2b76cbb69db1d11bf8b0a40`.
Manrope is licensed under the SIL Open Font License 1.1. Its license and
copyright notice can be found at
https://github.com/google/fonts/blob/main/ofl/manrope/OFL.txt .
Raw font files are not bundled: FalconOS's own kernel paints baked alpha glyphs.

## Release qualification and limitations

Falco reproduces the verified `falconos.tech` Contour landing page using the
site's real CSS design tokens and our original native vector/software layout
engine. F9 opens the page, keys 1–4 or the built-in navigation move between
the hero, features, architecture and install sections. This **is not
pixel-for-pixel full browser compatibility**: arbitrary CSS cascade, third-
party sites, dynamic JS, authentication forms and Cloudflare human verification
are not implemented. The source of the *text* is public live HTML, host CA TLS
verified. No source HTML, user agent string or CSS substitute proves the
user to be human.

Release CI validates (1) passwordless auto-login; (2) password-protected lock;
(3) no second user by default; (4) real Falco live site; (5) page section
painting; (6) background settings and persistence over two boots;
(7) the directly executed, self-contained AppImage's actual QEMU ISO boot;
(8) QEMU GTK display under virtual X11, rather than only a headless service.
