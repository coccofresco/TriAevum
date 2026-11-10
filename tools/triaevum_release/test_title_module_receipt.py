import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from precompiled_title_layout import generated_identity  # noqa: E402
from release_platform import WINDOWS_GNU  # noqa: E402
from title_module_receipt import write_receipt  # noqa: E402
from whole_aot_plugin_backend import FORMAT  # noqa: E402


def translated_tree(root: Path, *, functions=3, shards=2) -> Path:
    files = {}
    for name, text in (("oot3d_whole_aot_generated.cpp", "int f();\n"), ("oot3d_whole_aot_shard_000.cpp", "int g();\n")):
        (root / name).write_bytes(text.encode())
        files[name] = hashlib.sha256(text.encode()).hexdigest()
    manifest = root / "TITLE_SOURCE_MANIFEST.json"
    manifest.write_text(json.dumps({
        "format": "triaevum_translated_title_source_v1", "recipe": "mm3d-eur-local-6eec27b1",
        "build_source_commit": "a" * 40, "files": files,
        "build": {"code_sha256": "c" * 64, "program_sha256": "p" * 64, "shards": shards,
                  "functions": functions, "translator_identity_sha256": "t" * 64,
                  "target": "x86_64-pc-windows-msvc"}}), encoding="utf-8")
    return manifest


class TitleModuleReceiptTests(unittest.TestCase):
    def test_receipt_binds_module_and_translated_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = translated_tree(root)
            plugin = root / "build" / "triaevum_title_aot.dll"
            plugin.parent.mkdir()
            plugin.write_bytes(b"MZ module")
            receipt = write_receipt(plugin=plugin, translated_manifest=manifest, target=WINDOWS_GNU.target,
                                    compiler_identity="clang version 22.1.6", flags="-O2 -ffp-model=strict")
            written = json.loads((plugin.parent / "whole-aot-plugin.json").read_text(encoding="utf-8"))
            self.assertEqual(written, receipt)
            self.assertEqual(receipt["format"], FORMAT)
            self.assertEqual(receipt["profile"], WINDOWS_GNU.profile)
            self.assertEqual(receipt["plugin_bytes"], len(b"MZ module"))
            self.assertIsNone(receipt["profile_data_sha256"])
            loaded = json.loads(manifest.read_text(encoding="utf-8"))
            self.assertEqual(generated_identity(loaded), (receipt["code_sha256"], receipt["program_sha256"],
                                                          receipt["shards"], receipt["functions"]))

    def test_changed_source_or_wrong_module_name_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = translated_tree(root)
            plugin = root / "triaevum_title_aot.so"
            plugin.write_bytes(b"x")
            with self.assertRaisesRegex(ValueError, "named triaevum_title_aot.dll"):
                write_receipt(plugin=plugin, translated_manifest=manifest, target=WINDOWS_GNU.target,
                              compiler_identity="c", flags="f")
            plugin = root / "triaevum_title_aot.dll"
            plugin.write_bytes(b"x")
            (root / "oot3d_whole_aot_shard_000.cpp").write_text("changed", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "differs from its manifest"):
                write_receipt(plugin=plugin, translated_manifest=manifest, target=WINDOWS_GNU.target,
                              compiler_identity="c", flags="f")

    def test_generated_identity_of_generator_manifest_unchanged(self):
        generated = {"format": "oot3d_whole_aot_cpp_v1", "code_sha256": "c", "program_sha256": "p",
                     "shard_count": 2, "functions": [{}, {}, {}]}
        self.assertEqual(generated_identity(generated), ("c", "p", 2, 3))
        self.assertIsNone(generated_identity({"format": "other"}))


if __name__ == "__main__":
    unittest.main()
