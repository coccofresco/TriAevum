import tempfile
import unittest
from pathlib import Path

from common import atomic_write_json, sha256_file
from windows_component_layout import component_trees


class ComponentLayoutTests(unittest.TestCase):
    def test_msi_file_ids_resolve_to_sdk_paths(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            name = "Windows Kits/10/Lib/10.0.26100.0/um/x64/kernel32.lib"
            path = root / "components/000" / name
            path.parent.mkdir(parents=True)
            path.write_bytes(b"library")
            receipt = {"format": "triaevum_windows_components_v1", "status": "extracted_not_activated",
                       "components": [{"path": "components/000", "payload": {"kind": "msi"},
                                       "extraction": {"files": {"opaque-msi-id": {
                                           "path": name, "bytes": 7, "sha256": sha256_file(path)}}}}]}
            atomic_write_json(root / "components.json", receipt)
            kwargs = {"msvc_version": "14.44.35207", "sdk_version": "10.0.26100.0"}
            self.assertEqual(component_trees(root, **kwargs),
                             [(path.parent, "sdk/Lib/10.0.26100.0/um/x64")])
            receipt["components"][0]["path"] = "../escaped"
            atomic_write_json(root / "components.json", receipt)
            with self.assertRaises(ValueError):
                component_trees(root, **kwargs)

    def test_maps_toolset_not_package_version_and_rejects_changed_inputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            name = "Contents/VC/Tools/MSVC/14.44.35207/include/vector"
            path = root / "components/000" / name
            path.parent.mkdir(parents=True)
            path.write_bytes(b"header")
            atomic_write_json(root / "components.json", {
                "format": "triaevum_windows_components_v1", "status": "extracted_not_activated",
                "components": [{"path": "components/000", "payload": {"kind": "vsix", "version": "14.44.35220"},
                                "extraction": {"files": {name: {"bytes": 6, "sha256": sha256_file(path)}}}}]})
            kwargs = {"msvc_version": "14.44.35207", "sdk_version": "10.0.26100.0"}
            self.assertEqual(component_trees(root, **kwargs), [(path.parent, "msvc/include")])
            with self.assertRaisesRegex(ValueError, "No supported"):
                component_trees(root, msvc_version="14.44.35220", sdk_version="10.0.26100.0")
            extra = path.parent / "extra"
            extra.write_bytes(b"extra")
            with self.assertRaisesRegex(ValueError, "Unindexed"):
                component_trees(root, **kwargs)
            extra.unlink()
            path.write_bytes(b"broken")
            with self.assertRaisesRegex(ValueError, "Changed"):
                component_trees(root, **kwargs)
