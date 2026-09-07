import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from zipfile import ZipFile

from acquire_windows_components import acquire_components
from common import load_json_object, sha256_file


class ComponentAcquisitionTests(unittest.TestCase):
    def fixture(self, root):
        cache = root / "cache"
        cache.mkdir()
        archive = cache / "input.zip"
        with ZipFile(archive, "w") as package:
            package.writestr("Contents/include/vector", "fixture header")
            package.writestr("license.txt", "fixture license")
        digest = sha256_file(archive)
        size = archive.stat().st_size
        archive.replace(cache / digest)
        return cache, {"format": "triaevum_windows_toolchain_acquisition_v1",
                      "scope": "private_acquisition_not_public_redistribution",
                      "license_acceptance_required": True, "sdk_cabinets": {},
                      "payloads": [{"kind": "vsix", "fileName": "headers.vsix",
                                    "sha256": digest, "size": size,
                                    "url": "https://download.visualstudio.microsoft.com/fixture"}]}

    def test_cached_real_extraction_retains_license_and_receipt(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cache, plan = self.fixture(root)
            output = root / "output"
            events = []
            result = acquire_components(plan, cache, output, accept_licenses=True,
                                        progress=lambda *event: events.append(event))
            self.assertEqual(result, load_json_object(output / "components.json"))
            self.assertEqual(result["status"], "extracted_not_activated")
            self.assertEqual((output / "components/000/license.txt").read_text(), "fixture license")
            self.assertEqual([event[0] for event in events], ["download", "extract"])
            with self.assertRaisesRegex(ValueError, "new output"):
                acquire_components(plan, cache, output, accept_licenses=True)

    def test_consent_precedes_network_and_failure_does_not_publish(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cache, plan = self.fixture(root)
            output = root / "output"
            with patch("acquire_windows_components.acquire_payload") as download:
                with self.assertRaisesRegex(ValueError, "licenses"):
                    acquire_components(plan, cache, output, accept_licenses=False)
                download.assert_not_called()
            with patch("acquire_windows_components.extract_vsix", side_effect=ValueError("bad archive")):
                with self.assertRaisesRegex(ValueError, "bad archive"):
                    acquire_components(plan, cache, output, accept_licenses=True)
            self.assertFalse(output.exists())
            self.assertEqual(list(root.glob(".sdk-components-*")), [])
            self.assertTrue((cache / plan["payloads"][0]["sha256"]).is_file())
