"""Ensure QEMU commands boot the FalconOS ISO before persistent data drives."""
import subprocess
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class QemuBootPriorityTests(unittest.TestCase):
    def test_all_qemu_make_targets_boot_from_cdrom(self):
        targets = ("run-disk", "run-disk-headless", "run-disk-ephemeral",
                   "run-market", "run-market-publish-vm", "run-cdrom")
        for target in targets:
            with self.subTest(target=target):
                result = subprocess.run(
                    ["make", "-n", target], cwd=ROOT, capture_output=True,
                    text=True, check=True
                )
                qemu_lines = [line for line in result.stdout.splitlines()
                              if "qemu-system-" in line]
                self.assertTrue(qemu_lines, f"{target} did not invoke QEMU")
                for line in qemu_lines:
                    self.assertIn("-cdrom", line)
                    self.assertIn("-boot order=d", line)

    def test_host_only_publisher_does_not_boot_qemu(self):
        result = subprocess.run(["make", "-n", "run-market-publish"], cwd=ROOT,
                                capture_output=True, text=True, check=True)
        self.assertIn("tools/publish_app.py", result.stdout)
        self.assertNotIn("qemu-system-", result.stdout)


if __name__ == "__main__":
    unittest.main()
