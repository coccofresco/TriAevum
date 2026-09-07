import unittest
import tempfile
import hashlib
from pathlib import Path
from unittest.mock import patch

from msi_metadata import resolve_cabinet_payloads, file_layout
from plan_windows_toolchain import resolve_plan_cabinets


class MsiMetadataTests(unittest.TestCase):
    def test_layout_uses_target_long_names_and_components(self):
        directories = [{"Directory": "ROOT", "Directory_Parent": "", "DefaultDir": "SourceDir"},
                       {"Directory": "LIB", "Directory_Parent": "ROOT", "DefaultDir": "SHORT|Long Library:source"}]
        components = [{"Component": "C", "Directory_": "LIB"}]
        files = [{"File": "fileid", "Component_": "C", "FileName": "SHORT.LIB|library.lib", "FileSize": "10"}]
        self.assertEqual(file_layout(directories, components, files), {"fileid": {"path": "Long Library/library.lib", "bytes": 10}})
        directories[1]["Directory_Parent"] = "LIB"
        with self.assertRaisesRegex(ValueError, "Cyclic"):
            file_layout(directories, components, files)

    def test_layout_rejects_unsafe_and_duplicate_targets(self):
        directories = [{"Directory": "ROOT", "Directory_Parent": "", "DefaultDir": "SourceDir"}]
        components = [{"Component": "C", "Directory_": "ROOT"}]
        for name in ("../file", "C:drive", "NUL", "bad.", "bad "):
            with self.subTest(name=name), self.assertRaises(ValueError):
                file_layout(directories, components, [{"File": "id", "Component_": "C", "FileName": name, "FileSize": "1"}])
        files = [{"File": identifier, "Component_": "C", "FileName": name, "FileSize": "1"}
                 for identifier, name in (("one", "lib.h"), ("two", "LIB.h"))]
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            file_layout(directories, components, files)

    def setUp(self):
        self.payload = {"fileName": "Installers/payload.cab", "size": 10, "sha256": "a" * 64,
                        "url": "https://download.visualstudio.microsoft.com/payload.cab"}
        self.catalog = {"payload.cab": self.payload}

    def test_resolves_case_insensitively_and_deduplicates(self):
        self.assertEqual(resolve_cabinet_payloads(["Payload.cab", "payload.cab", "#embedded"], self.catalog), [self.payload])

    def test_rejects_missing_or_unsafe_cabinets(self):
        for name in ("missing.cab", "../payload.cab", "C:payload.cab", "x\\payload.cab", "payload.exe"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                resolve_cabinet_payloads([name], self.catalog)

    def test_rejects_catalog_alias_or_untrusted_url(self):
        for changed in ({"fileName": "other.cab"}, {"url": "https://example.com/payload.cab"}):
            with self.assertRaises(ValueError):
                resolve_cabinet_payloads(["payload.cab"], {"payload.cab": {**self.payload, **changed}})

    def test_plan_checks_msi_before_reading_metadata(self):
        content = b"synthetic MSI identity fixture"
        digest = hashlib.sha256(content).hexdigest()
        payload = {**self.payload, "fileName": "headers.msi", "kind": "msi",
                   "sha256": digest, "size": len(content)}
        plan = {"payloads": [payload], "sdk_cabinets": self.catalog,
                "license_acceptance_required": True}
        with tempfile.TemporaryDirectory() as temporary:
            cache = Path(temporary)
            local = cache / digest
            local.write_bytes(content)
            with patch("plan_windows_toolchain.read_cabinets", return_value=["payload.cab"]):
                resolved = resolve_plan_cabinets(plan, cache)
            self.assertEqual(resolved["required_cabinets"], [self.payload])
            self.assertTrue(resolved["license_acceptance_required"])
            self.assertNotIn("required_cabinets", plan)
            local.write_bytes(b"x" * len(content))
            with patch("plan_windows_toolchain.read_cabinets") as read:
                with self.assertRaisesRegex(ValueError, "verified"):
                    resolve_plan_cabinets(plan, cache)
                read.assert_not_called()


if __name__ == "__main__":
    unittest.main()
