import importlib.util
import pathlib
import struct
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("make_safe_disk", ROOT / "tools" / "make_safe_disk.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class SafeDiskTests(unittest.TestCase):
    def test_partitioned_image_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as temp:
            image = pathlib.Path(temp) / "test.raw"
            module.make_image(image, module.parse_size("4M"))
            self.assertEqual(image.stat().st_size, 4 * 1024 * 1024)
            with image.open("rb") as fp:
                mbr = fp.read(512)
                fp.seek(2048 * 512)
                self.assertEqual(fp.read(64), bytes(64))
            self.assertEqual(mbr[510:512], b"\x55\xaa")
            self.assertEqual(mbr[450], 0xFA)
            self.assertEqual(struct.unpack_from("<II", mbr, 454),
                             (2048, 4 * 1024 * 1024 // 512 - 2048))
            with self.assertRaises(FileExistsError):
                module.make_image(image, module.parse_size("4M"))
            with image.open("rb") as fp:
                self.assertEqual(fp.read(512), mbr)

    def test_bad_sizes(self):
        for size in ("0", "500", "3MB", "1K", "129G", "test"):
            with self.subTest(size=size), self.assertRaises(ValueError):
                module.parse_size(size)


if __name__ == "__main__":
    unittest.main()
