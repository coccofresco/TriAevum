"""Offline checks: no donor checkout, SDK, ROM or connected device required."""
import hashlib
import json
from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
CONTROLS = ROOT / "ports/android/controls"
JAVA = CONTROLS / "src/main/java/org/triaevum/android/controls"


class AzaharOverlayImportTests(unittest.TestCase):
    def test_imported_files_match_recorded_donor_adaptation(self):
        manifest = json.loads((CONTROLS / "donor_manifest.json").read_text())
        self.assertEqual(manifest["license"], "GPL-2.0-or-later")
        self.assertEqual(len(manifest["snapshot_commit"]), 40)
        for relative, digest in manifest["outputs"].items():
            with self.subTest(path=relative):
                self.assertTrue((CONTROLS / relative).resolve().is_relative_to(CONTROLS.resolve()))
                self.assertEqual(hashlib.sha256((CONTROLS / relative).read_bytes()).hexdigest(), digest)

    def test_overlay_has_all_original_resources_without_emulator_dependencies(self):
        kotlin = "\n".join(path.read_text() for path in JAVA.glob("*.kt"))
        for forbidden in ("org.citra.citra_emu", "sEmulationActivity", "System.loadLibrary", "external fun"):
            self.assertNotIn(forbidden, kotlin)
        drawables = {path.stem for path in (CONTROLS / "src/main/res").glob("drawable*/*")}
        self.assertTrue(set(re.findall(r"R\.drawable\.(\w+)", kotlin)) <= drawables)
        values = ET.parse(CONTROLS / "src/main/res/values/azahar_overlay_integers.xml").getroot()
        self.assertEqual({item.get("name") for item in values}, set(re.findall(r"R\.integer\.(\w+)", kotlin)))

    def test_adapter_bits_match_shared_native_3ds_service(self):
        abi = (ROOT / "runtime/triaevum_module/include/triaevum/service_abi.h").read_text()
        expected = dict(re.findall(r"TRIAEVUM_INPUT_BUTTON_(\w+)_V1 = 1U << (\d+)U", abi))
        adapter = (JAVA / "AzaharInputAdapter.kt").read_text()
        found = dict(re.findall(r"AzaharButtonIds\.(\w+) -> (\d+)", adapter))
        found = {name.removeprefix("BUTTON_").removeprefix("TRIGGER_"): bit for name, bit in found.items()}
        self.assertEqual(found, expected)


if __name__ == "__main__":
    unittest.main()
