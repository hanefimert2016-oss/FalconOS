import importlib.util
from pathlib import Path
import unittest

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
