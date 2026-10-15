# FalconOS V2.1 public preview — 15 October 2026

## Branch and scope

- Published branch: `v2`
- Baseline: `feature/v2-visual-runtime-refresh-20261014`, based on `v2-beta`
- Version badge: `V2 PREVIEW`
- V1 branch `FalconOS-1-release` remains unchanged.
- CI: `.github/workflows/v2-publication-qa.yml`.
- Boot: x86_64 UEFI/OVMF, own FalconBoot, real original GLB-derived animation,
  kernel C framebuffer UI (not browser screenshots).

## What changed in Falco

**Previous:** the host HTTPS gateway called `semantic_html_text(data)`
unconditionally for ordinary HTML. A guest page received little more than
plain text and lost its heading tags, anchors, CSS, and all structure.

**V2:** a newly tested `project_safe_html` path parses the actual
certificate-verified HTTPS response on the host, discards executable or
active HTML, and creates a bounded, well-formed safe HTML document. The
guest native MiniWeb reads this HTML and itself renders the visible
headings, paragraphs, supported inline CSS, and links. This is a
readability improvement and genuine native guest browser rendering,
*not* reproduction of the original complete site's CSS design.

Browser controls inherited from the V2 feature line:

- bounded 10-site back/forward history; F9 clear guest RAM history
- HTTPS-only link resolution, no arbitrary `javascript:` navigation
- F2 homepage; preset home sites; text zoom, reader, contrast, compact UI
- optional bounded literal-only script-style DOM operations (not JS runtime)
- no auto-execution of downloaded application packages

Host bridge security boundary:

- only public HTTPS destinations, validated certificates+hostnames
- no HTTP downgrade, user credentials in URLs, custom ports,
  non-public DNS addresses, or more than four redirects
- script, style-as-code, event-handler, form and active embed removal;
  sanitized CSS allowlist limited to text readability
- max 2,850 bytes of safe UTF-8 page HTML transferred to guest
- bridge over private VM NAT TCP remains **plaintext**, not end-to-end TLS
- no web accounts, passwords, banking or personal secrets in Falco

## Preview verification

Workflow stages:
1. full Python unit/regression tests including V2 HTML preservation,
   sanitization, UTF-8 bounds, actual gateway reply and redirect security
2. native host-compiled C HTML/CSS engine, URL history and media tests
3. full UEFI ISO build with real GLB boot asset compiled into ESP
4. actual OVMF/QEMU boot + unmodified framebuffer PNG capture
5. downloadable immutable workflow artifacts containing ISO, PNG, diagnostics

The 12-site audit of the previous version was a **pre-change baseline**
and must not be reused as post-V2 success evidence. The primary
measurement now is how many sites show actual headings/links inside the
guest, with honest error screens for JS-required or network-rejected
sites. Public-site availability can be affected by upstream blocking
and request size; compiler and boot tests alone do not prove visual
compatibility with any specific public website.

## Remaining work before a production/stable V2

1. Actual browser-standard HTML5 DOM, CSS layout (flex/grid/media queries),
   UTF-8 shaping and image decoders/loading.
2. Process-isolated JavaScript engine and permission/site-policy enforcement.
3. Native or securely scoped end-to-end TLS suitable for general use,
   cache, DNS hardening, cookies, storage partitioning, browser tabs and forms.
4. User-mode app isolation, PCI/GPU/NVMe/xHCI/audio drivers across
   real devices, fault containment and crash-safe filesystem.
5. Signed Marketplace apps, atomic system updates, verified rollback,
   comprehensive security audits and actual bare-metal compatibility matrix.

**Release classification: V2.1 PREVIEW. Not V2 stable, not production-safe.**
