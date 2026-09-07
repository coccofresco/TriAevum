import tempfile
import unittest
from pathlib import Path
from zipfile import ZipFile, ZipInfo

from common import sha256_file
from vsix_extract import extract_vsix


class VsixExtractTests(unittest.TestCase):
    def test_extracts_payload_and_preserves_metadata(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / "payload.vsix"
            with ZipFile(archive, "w") as package:
                package.writestr("Contents/VC/include/vector", b"fixture header")
                package.writestr("extension.vsixmanifest", b"fixture metadata")
            output = root / "output"
            result = extract_vsix(archive, sha256_file(archive), output)
            self.assertEqual(len(result["files"]), 2)
            self.assertEqual((output / "Contents/VC/include/vector").read_bytes(), b"fixture header")
            with self.assertRaisesRegex(ValueError, "new output"):
                extract_vsix(archive, sha256_file(archive), output)

    def test_rejects_unsafe_members_without_publishing(self):
        for names in (("../escape",), ("NUL",), ("A", "a"), ("C:/escape",), ("a ",)):
            with self.subTest(names=names), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                archive = root / "payload.vsix"
                with ZipFile(archive, "w") as package:
                    for name in names:
                        package.writestr(name, b"test")
                with self.assertRaises(ValueError):
                    extract_vsix(archive, sha256_file(archive), root / "output")
                self.assertFalse((root / "output").exists())

    def test_rejects_hash_links_and_size_limit(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / "payload.vsix"
            with ZipFile(archive, "w") as package:
                package.writestr("file", b"large")
            with self.assertRaisesRegex(ValueError, "identity"):
                extract_vsix(archive, "0" * 64, root / "bad-hash")
            with self.assertRaisesRegex(ValueError, "limits"):
                extract_vsix(archive, sha256_file(archive), root / "large", max_bytes=1)
            with ZipFile(archive, "w") as package:
                link = ZipInfo("link")
                link.create_system = 3
                link.external_attr = 0o120777 << 16
                package.writestr(link, "target")
            with self.assertRaisesRegex(ValueError, "links"):
                extract_vsix(archive, sha256_file(archive), root / "link-output")


if __name__ == "__main__":
    unittest.main()
