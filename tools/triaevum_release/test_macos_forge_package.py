import hashlib
import json
from pathlib import Path
import plistlib
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from common import atomic_write_json
from macos_forge_package import rebind_translated, reference_files
from macos_macho_audit import bundle_errors, EXTERNAL_FILES, FORMAT, RECEIPT, LAUNCHER, launcher_identity, MACHO_ROLES
from precompiled_title_layout import artifact
from test_android_forge_package import Fixture
from test_macos_macho_audit import macho


class MacOSPublisherTests(unittest.TestCase):
    def test_reference_manifest_may_live_in_docs_without_copying_foreign_binaries(self):
        with tempfile.TemporaryDirectory() as temporary:
            fixture = Fixture(Path(temporary))
            manifest = fixture.reference / "release-manifest.json"
            (fixture.reference / "docs").mkdir()
            manifest.rename(fixture.reference / "docs/release-manifest.json")
            selected = reference_files(fixture.reference)
            self.assertIn("recipes/oot3d.json", selected)
            self.assertIn("LICENSE", selected)
            self.assertNotIn("TriAevum.exe", selected)
            self.assertNotIn("forge/oot3d_native_pica_aot_compiler.exe", selected)

    def test_rebinding_checks_translation_bytes_and_binds_new_signed_module(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fixture = Fixture(root)
            source = fixture.reference / fixture.title["sources"][0]["path"]
            with zipfile.ZipFile(source) as archive:
                metadata = json.loads(archive.read("TITLE_SOURCE_MANIFEST.json"))
            content = b"// synthetic translated C++ fixture\n"
            metadata["files"] = {"function.cpp": hashlib.sha256(content).hexdigest()}
            with zipfile.ZipFile(source, "w") as archive:
                archive.writestr("TITLE_SOURCE_MANIFEST.json", json.dumps(metadata))
                archive.writestr("function.cpp", content)
            fixture.title["sources"] = [artifact(source, source.relative_to(fixture.reference).as_posix())]
            plugin = {"sha256": "a" * 64, "bytes": 321, "path": "titles/fixture/triaevum_title_aot.dylib"}
            destination = root / "new-translated.zip"
            rebind_translated(fixture.reference, fixture.title, destination, plugin, "b" * 40)
            with zipfile.ZipFile(destination) as archive:
                result = json.loads(archive.read("TITLE_SOURCE_MANIFEST.json"))
                self.assertEqual(archive.read("function.cpp"), content)
            self.assertEqual(result["build"]["plugin_sha256"], plugin["sha256"])
            self.assertEqual(result["build_source_commit"], "b" * 40)
            self.assertEqual(result["build"]["translator_identity_sha256"], "3" * 64)
            metadata["files"]["function.cpp"] = "0" * 64
            with zipfile.ZipFile(source, "w") as archive:
                archive.writestr("TITLE_SOURCE_MANIFEST.json", json.dumps(metadata))
                archive.writestr("function.cpp", content)
            fixture.title["sources"] = [artifact(source, source.relative_to(fixture.reference).as_posix())]
            with self.assertRaisesRegex(ValueError, "inventory mismatch"):
                rebind_translated(fixture.reference, fixture.title, destination, plugin, "b" * 40)

    def test_bundle_audit_covers_outer_files_and_minos(self):
        with tempfile.TemporaryDirectory() as temporary:
            app = Path(temporary) / "TriAevum.app"
            root = app / "Contents/Resources"
            root.mkdir(parents=True)
            for relative in EXTERNAL_FILES:
                path = app / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                if relative.endswith(".dylib"):
                    macho(path, kind=6)
                elif path.name == LAUNCHER:
                    macho(path)
            plist = {"CFBundleExecutable": LAUNCHER, "CFBundlePackageType": "APPL",
                     "LSMinimumSystemVersion": "14.0", "NSHighResolutionCapable": True}
            (app / "Contents/Info.plist").write_bytes(plistlib.dumps(plist))
            atomic_write_json(root / "config/vulkan/MoltenVK_icd.json", {
                "file_format_version": "1.0.0", "ICD": {"library_path": "../../../Frameworks/libMoltenVK.dylib",
                                                          "is_portability_driver": True}})
            seal = app / "Contents/_CodeSignature/CodeResources"
            seal.parent.mkdir()
            seal.write_bytes(b"synthetic signature (not executed)")
            macho(root / "TriAevum")
            declared = {"TriAevum": {"role": "runtime_executable"}}
            release = {"minimum_macos": "14.0", "forge_profile": "rust"}

            def receipt():
                records = [artifact(app / name, name) for name in sorted(EXTERNAL_FILES)]
                for item in records:
                    if item["path"] == "Contents/MacOS/" + LAUNCHER:
                        item.update(launcher_identity(app / item["path"]))
                atomic_write_json(root / RECEIPT, {"format": FORMAT, "minimum_macos": "14.0",
                    "signing": "ad-hoc", "notarized": False,
                    "files": records})

            receipt()
            with patch("macos_macho_audit.sys.platform", "linux"):
                self.assertEqual(list(bundle_errors(root, declared, release)), [])
                # Every native role, each Framework and the outer launcher must
                # fail the privacy gate, without printing the synthetic name.
                leak = b"/Users/synthetic-private-builder/project/source.cpp"
                targets = [(root / "privacy-fixture", role) for role in MACHO_ROLES]
                targets += [(app / name, None) for name in EXTERNAL_FILES
                            if name.endswith(".dylib") or name.endswith(LAUNCHER)]
                for path, role in targets:
                    with self.subTest(path=path.name, role=role):
                        if role:
                            macho(path)
                            declared["privacy-fixture"] = {"role": role}
                        original = path.read_bytes()
                        if path.name == LAUNCHER:
                            # Grow the hashed pre-signature section, keeping the
                            # launcher's signature-last contract intact.
                            import struct
                            data = bytearray(original[:-16] + leak + original[-16:])
                            struct.pack_into("<I", data, 64, len(data) - 16)
                            path.write_bytes(data)
                        else:
                            path.write_bytes(original + leak)
                        receipt()
                        errors = list(bundle_errors(root, declared, release))
                        self.assertTrue(any("build-machine path" in error for error in errors), errors)
                        self.assertNotIn("synthetic-private-builder", "\n".join(errors))
                        path.write_bytes(original)
                        if role:
                            del declared["privacy-fixture"]
                            path.unlink()
                receipt()
                hidden = root / "resources/pretend-asset.dat"
                hidden.parent.mkdir(exist_ok=True)
                for magic in ("feedface", "cefaedfe", "feedfacf", "cffaedfe",
                              "cafebabe", "bebafeca", "cafebabf", "bfbafeca", "7f454c46", "4d5a0000"):
                    for role in ("documentation", "title_neutral_resource"):
                        with self.subTest(magic=magic, role=role):
                            hidden.write_bytes(bytes.fromhex(magic) + bytes(60))
                            declared["resources/pretend-asset.dat"] = {"role": role}
                            self.assertTrue(any("Undeclared native-code role" in error
                                                for error in bundle_errors(root, declared, release)))
                hidden.unlink()
                del declared["resources/pretend-asset.dat"]
                extra = app / "Contents/MacOS/private.cci"
                extra.write_bytes(b"synthetic forbidden extra")
                self.assertTrue(any("allowlist mismatch" in error for error in bundle_errors(root, declared, release)))
                extra.unlink()
                macho(app / "Contents/Frameworks/libMoltenVK.dylib", kind=6, minimum=0x000F0000)
                receipt()
                self.assertTrue(any("requires macOS" in error for error in bundle_errors(root, declared, release)))


if __name__ == "__main__":
    unittest.main()
