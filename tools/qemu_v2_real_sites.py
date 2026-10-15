#!/usr/bin/env python3
"""Real native Falco V2 website samples. Reuses actual PS/2 guest UI tests.

No simulated page, Chromium embed, HTML screenshot renderer or fake domain.
Only sites observed returning real HTTPS content in the earlier V2 baseline.
"""
from pathlib import Path
import qemu_falco_12sites_audit as audit
p=Path("build/falco-v2-websites").absolute()
p.mkdir(parents=True,exist_ok=True)
audit.SITES=[
 "example.com",
 "www.kde.org",
 "www.rust-lang.org",
 "www.wikimedia.org",
 "www.kernel.org",
 "www.mozilla.org",
]
audit.OUT=p
audit.DEBUG=p/"v2-qemu-guest-debug.log"
audit.MONITOR=p/"v2-qemu-monitor.sock"
audit.GATEWAY_LOG=p/"v2-https-gateway.log"
audit.REPORT=p/"falco-v2-real-sites.json"
audit.COMPOSITE=p/"Falco-V2-six-real-websites.png"
if __name__=="__main__":
 audit.main()
