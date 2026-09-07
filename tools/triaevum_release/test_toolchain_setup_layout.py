import tempfile
import unittest
from pathlib import Path

from common import atomic_write_json, sha256_file
from toolchain_setup_layout import setup_layout


class SetupLayoutTests(unittest.TestCase):
    def test_only_public_metadata_is_packaged(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            descriptor, plan_path, license_file = [root / name for name in (
                "toolchain-setup.json", "toolchain-plan.json", "toolchain-license.txt")]
            payload = {"sha256": "a" * 64, "size": 1,
                       "url": "https://download.visualstudio.microsoft.com/fixture"}
            plan = {"format": "triaevum_windows_toolchain_acquisition_v1",
                    "scope": "private_acquisition_not_public_redistribution",
                    "license_acceptance_required": True, "payloads": [payload],
                    "sdk_cabinets": {"fixture.cab": payload}}
            license_file.write_text("fixture license", encoding="utf-8")
            config = {"format": "triaevum_toolchain_setup_v1", "plan": plan_path.name,
                      "license": license_file.name, "license_sha256": sha256_file(license_file),
                      "clang_resource": "clang", "clang_version": "22", "msvc_version": "14.44.35207",
                      "sdk_version": "10.0.26100.0"}
            def write():
                atomic_write_json(plan_path, plan)
                config["plan_sha256"] = sha256_file(plan_path)
                atomic_write_json(descriptor, config)
            write()
            (root / "private-sdk.lib").write_bytes(b"private")
            files = setup_layout(descriptor)
            self.assertEqual([item["path"] for item in files], ["forge/" + name for name in (
                descriptor.name, plan_path.name, license_file.name)])
            plan["installed_metadata"] = [{"path": "C:/private"}]
            write()
            with self.assertRaisesRegex(ValueError, "private"):
                setup_layout(descriptor)
            del plan["installed_metadata"]
            config["clang_resource"] = "other"
            write()
            with self.assertRaisesRegex(ValueError, "bundled Clang"):
                setup_layout(descriptor)
