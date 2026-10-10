"""Host-side regression checks: public fallback catalog and unified UI wiring."""
import importlib.util
from pathlib import Path
import unittest
from unittest import mock
import urllib.error

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("marketplace_bridge",ROOT/"tools"/"marketplace_bridge.py")
bridge=importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)


class FakeResponse:
    def __init__(self,body):
        self.body=body
    def read(self,n):
        return self.body[:n]
    def __enter__(self):
        return self
    def __exit__(self,*args):
        return False


class StaticMarketplaceTests(unittest.TestCase):
    ROW=("FCAT/1\n"
         "CAT|falcon-test|1.0.0|FalconOS Test App|"+
         "a"*64+"|falcon-test-v1.0.0.app.pkg\n").encode("ascii")

    def test_usable_catalog_when_api_rate_limited(self):
        with mock.patch.object(bridge,"request_json",
                               side_effect=urllib.error.HTTPError("api",403,"rate limit",{},None)):
            with mock.patch.object(bridge.urllib.request,"urlopen",
                                   return_value=FakeResponse(self.ROW)):
                result=bridge.releases()
        self.assertIn("falcon-test",result)
        info=result["falcon-test"]
        self.assertEqual(info["version"],"1.0.0")
        self.assertEqual(info["static_digest"],"a"*64)
        self.assertTrue(info["asset"]["browser_download_url"].startswith(bridge.STATIC_ROOT))

    def test_rejects_path_traversal_and_forged_catalog(self):
        malicious=self.ROW.replace(b"falcon-test-v1.0.0.app.pkg",b"../../passwords")
        with mock.patch.object(bridge.urllib.request,"urlopen",
                               return_value=FakeResponse(malicious)):
            with self.assertRaises(ValueError):
                bridge.static_catalog()
        with self.assertRaises(ValueError):
            bridge.request_bytes(bridge.STATIC_ROOT+"../../secret")

    def test_shell_removed_and_turkish_glyphs_retained(self):
        main=(ROOT/"kernel"/"main.c").read_text(encoding="utf-8")
        apps=(ROOT/"kernel"/"apps.c").read_text(encoding="utf-8")
        self.assertNotIn("mode_developer_input(k)",main)
        self.assertNotIn("mode_developer_render(g_tick)",main)
        self.assertIn('T("Discover","Keşfet")',apps)
        self.assertIn("Türk", (ROOT/"kernel"/"locale.c").read_text(encoding="utf-8"))

if __name__=="__main__":
    unittest.main()
