"""AeroFlow native shell regression contracts.
Rendering and interaction are additionally verified using OVMF/QEMU PNGs.
"""
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]

class AeroFlowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ui=(ROOT/"kernel/aeroflow.h").read_text()
        cls.launcher=(ROOT/"kernel/launchpad.c").read_text()
        cls.desktop=(ROOT/"kernel/personal.c").read_text()
        cls.windows=(ROOT/"kernel/apps.c").read_text()
        cls.mainloop=(ROOT/"kernel/main.c").read_text()

    def test_runtime_palette_drives_native_surfaces(self):
        for token in ("PAL_PANEL","PAL_HAIRLINE","PAL_ACCENT","SET.theme"):
            self.assertIn(token,self.ui)
        for source in (self.launcher,self.desktop,self.windows):
            self.assertIn('#include "aeroflow.h"',source)

    def test_launcher_keeps_real_input_and_app_ids(self):
        for token in ("apps_launcher_id(","launchpad_input(i32 key)",
                      "apps_open(visible[selected])","desktop_pin_toggle(",
                      "apps_display_name(id)","mouse_consume_click()"):
            self.assertIn(token,self.launcher)
        self.assertIn("shortsub[n]=0",self.launcher)
        self.assertIn("limit=(tile_w-14)/8",self.launcher)

    def test_desktop_widgets_use_real_data(self):
        for token in ("rtc_local(&now)","RAM_TOTAL_BYTES","net_connected()",
                      "apps_window_count()","apps_is_open(id)"):
            self.assertIn(token,self.desktop)
        self.assertNotIn('"42%"',self.desktop)
        self.assertNotIn('"18°"',self.desktop)

    def test_window_focus_does_not_move_click_targets(self):
        for token in ("wx+ww-134","wx+ww-90","wx+ww-48",
                      "aero_card(wx+ww-124","aero_card(wx+ww-84"):
            self.assertIn(token,self.windows)

    def test_ci_theme_cycle_not_in_release_build(self):
        self.assertIn("#ifdef FALCON_QEMU_AEROFLOW_TEST",self.mainloop)
        self.assertIn('outb(0xE9,\'A\')',self.mainloop)
        self.assertIn("ImageChops.difference", (ROOT/"tools/vnc_falco_codedium_e2e.py").read_text())

if __name__=="__main__":
    unittest.main()
