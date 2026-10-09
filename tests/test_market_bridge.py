import importlib.util
from pathlib import Path
import unittest
from unittest import mock

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("market_bridge", root / "tools" / "marketplace_bridge.py")
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)

class BridgeTests(unittest.TestCase):
    def test_valid_pkg(self):
        raw = (
            b"FAPP/1\nid=hello-world\nname=Hello World\nversion=1.0.0\n"
            b"summary=My example app\n\necho hello\nuname\n"
        )
        metadata = bridge.validate_package(raw, "hello-world")
        self.assertEqual(metadata["version"], "1.0.0")
    def test_reject_wrong_id(self):
        raw = (
            b"FAPP/1\nid=hello-world\nname=Hello World\nversion=1.0.0\n"
            b"summary=My example app\n\necho hello\n"
        )
        with self.assertRaises(ValueError):
            bridge.validate_package(raw, "another-app")
    def test_reject_shell_operators(self):
        for code in (b"reboot\n", b"echo x; reboot\n", b"echo hi > disk\n", b"echo hello & reboot\n"):
            raw = (
                b"FAPP/1\nid=hello-world\nname=Hello World\nversion=1.0.0\n"
                b"summary=My example app\n\n" + code
            )
            with self.subTest(code=code), self.assertRaises(ValueError):
                bridge.validate_package(raw, "hello-world")


class VersionSelectionTests(unittest.TestCase):
    def test_semver_natural_order(self):
        self.assertGreater(bridge.semver_key("1.10.0"), bridge.semver_key("1.9.99"))
        self.assertGreater(bridge.semver_key("1.0.0"), bridge.semver_key("1.0.0-rc.1"))
        self.assertLess(bridge.semver_key("1.0.0-beta.2"), bridge.semver_key("1.0.0-beta.11"))
        for v in ("1.0.0-", "1.0.0-rc.", "01.0.0", "4294967296.0.0"):
            with self.subTest(version=v), self.assertRaises(ValueError):
                bridge.semver_key(v)

    def test_newest_version_wins_even_when_releases_out_of_order(self):
        versions = ["1.9.0", "1.10.0", "1.4.0"]
        releases = []
        for version in versions:
            filename = f"hello-world-v{version}.app.pkg"
            releases.append({
                "draft": False, "prerelease": False,
                "tag_name": f"app-hello-world-v{version}", "name": "Hello World",
                "assets": [
                    {"name": filename, "size": 100, "browser_download_url": "https://github.com/example"},
                    {"name": filename + ".sha256", "size": 100, "browser_download_url": "https://github.com/example"}
                ]
            })
        def pages(url):
            return releases if "page=1" in url else []
        with mock.patch.object(bridge, "request_json", side_effect=pages):
            result = bridge.releases()
        self.assertEqual(result["hello-world"]["version"], "1.10.0")
