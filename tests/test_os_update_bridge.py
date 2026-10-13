"""Native ISO updater security: version, manifest and immutable ISO payload.

Runs entirely offline. The QEMU-specific media switch is integration tested
separately; none of these tests writes physical devices.
"""
import hashlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
SPEC=importlib.util.spec_from_file_location("osup",ROOT/"tools"/"falcon_os_update_bridge.py")
UP=importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(UP)

class FakeStream:
    def __init__(self,data):
        self.io=io.BytesIO(data)
    def read(self,size=-1):
        return self.io.read(size)
    def __enter__(self):
        return self
    def __exit__(self,*a):
        pass

def sample():
    raw=bytearray(1024*1024+65536)
    raw[0x8001:0x8006]=b"CD001"
    raw[0]=0x46
    return bytes(raw)

def manifest(data):
    size=len(data)
    sha=hashlib.sha256(data).hexdigest()
    return (
        "FOSUP/1\nversion=2.0.1-beta\ntag=os-v2.0.1-beta\n"
        "asset=FalconOS-2.0.1-beta.iso\nsha256="+sha+
        "\nbytes="+str(size)+"\n"
    ).encode("ascii")

class OSUpdateSecurity(unittest.TestCase):
    def test_real_versus_installed(self):
        self.assertGreater(UP.semver("2.0.1-beta"),UP.semver("2.0.0-beta"))
        with self.assertRaises(ValueError):
            UP.semver("2.0.1-beta;shutdown")
        with self.assertRaises(ValueError):
            UP.semver("99.0.0")
    def test_valid_signed_channel_shape(self):
        entry=UP.parse_manifest(manifest(sample()))
        self.assertEqual(entry["version"],"2.0.1-beta")
        self.assertEqual(entry["tag"],"os-v2.0.1-beta")
    def test_reject_malicious_manifest(self):
        m=manifest(sample())
        for changed in (
            m.replace(b"FalconOS-2.0.1-beta.iso",b"../../tmp/a.iso"),
            m.replace(b"os-v2.0.1-beta",b"anything"),
            m.replace(b"FOSUP/1",b"BAD/1"),
            m.replace(b"sha256=",b"hash="),
            m.replace(b"bytes=",b"bytes=99999999999"),
        ):
            with self.subTest(manifest=changed[:110]),self.assertRaises(ValueError):
                UP.parse_manifest(changed)
    def test_verified_iso_appears_only_after_hash_and_iso9660(self):
        raw=sample()
        entry=UP.parse_manifest(manifest(raw))
        with tempfile.TemporaryDirectory() as temp:
            target=Path(temp)/"update.iso"
            with patch.object(UP.urllib.request,"urlopen",return_value=FakeStream(raw)):
                actual=UP.download_verified(entry,target)
            self.assertEqual(actual,target)
            self.assertEqual(target.read_bytes(),raw)
    def test_corrupted_iso_does_not_become_boot_media(self):
        original=sample()
        entry=UP.parse_manifest(manifest(original))
        bad=bytearray(original);bad[3000]=123
        with tempfile.TemporaryDirectory() as temp:
            target=Path(temp)/"update.iso"
            with patch.object(UP.urllib.request,"urlopen",return_value=FakeStream(bytes(bad))):
                with self.assertRaises(ValueError):
                    UP.download_verified(entry,target)
            self.assertFalse(target.exists())
            self.assertFalse(any(Path(temp).iterdir()))
    def test_atomic_persistent_boot_selection_and_rollback(self):
        with tempfile.TemporaryDirectory() as tmp:
            base=Path(tmp)
            storage=base/"updates";storage.mkdir()
            original=base/"original.iso";original.write_bytes(b"fallback")
            target=storage/"FalconOS-2.0.2-beta.iso"
            target.write_bytes(sample())
            UP.activate_on_next_boot(storage,target)
            pointer=storage/"active.iso"
            self.assertTrue(pointer.is_symlink())
            self.assertEqual(pointer.resolve(),target)
            self.assertEqual(original.read_bytes(),b"fallback")
            with self.assertRaises(ValueError):
                UP.activate_on_next_boot(storage,original)
            self.assertEqual(pointer.resolve(),target)
            pointer.unlink()
            self.assertFalse(pointer.exists())
            self.assertEqual(target.stat().st_size,len(sample()))

    def test_no_direct_kernel_disk_update(self):
        code=(ROOT/"kernel/os_update.c").read_text()
        self.assertIn("#define UPDATE_PORT 0x2F8",code)
        self.assertIn("send(\"INSTALL|\")",code)
        self.assertNotIn("ata_write_lba",code)
        self.assertNotIn("diskdb_store_io",code)
        self.assertIn("os_update_poll()", (ROOT/"kernel/main.c").read_text())
    def test_qemu_update_replaces_named_virtual_iso(self):
        source=(ROOT/"Makefile").read_text()
        self.assertIn("id=falcon_os_cd",source)
        self.assertIn("tools/falcon_os_update_bridge.py",source)
        self.assertIn("update check", (ROOT/"kernel/apps.c").read_text())

if __name__=="__main__":
    unittest.main()
