"""Protocol-level smoke test for the host Marketplace bridge (no GitHub calls)."""
import importlib.util
from pathlib import Path
import socket
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
    def test_catalog_and_chunked_download(self):
        release = {
            "hello-world": {
                "asset": {"browser_download_url":
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
        with patch.object(bridge, "releases", return_value=release), patch.object(
                bridge, "request_bytes", return_value=PKG):
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
