"""Protocol-level smoke test for the host Marketplace bridge (no GitHub calls)."""
import importlib.util
from pathlib import Path
import socket
import hashlib
import threading
import unittest
from unittest.mock import patch

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("bridge", root / "tools" / "marketplace_bridge.py")
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)

PKG = (
    b"FAPP/1\nid=hello-world\nname=Hello World\nversion=1.0.0\n"
    b"summary=My sample application\n\necho hello\n"
)

class ProtocolTest(unittest.TestCase):
    def test_uefi_firmware_noise_resynchronizes_before_valid_list(self):
        left,right=socket.socketpair()
        try:
            right.sendall(b"OVMF console\r\n" + b"X"*240 +
                          b"\n" + b"\x00bad-line\n" + b"LIST\n")
            self.assertEqual(bridge.read_line(left),"LIST")
            right.sendall(b"GET|device-doctor\n")
            self.assertEqual(bridge.read_line(left),"GET|device-doctor")
        finally:
            left.close();right.close()

    def test_refresh_during_cat_ack_is_safe(self):
        left,right=socket.socketpair()
        errors=[]
        def send_cat():
            try:
                bridge.send_acknowledged(left,"CAT|device-doctor|1.0.0|Device Doctor")
            except Exception as exc:
                errors.append(str(exc))
        t=threading.Thread(target=send_cat,daemon=True)
        t.start()
        try:
            response=bytearray()
            while not response.endswith(b"\n"):
                response.extend(right.recv(1))
            self.assertTrue(response.startswith(b"CAT|device-doctor|"))
            # A duplicate F5 arrived before guest's ACK: do not abort
            # an otherwise valid immutable Release transfer.
            right.sendall(b"LIST\nACK\n")
            t.join(timeout=4)
            self.assertFalse(t.is_alive(),"catalog ACK timed out")
            self.assertFalse(errors,errors)
        finally:
            left.close();right.close()

    def test_catalog_and_chunked_download(self):
        release = {
            "hello-world": {
                "checksum_asset": {"browser_download_url": "https://github.com/hanefimert2016-oss/FalconOS-Marketplace/releases/download/a/a.app.pkg.sha256"},
                "asset": {"name": "a.app.pkg", "browser_download_url":
                    "https://github.com/hanefimert2016-oss/FalconOS-Marketplace/releases/download/a/a.app.pkg"},
                "version": "1.0.0",
                "release": {"name": "Hello World"},
            }
        }
        left, right = socket.socketpair()
        right.settimeout(4)
        worker_errors = []
        def server():
            try:
                bridge.serve(left)
            except ConnectionError:
                pass
            except OSError:
                pass
            except Exception as exc:
                worker_errors.append(str(exc))
        def read_line():
            data = bytearray()
            while True:
                c = right.recv(1)
                if c == b"\n": return data.decode("ascii")
                if not c: raise RuntimeError("socket closed unexpectedly")
                data += c
        checksum = hashlib.sha256(PKG).hexdigest().encode("ascii") + b"  a.app.pkg\n"
        def fake_download(url):
            return checksum if url.endswith(".sha256") else PKG
        with patch.object(bridge, "releases", return_value=release), patch.object(
                bridge, "request_bytes", side_effect=fake_download):
            thread = threading.Thread(target=server, daemon=True)
            thread.start()
            try:
                right.sendall(b"LIST\n")
                self.assertEqual(read_line(), "CAT|hello-world|1.0.0|Hello World")
                right.sendall(b"ACK\n")
                self.assertEqual(read_line(), "DONE")
                right.sendall(b"GET|hello-world\n")
                header = read_line().split("|")
                self.assertEqual(header[0], "BEGIN")
                self.assertEqual(header[1], "hello-world")
                self.assertEqual(int(header[2]), len(PKG))
                right.sendall(b"ACK\n")
                payload = bytearray()
                while True:
                    line = read_line()
                    if line == "END":
                        right.sendall(b"ACK\n")
                        break
                    self.assertTrue(line.startswith("CHUNK|"))
                    payload.extend(bytes.fromhex(line[6:]))
                    right.sendall(b"ACK\n")
                self.assertEqual(bytes(payload), PKG)
            finally:
                right.close()
                left.close()
                thread.join(timeout=3)
        self.assertFalse(worker_errors, worker_errors)
