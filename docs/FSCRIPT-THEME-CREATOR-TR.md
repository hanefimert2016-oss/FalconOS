# FalconOS Theme Creator + FScript 0.1

This branch is the Chromium-free **native FalconOS** implementation.

## Theme Creator

Open **Launchpad → Theme Creator**. Controls:

- **P** selects the next complete palette preset.
- **Left/Right** chooses wallpaper top, wallpaper bottom or accent.
- **Tab** selects red, green or blue; **Up/Down** adjusts the channel.
- **Space** enables or disables the custom theme; **S** persists to the selected dedicated 0xFA FalconFS partition.
- **E** exports inert `/home/falcon/Desktop/my-theme.fth` (`FTHEME/1`); **I** imports it.

Theme colors are drawn pixel-accurately into the FalconOS framebuffer and affect the actual desktop. No HTML, Chromium or remote screenshots are used. Without an explicitly selected safe FalconFS disk, changes last only until shutdown. The persistent settings loader migrates the two previous saved settings layouts without moving account/password fields.

## FScript — FalconOS's own language

FScript **is not C, JavaScript, Python, ELF or shell**. It is an interpreted, sandboxed and deliberately bounded first version of a native GUI application language. A sample:

```fscript
FSC/1
uygulama Sayacim
arka #122238
renk #67E7B3
baslik Benim Ilk Uygulamam
metin Bu yaziyi FScript cizer.
sayac tiklama 0
dugme Arttir -> tiklama 1
deger tiklama
cizgi
```

| Statement | Effect |
|---|---|
| `FSC/1` | Magic header, required on first line |
| `uygulama AD` | Window title |
| `arka #RRGGBB` | Native window background |
| `renk #RRGGBB` | Native foreground/button accent |
| `baslik METIN` | Heading |
| `metin METIN` | Label |
| `sayac ad 0` | Declare integer counter |
| `dugme Label -> ad 1` | Button that changes the counter on Enter |
| `deger ad` | Draw counter |
| `cizgi` | Divider |

Limits: 4095 bytes, 40 visible instructions, 8 counters. Arbitrary memory, hardware, filesystem, host subprocess and network calls are **not available** inside the FScript VM. It doesn't execute C. Later FScript versions may add variables/loops/events through carefully designed capabilities.

## CodeDium

Open **Launchpad → CodeDium**. Keyboard actions are F5 Save (`project.fsc`), F6 run real native GUI, F7 build **both** `my-app.app.pkg` (run on double-click) and `my-app.setup.app.pkg` (opens FalconOS Setup Wizard on double-click), F8 in-app `yardimci.txt` manual. The underlying instructions and exports are FSC/1 source, not an executable Linux file.

Installer can install an app to `/home/falcon/apps` or the Desktop; the default apps installation creates a corresponding `.link` shortcut on the native desktop. The setup wizard requires explicit Enter confirmation and doesn't auto-run downloads. Legacy FAPP/1 packages still run their strictly allowlisted commands.

## Marketplace

The `hanefimert2016-oss/FalconOS-Marketplace` repository publishes versioned reviewed FAPP/1 launchers that open the real built-in Theme Creator and CodeDium windows. These launcher packages **require a FalconOS ISO with those apps built in**; downloading them onto older ISOs does not install a complete editor binary. Native FScript `FSC/1` distribution is enabled for direct HTTPS links and their own setup documents, not yet integrated into Marketplace's old FAPP-only publish builder.

Existing **ISO Studio** is already listed in Marketplace but a fully safe physical USB ISO-writing workflow is **not verified**. USB block writes are deliberately not enabled just because a package says ISO Studio.

## Boot, locale and browser compatibility

- GRUB/Multiboot2 is still the bootloader, but **hidden** and configured for **zero-second auto-boot**. Firmware `gfxmode=auto` chooses a graphics mode; FalconOS adapts its UI to the framebuffer. EDID-based optimal resolution is not guaranteed.
- The installer offers Turkish, English, German, French and Spanish. It suggests keyboard and timezone from the selected language: this is an **offline heuristic**, not geolocation or DST lookup.
- `tools/falco_50_site_audit.py` samples exactly 50 random public sites using a recorded seed and checks real HTTPS and HTML. HTTP success **does not prove** full native HTML/CSS/JavaScript rendering. FalconOS still has a bespoke owner-site visual renderer, not a general-purpose standards-compliant web engine.
