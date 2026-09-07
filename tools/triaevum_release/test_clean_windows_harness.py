import os
from pathlib import Path
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.name == "nt", "Windows PowerShell qualification harness")
class CleanWindowsHarnessTests(unittest.TestCase):
    def test_preflight_never_overwrites_or_recursively_copies_inputs(self):
        script = Path(__file__).with_name("qualify_clean_windows.ps1").resolve()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / "package"
            package.mkdir()
            rom = root / "title.cci"
            rom.write_bytes(b"fixture")
            for name in ("TriAevum.exe", "TriAevumForge.exe", "release-manifest.json"):
                (package / name).write_bytes(b"fixture")
            existing = root / "existing"
            existing.mkdir()
            marker = existing / "keep"
            marker.write_bytes(b"keep")
            for output, reason in ((existing, "Output must not exist"),
                                   (package / "nested", "outside the input directories")):
                with self.subTest(output=output):
                    result = subprocess.run([
                        "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass",
                        "-File", str(script), "-Package", str(package), "-Rom", str(rom),
                        "-Output", str(output),
                    ], capture_output=True, text=True, timeout=20)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(reason, result.stderr)
            self.assertEqual(marker.read_bytes(), b"keep")
            self.assertFalse((package / "nested").exists())
