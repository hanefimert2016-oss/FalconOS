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
    def settimeout(self,seconds):
        self.timeout=seconds
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



class HardeningTests(unittest.TestCase):
    def test_latest_static_version_selected_even_out_of_order(self):
        import io,hashlib
        def line(version):
            return ("CAT|hello-world|"+version+"|Hello World|"
                    +hashlib.sha256(version.encode()).hexdigest()
                    +"|hello-world-v"+version+".app.pkg")
        data=("FCAT/1\n"+line("1.10.0")+"\n"+line("1.2.0")+"\n").encode()
        with mock.patch.object(bridge.urllib.request,"urlopen",return_value=io.BytesIO(data)):
            result=bridge.static_catalog()
        self.assertEqual(result["hello-world"]["version"],"1.10.0")

    def test_missing_title_cannot_break_listing(self):
        entry={"hello-world":{
            "version":"1.0.0",
            "asset":{"name":"hello-world-v1.0.0.app.pkg","browser_download_url":"https://example.com/app"},
            "checksum_asset":{"browser_download_url":"https://example.com/sha"},
            "release":{"name":None}}}
        wire=FakeUART(["LIST"])
        with mock.patch.object(bridge,"releases",return_value=entry),mock.patch.object(bridge.time,"sleep",return_value=None):
            with self.assertRaises(ConnectionError):bridge.serve(wire)
        self.assertIn("CAT|hello-world|1.0.0|hello-world",wire.outbound.decode())

    def test_empty_checksum_does_not_raise_indexerror(self):
        data=(b"FAPP/1\nid=hello-world\nname=Hello World\nversion=1.0.0\n"
              b"summary=My example app\n\necho hello\n")
        entry={"hello-world":{
            "version":"1.0.0",
            "asset":{"name":"hello-world-v1.0.0.app.pkg","browser_download_url":"https://example.com/app"},
            "checksum_asset":{"browser_download_url":"https://example.com/sha"},
            "release":{"name":"Hello World"}}}
        wire=FakeUART(["GET|hello-world"])
        with mock.patch.object(bridge,"releases",return_value=entry), \
             mock.patch.object(bridge,"request_bytes",side_effect=lambda url:b"" if url.endswith("/sha") else data), \
             mock.patch.object(bridge.time,"sleep",return_value=None):
            with self.assertRaises(ConnectionError):bridge.serve(wire)
        self.assertIn("ERR|download failed",wire.outbound.decode())


class AssetRedirectSecurityTests(unittest.TestCase):
    """Only HTTPS redirects to GitHub-controlled asset servers are allowed."""

    def test_block_untrusted_asset_redirects(self):
        handler = bridge.AssetRedirectHandler()
        request = bridge.urllib.request.Request(
            "https://github.com/hanefimert2016-oss/FalconOS-Marketplace/"
            "releases/download/app-hello-world-v1.0.0/hello-world-v1.0.0.app.pkg"
        )
        for destination in (
            "http://release-assets.githubusercontent.com/package",
            "https://example.com/package",
            "https://127.0.0.1/private",
            "https://user:pass@release-assets.githubusercontent.com/file",
            "https://release-assets.githubusercontent.com:8443/file",
            "https://release-assets.githubusercontent.com.evil.example/file",
        ):
            with self.subTest(destination=destination), self.assertRaises(ValueError):
                handler.redirect_request(request, None, 302, "Found", {}, destination)

    def test_allow_exact_github_asset_cdn(self):
        handler = bridge.AssetRedirectHandler()
        request = bridge.urllib.request.Request(
            "https://github.com/hanefimert2016-oss/FalconOS-Marketplace/"
            "releases/download/app-hello-world-v1.0.0/hello-world-v1.0.0.app.pkg"
        )
        destination = "https://release-assets.githubusercontent.com/signed?token=abc"
        next_request = handler.redirect_request(
            request, None, 302, "Found", {}, destination
        )
        self.assertEqual(next_request.full_url, destination)
        self.assertLessEqual(handler.max_redirections, 4)

