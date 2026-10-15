"""Prevent the V2-beta branch from silently reverting to FalconOS 1 branding."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class V2ReleaseBrandingTests(unittest.TestCase):
    def test_single_authoritative_beta_label(self):
        version = (ROOT / "kernel/version.h").read_text(encoding="utf-8")
        self.assertIn('#define FALCON_RELEASE_LABEL         "FalconOS 2.0 Preview"', version)
        self.assertIn('#define FALCON_RELEASE_BADGE         "V2 PREVIEW"', version)
        self.assertIn('#define FALCON_RELEASE_CHANNEL       "v2"', version)

    def test_about_and_launcher_share_release_label(self):
        apps = (ROOT / "kernel/apps.c").read_text(encoding="utf-8")
        self.assertIn("gfx_text_lg_centered(cx, wy + 180, FALCON_RELEASE_LABEL, PAL_TEXT);", apps)
        self.assertIn('{ "About",      FALCON_RELEASE_LABEL,', apps)

    def test_shell_commands_use_central_version(self):
        apps = (ROOT / "kernel/apps.c").read_text(encoding="utf-8")
        self.assertIn('FALCON_RELEASE_LABEL " x86_64 bare-metal"', apps)
        self.assertIn('FALCON_RELEASE_LABEL " i386 bare-metal"', apps)
        self.assertIn('k_strcpy(out, FALCON_RELEASE_KERNEL_PREFIX);', apps)
        self.assertIn('FALCON_RELEASE_CHANNEL', apps)

    def test_desktop_beta_badge(self):
        main = (ROOT / "kernel/main.c").read_text(encoding="utf-8")
        self.assertIn("gfx_text_centered(168,8,FALCON_RELEASE_BADGE,PAL_ACCENT);", main)


if __name__ == "__main__":
    unittest.main()
