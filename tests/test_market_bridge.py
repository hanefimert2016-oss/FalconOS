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


class FakeUART:
    def __init__(self,lines):
        self.inbound=bytearray(("\n".join(lines)+"\n").encode("ascii"))
        self.outbound=bytearray()
    def recv(self,n):
        if not self.inbound:return b""
        first=self.inbound[:n]
        del self.inbound[:n]
        return bytes(first)
    def sendall(self,data):
        self.outbound.extend(data)


class GuestPublishingTests(unittest.TestCase):
    PKG=(b"FAPP/1\nid=hello-world\nname=Hello World\nversion=2.0.0\n"
         b"summary=Published by CodeDium\n\n"
         b"echo FalconOS publish test\nuname\n")

    def lines(self,pkg=None):
        import hashlib
        pkg=pkg or self.PKG
        digest=hashlib.sha256(pkg).hexdigest()
        lines=["UP|hello-world|2.0.0|"+str(len(pkg))+"|"+digest]
        lines.extend("DAT|"+pkg[i:i+16].hex() for i in range(0,len(pkg),16))
        lines.append("UPEND")
        return lines

    def run_guest(self,lines,publisher=None):
        fake=FakeUART(lines)
        with mock.patch.object(bridge.time,"sleep",return_value=None):
            with self.assertRaises(ConnectionError):
                bridge.serve(fake,publisher=publisher)
        return fake.outbound.decode("ascii")

    def test_unauthorized_upload_refused(self):
        answer=self.run_guest(self.lines())
        self.assertIn("PUBERR|",answer)
        self.assertNotIn("PUBOK|",answer)

    def test_authorized_upload_receives_host_ack(self):
        captured=[]
        def publish(raw,meta):
            captured.append((raw,meta))
            return "app-hello-world-v2.0.0"
        answer=self.run_guest(self.lines(),publish)
        self.assertEqual(captured[0][0],self.PKG)
        self.assertEqual(captured[0][1]["id"],"hello-world")
        self.assertIn("PUBOK|app-hello-world-v2.0.0",answer)

    def test_invalid_hash_refused_before_publisher(self):
        calls=[]
        tampered=self.lines()
        tampered[-2]="DAT|00"
        answer=self.run_guest(tampered,lambda *a:calls.append(a))
        self.assertIn("PUBERR|",answer)
        self.assertFalse(calls)

    def test_gh_release_commands_are_fixed_and_checksum_uploaded(self):
        def mocked(cmd,**kwargs):
            if cmd[:3]==["gh","release","create"]:
                self.assertEqual(cmd[3],"app-hello-world-v2.0.0")
                self.assertEqual(cmd[6:8],["--repo",bridge.REPO])
                from pathlib import Path
                pkg=Path(cmd[4]);sha=Path(cmd[5])
                self.assertEqual(pkg.read_bytes(),self.PKG)
                self.assertTrue(sha.read_text().startswith(
                    __import__("hashlib").sha256(self.PKG).hexdigest()))
            return mock.Mock(returncode=1 if cmd[:3]==["gh","release","view"] else 0,
                             stderr="",stdout="")
        with mock.patch.object(bridge.subprocess,"run",side_effect=mocked):
            self.assertEqual(
                bridge.publish_release(self.PKG,
                    bridge.validate_package(self.PKG,"hello-world")),
                "app-hello-world-v2.0.0")

