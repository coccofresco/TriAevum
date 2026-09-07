import json
import shutil
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

from audit_release import audit_release
from common import atomic_write_json, load_json_object, sha256_file
from precompiled_title_layout import title_layout
from precompiled_titles import MODEL
from test_release_audit import write_clean_package


class PrecompiledReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        self.root = self.work / "package"
        self.root.mkdir()
        write_clean_package(self.root)
        self.manifest = load_json_object(self.root / "release-manifest.json")
        removed = {"forge_tool", "forge_link_library", "forge_tool_resource"}
        for item in self.manifest["files"]:
            if item["role"] in removed:
                (self.root / item["path"]).unlink()
        self.manifest["files"] = [item for item in self.manifest["files"] if item["role"] not in removed]
        self.recipe = {"id": "fixture", "inputs": {kind: {"sha256": str(i) * 64, "bytes": i}
            for i, kind in enumerate(("code", "exheader", "romfs"), 1)}}
        atomic_write_json(self.root / "recipes/oot3d.json", {"recipes": [self.recipe]})
        self.rehash("recipes/oot3d.json")
        plugin = self.work / "triaevum_title_aot.dll"
        plugin.write_bytes(b"MZ compiled fixture")
        build = {"format": "triaevum_generated_cpp_whole_aot_plugin_v2",
                 "profile": "x86_64-windows-thinlto-release-v1", "code_sha256": "1" * 64,
                 "program_sha256": "d" * 64, "plugin_sha256": sha256_file(plugin),
                 "plugin_bytes": plugin.stat().st_size, "translator_identity_sha256": "e" * 64,
                 "shards": 1, "functions": 1}
        self.build = self.work / "build.json"
        atomic_write_json(self.build, build)
        cpp = self.work / "body.cpp"
        cpp.write_text("void fixture() {}", encoding="ascii")
        self.generated = self.work / "generated.json"
        atomic_write_json(self.generated, {"format": "oot3d_whole_aot_cpp_v1", "code_sha256": "1" * 64,
            "program_sha256": "d" * 64, "shard_count": 1, "functions": [{}],
            "files": {"body.cpp": sha256_file(cpp)}})
        self.build_source = self.work / "build.zip"
        with zipfile.ZipFile(self.build_source, "w") as archive:
            archive.writestr("SOURCE_ARCHIVE_MANIFEST.json", json.dumps({"source_commit": "c" * 40}))
            archive.writestr("support.cpp", "void support() {}")
        with patch("precompiled_title_layout.query_product"):
            layout = self.layout()
        for item in layout:
            path = self.root / item["path"]
            path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(item["source"], path)
            self.manifest["files"].append({"path": item["path"], "role": item["role"]})
            self.rehash(item["path"])
        self.manifest["release"].update(contains_title_code=True, distribution_model=MODEL)
        self.save()

    def layout(self):
        return title_layout(runtime=self.root / "TriAevum.exe",
            native_module=self.root / "forge/oot3d_game_module.dll", plugin_manifest=self.build,
            generated_manifest=self.generated, build_source=self.build_source, recipe=self.recipe,
            work=self.work / "promoted")

    def rehash(self, relative):
        item = next(item for item in self.manifest["files"] if item["path"] == relative)
        path = self.root / relative
        item.update(bytes=path.stat().st_size, sha256=sha256_file(path))

    def save(self):
        atomic_write_json(self.root / "release-manifest.json", self.manifest)

    def test_explicit_title_code_and_sources_pass_without_compiler(self):
        result = audit_release(self.root)
        self.assertTrue(result.ok, result.errors)

    def test_title_code_cannot_be_hidden_as_neutral(self):
        self.manifest["release"]["contains_title_code"] = False
        self.save()
        self.assertFalse(audit_release(self.root).ok)

    def test_runtime_replaced_even_with_updated_package_hash_is_rejected(self):
        (self.root / "TriAevum.exe").write_bytes(b"wrong runtime")
        self.rehash("TriAevum.exe")
        self.save()
        self.assertTrue(any("integrity" in e for e in audit_release(self.root).errors))

    def test_generated_source_mismatch_rejected_before_publication(self):
        (self.work / "body.cpp").write_text("changed", encoding="ascii")
        with patch("precompiled_title_layout.query_product"), self.assertRaisesRegex(ValueError, "changed"):
            self.layout()

    def test_assets_still_rejected_inside_title_sources(self):
        relative = "source/titles/fixture-translated.zip"
        with zipfile.ZipFile(self.root / relative, "a") as archive:
            archive.writestr("code.bin", b"forbidden original executable")
        self.rehash(relative)
        self.save()
        self.assertTrue(any("code.bin" in e for e in audit_release(self.root).errors))

    def test_compiler_payloads_are_rejected(self):
        relative = "forge/clang-cl.exe"
        (self.root / relative).write_bytes(b"compiler")
        self.manifest["files"].append({"path": relative, "role": "forge_tool"})
        self.rehash(relative)
        self.save()
        self.assertTrue(any("compiler/SDK" in e for e in audit_release(self.root).errors))


if __name__ == "__main__":
    unittest.main()
