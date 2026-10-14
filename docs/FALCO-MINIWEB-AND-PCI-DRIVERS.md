# Falco MiniWeb/3 + real PCI driver inventory (V2 Beta, October 2026)

## Falco: WHAT WORKS

- Real native software-rendered HTML nodes: h1/h2/h3, paragraphs, div/section,
  article, labels, list items and push buttons. Content is painted into the
  FalconOS framebuffer, not represented by server-generated screenshots.
- CSS subsets: `color`, `background[-color]`, `font-size: 10..72px`,
  `text-align`, `font-weight`, `display:none`.
- Selector subsets: single tag, `#id`, and `.class`; inline style overrides
  simple style rules. Color values include #rgb, #rrggbb, and some named colors.
- Safe JavaScript subset: **literal assignments only**:
  `document.getElementById('status').textContent='Hello'`
  or `document.getElementById('status').style.color='red'`.
  Those operations may appear inside a small inline `<script>` or
  `button onclick`. Arbitrary JavaScript is NOT evaluated.
- `falco.demo` or `falco://demo` from Browser address bar loads a real,
  explicit local offline CSS/JS demonstration. Click the button to mutate a
  DOM node in the OS. Never represented as an HTTPS site.
- If HTML is unsupported, SemanticText/2 remains a read-only fallback;
  real external pages require certificate-verified HTTPS from the supported
  host gateway and size-limited content.

## What does NOT work

No complete CSS cascade, flexbox, grid, selectors with combinators,
media queries, layout reflow, font downloads, external scripts/styles,
ES modules, fetch, DOM event queues, cookies, WebAssembly, service workers,
video/DRM, or Chromium/Firefox compatibility. No third-party JS is compiled
or run as kernel machine code. Calling this a full modern JS/CSS renderer
would be inaccurate and unsafe.

## Real drivers

- RTL8139 is an existing **working QEMU Ethernet driver**. Initialization
  now validates all 32-bit DMA buffer addresses and I/O BAR bounds before
  enabling bus mastering; scanning handles PCI multifunction devices.
- PCI enumeration is read-only and now safely follows the conventional
  capability chain (max 32 hops; cycle detection) to detect MSI, MSI-X,
  PCIe and power-management capabilities. This does not activate any of them.
- `lspci` and `lspci -v` distinguish
  `[RTL8139 ACTIVE]`, `[RTL8139 pending]`, and `[probe only]`.
- PCI vendor detection includes Intel, NVIDIA, Realtek; NVMe, xHCI, AHCI
  and dGPU are **not functional hardware drivers** in this release.
  Do not attempt real GPU acceleration or physical disk installation.

## Validation

```sh
gcc -std=c11 -Wall -Wextra -Werror -Ikernel \
    tests/falco_miniweb_test.c kernel/falco_miniweb.c -o /tmp/falco-miniweb
/tmp/falco-miniweb
make iso ENABLE_BOOT_GLB=0 ENABLE_BEARSSL=0
make run
```

The QEMU UI regression also captures the actual Browser visiting `falco.demo`
and live `lspci -v` device capabilities. No browser or GPU drivers are
injected from Windows/Linux into FalconOS.

No separate website is added to FalconOS Marketplace by this change.
