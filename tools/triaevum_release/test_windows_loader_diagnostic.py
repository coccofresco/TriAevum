"""Exercise the user-facing diagnostic under stock Windows PowerShell."""
import json
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


def import_fixture(symbols):
    # Minimal PE32+ import table, never executed or loaded as code.
    data = bytearray(4096)
    struct.pack_into("<H", data, 0, 0x5A4D)
    struct.pack_into("<I", data, 0x3C, 0x80)
    struct.pack_into("<I", data, 0x80, 0x4550)
    struct.pack_into("<HH", data, 0x84, 0x8664, 0)
    struct.pack_into("<H", data, 0x94, 240)
    struct.pack_into("<H", data, 0x98, 0x20B)
    struct.pack_into("<I", data, 0x98 + 60, len(data))
    struct.pack_into("<I", data, 0x98 + 120, 0x200)
    struct.pack_into("<IIIII", data, 0x200, 0x300, 0, 0, 0x280, 0x300)
    data[0x280:0x28D] = b"kernel32.dll\0"
    for index, symbol in enumerate(symbols):
        rva = 0x500 + index * 128
        struct.pack_into("<Q", data, 0x300 + index * 8, rva)
        encoded = symbol.encode("ascii") + b"\0"
        data[rva + 2:rva + 2 + len(encoded)] = encoded
    return data


@unittest.skipUnless(sys.platform == "win32" and shutil.which("powershell.exe"),
                     "requires Windows PowerShell")
class WindowsLoaderDiagnosticTests(unittest.TestCase):
    def probe(self, image):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "TriAevum.exe").write_bytes(image)
            output = root / "report.json"
            result = subprocess.run([
                "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass",
                "-File", str(Path(__file__).with_name("diagnose_windows_loader.ps1")),
                "-Installation", str(root), "-Output", str(output),
            ], capture_output=True, text=True, timeout=30)
            self.assertTrue(output.is_file(), result.stderr)
            return result.returncode, json.loads(output.read_text(encoding="utf-8-sig"))

    def test_known_export(self):
        code, report = self.probe(import_fixture(["Sleep"]))
        self.assertEqual(code, 0, report)
        self.assertEqual(report["problems"], [])
        self.assertEqual(report["modules"][0]["imports"][0]["checked"], 1)

    def test_missing_export_names_exact_importer_provider_and_symbol(self):
        code, report = self.probe(import_fixture(["Sleep", "TriAevumMissingExportFixture"]))
        self.assertEqual(code, 1)
        self.assertEqual(report["problems"], [
            "TriAevum.exe requires kernel32.dll!TriAevumMissingExportFixture (not resolved)"
        ])

    def test_corrupt_image_produces_incomplete_report_not_success(self):
        code, report = self.probe(b"not a PE file")
        self.assertEqual(code, 1)
        self.assertIn("Diagnostic incomplete", report["problems"][0])
