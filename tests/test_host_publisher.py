"""Standalone public Release publisher: never start QEMU, always validate FAPP/1."""
import contextlib
import importlib.util
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
spec = importlib.util.spec_from_file_location("publish_app", ROOT / "tools" / "publish_app.py")
publisher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publisher)

VALID = (b"FAPP/1\nid=hello-world\nname=Hello World\nversion=3.0.0\n"
         b"summary=Safe script test\n\necho hello\nuname\n")


class StandalonePublisherTests(unittest.TestCase):
    def test_make_target_does_not_build_or_boot_vm(self):
        result = subprocess.run(["make", "-n", "run-market-publish"],
                                cwd=ROOT, capture_output=True, text=True, check=True)
        self.assertIn("tools/publish_app.py", result.stdout)
        self.assertNotIn("qemu-system", result.stdout)
        self.assertNotIn("nasm ", result.stdout)
        self.assertNotIn("grub-mkrescue", result.stdout)

    def test_check_only_validates_without_accessing_github(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "code.app.pkg"
            path.write_bytes(VALID)
            with mock.patch.object(publisher, "publish_release") as upload:
                with contextlib.redirect_stdout(io.StringIO()) as output:
                    result = publisher.main(["--package", str(path), "--check-only"])
            self.assertEqual(result, 0)
            self.assertIn("Nothing was uploaded", output.getvalue())
            upload.assert_not_called()

    def test_real_publish_requires_explicit_confirmation(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "code.app.pkg"
            path.write_bytes(VALID)
            with mock.patch.object(publisher, "publish_release") as upload:
                with mock.patch.object(publisher.sys, "stdin", io.StringIO("no\n")):
                    result = publisher.main(["--package", str(path), "--yes"])
                self.assertEqual(result, 0)
                upload.assert_called_once()
                self.assertEqual(upload.call_args.args[0], VALID)
                self.assertEqual(upload.call_args.args[1]["version"], "3.0.0")

    def test_validation_rejects_malformed_package_before_github(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "dangerous.app.pkg"
            path.write_bytes(VALID.replace(b"echo hello\n", b"reboot\n"))
            with mock.patch.object(publisher, "publish_release") as upload:
                with contextlib.redirect_stderr(io.StringIO()):
                    result = publisher.main(["--package", str(path), "--yes"])
                self.assertEqual(result, 1)
                upload.assert_not_called()

    def test_missing_file_reports_error_no_nasm_required(self):
        with mock.patch.object(publisher, "publish_release") as upload:
            with contextlib.redirect_stderr(io.StringIO()):
                result = publisher.main(["--package", "/path/that/does/not/exist.app.pkg"])
        self.assertEqual(result, 1)
        upload.assert_not_called()

if __name__ == "__main__":
    unittest.main()