class SignedPublisherTests(unittest.TestCase):
    """Cryptographic publisher authentication is separate from SHA-256."""

    PKG = (b"FAPP/1\nid=hello-world\nname=Hello World\nversion=1.0.0\n"
           b"summary=Test publisher app\n\necho signed package\n")

    @classmethod
    def setUpClass(cls):
        import shutil, subprocess, tempfile
        if not shutil.which("openssl"):
            raise unittest.SkipTest("OpenSSL Ed25519 verifier unavailable")
        cls.tmp = tempfile.TemporaryDirectory()
        root = Path(cls.tmp.name)
        cls.public = root / "publisher.pub.pem"
        cls.private = root / "publisher.private.pem"
        cls.pkg = root / "hello-world.app.pkg"
        cls.sig = root / "hello-world.app.pkg.sig"
        cls.pkg.write_bytes(cls.PKG)
        for cmd in (
            ["openssl", "genpkey", "-algorithm", "Ed25519", "-out", str(cls.private)],
            ["openssl", "pkey", "-in", str(cls.private), "-pubout",
             "-out", str(cls.public)],
            ["openssl", "pkeyutl", "-sign", "-inkey", str(cls.private),
             "-rawin", "-in", str(cls.pkg), "-out", str(cls.sig)],
        ):
            subprocess.run(cmd, check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_valid_signature_and_tampering(self):
        signature = self.sig.read_bytes()
        self.assertTrue(bridge.verify_publisher_signature(
            self.PKG, signature, self.public))
        with self.assertRaises(ValueError):
            bridge.verify_publisher_signature(
                self.PKG + b"\n", signature, self.public)
        with self.assertRaises(ValueError):
            bridge.verify_publisher_signature(self.PKG, signature[:4], self.public)

    def _entry(self, signed=True):
        import hashlib
        filename = "hello-world-v1.0.0.app.pkg"
        pkg_url = "https://github.com/hanefimert2016-oss/FalconOS-Marketplace/releases/download/app-hello-world-v1.0.0/" + filename
        e = {
            "version": "1.0.0",
            "asset": {"name": filename, "browser_download_url": pkg_url},
            "checksum_asset": {"name": filename + ".sha256",
                              "browser_download_url": pkg_url + ".sha256"},
            "release": {"name": "Hello World"}
        }
        if signed:
            e["signature_asset"] = {"name": filename + ".sig",
                                    "browser_download_url": pkg_url + ".sig"}
        return {"hello-world": e}

    def test_signed_only_get_accepts_correct_signature(self):
        import hashlib
        signature = self.sig.read_bytes()
        pkg = self.PKG
        digest = hashlib.sha256(pkg).hexdigest()
        received = FakeUART(["GET|hello-world"] + ["ACK"] * (2 + (len(pkg)+15)//16))
        def download(url):
            if url.endswith(".sig"):
                return signature
            if url.endswith(".sha256"):
                return (digest + "  hello-world-v1.0.0.app.pkg\n").encode()
            return pkg
        with mock.patch.object(bridge, "releases", return_value=self._entry()), \
             mock.patch.object(bridge, "request_bytes", side_effect=download):
            with self.assertRaises(ConnectionError):
                bridge.serve(received, trusted_publisher_key=self.public)
        result = received.outbound.decode()
        self.assertIn("BEGIN|hello-world|", result)
        self.assertIn("CHUNK|", result)
        self.assertIn("END", result)

    def test_signed_only_get_rejects_unsigned_release(self):
        import hashlib
        pkg = self.PKG
        digest = hashlib.sha256(pkg).hexdigest()
        received = FakeUART(["GET|hello-world"])
        def download(url):
            if url.endswith(".sha256"):
                return (digest + "  hello-world-v1.0.0.app.pkg\n").encode()
            return pkg
        with mock.patch.object(bridge, "releases", return_value=self._entry(False)), \
             mock.patch.object(bridge, "request_bytes", side_effect=download):
            with self.assertRaises(ConnectionError):
                bridge.serve(received, trusted_publisher_key=self.public)
        self.assertNotIn("BEGIN|", received.outbound.decode())
        self.assertIn("ERR|download failed", received.outbound.decode())
