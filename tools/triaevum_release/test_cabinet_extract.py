import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from cabinet_extract import extract_cabinet
from common import sha256_file
from msi_extract import extract_msi_package


@unittest.skipUnless(os.name == "nt", "Windows cabinet API")
class CabinetExtractTests(unittest.TestCase):
    def test_native_extraction_and_negative_contracts(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "fixture.txt"
            source.write_bytes(b"synthetic cabinet payload")
            cabinet = root / "fixture.cab"
            makecab = Path(os.environ["SystemRoot"]) / "System32/makecab.exe"
            subprocess.run([str(makecab), str(source), str(cabinet)], check=True,
                           capture_output=True, timeout=15)
            digest = sha256_file(cabinet)
            layout = {source.name: {"path": "sdk/include/renamed.h", "bytes": source.stat().st_size}}
            result = extract_cabinet(cabinet, root / "good", layout, digest)
            self.assertEqual(result[source.name]["sha256"], sha256_file(source))
            self.assertEqual((root / "good/sdk/include/renamed.h").read_bytes(), source.read_bytes())
            with self.assertRaisesRegex(ValueError, "new staging"):
                extract_cabinet(cabinet, root / "good", layout, digest)
            with self.assertRaisesRegex(ValueError, "identity mismatch"):
                extract_cabinet(cabinet, root / "bad-hash", layout, "0" * 64)
            self.assertFalse((root / "bad-hash").exists())
            with self.assertRaises(ValueError):
                extract_cabinet(cabinet, root / "escape", {source.name: {"path": "../outside", "bytes": 1}}, digest)
            with self.assertRaisesRegex(ValueError, "size differs"):
                extract_cabinet(cabinet, root / "bad-size", {source.name: {"path": "file", "bytes": 1}}, digest)
            with self.assertRaisesRegex(ValueError, "No expected"):
                extract_cabinet(cabinet, root / "missing", {"missing": {"path": "file", "bytes": 1}}, digest)

            msi = root / "fixture.msi"
            msi.write_bytes(b"synthetic MSI metadata supplied by the test")
            with (patch("msi_extract.read_table", return_value=[]),
                  patch("msi_extract.file_layout", return_value=layout),
                  patch("msi_extract.read_cabinets", return_value=[cabinet.name])):
                result = extract_msi_package(msi, sha256_file(msi), {cabinet.name: (cabinet, digest)}, root / "package")
                self.assertEqual(len(result["files"]), 1)
                layout["missing"] = {"path": "missing.h", "bytes": 1}
                with self.assertRaisesRegex(ValueError, "incomplete"):
                    extract_msi_package(msi, sha256_file(msi), {cabinet.name: (cabinet, digest)}, root / "incomplete-package")
                self.assertFalse((root / "incomplete-package").exists())
                self.assertEqual(list(root.glob(".sdk-extract-*")), [])


if __name__ == "__main__":
    unittest.main()
