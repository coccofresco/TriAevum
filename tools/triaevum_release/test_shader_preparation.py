import copy
import os
import shutil
import struct
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

from common import atomic_write_json, load_json_object, sha256_file
from shader_preparation import FORMAT, prepare_shader_seed


def synthetic_pack(schema=3):
    return struct.pack("<8s4I", b"O3PSAOT\0", 1, schema, 1, 0) + bytes(56 + 20)


class ShaderPreparationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.data = self.root / "data"
        self.title = {"recipe": "test", "shader_preparation": {
            "format": FORMAT, "mode": "portable_pack", "descriptor_schema_version": 3,
            "pack": self.artifact("seed.o3ps", synthetic_pack())}}

    def artifact(self, name, data=b"fixture"):
        path = self.root / name
        path.write_bytes(data)
        return {"path": name, "bytes": len(data), "sha256": sha256_file(path)}

    def prepare(self):
        return prepare_shader_seed(root=self.root, data_root=self.data, title=self.title)

    def test_optional_off_never_runs_a_tool(self):
        with patch("shader_preparation._run") as run:
            self.assertIsNone(prepare_shader_seed(root=self.root, data_root=self.data, title={}))
            run.assert_not_called()

    def test_portable_pack_copy_reuse_and_corruption_repair(self):
        with patch("shader_preparation._run") as run:
            pack = self.prepare()
            self.assertEqual(pack.read_bytes(), synthetic_pack())
            before = pack.stat().st_mtime_ns
            self.assertEqual(pack, self.prepare())
            self.assertEqual(before, pack.stat().st_mtime_ns)
            pack.write_bytes(b"damaged")
            self.assertEqual(self.prepare().read_bytes(), synthetic_pack())
            run.assert_not_called()
            receipt = load_json_object(pack.parent / "preparation.json")
            self.assertEqual(receipt["device_pipeline_prewarm"], "not_performed")
            self.assertFalse(receipt["game_coverage_proven"])

    def test_bad_schema_integrity_and_path_are_rejected(self):
        original = copy.deepcopy(self.title)
        for change in (lambda seed: seed.update(descriptor_schema_version=4),
                       lambda seed: seed["pack"].update(sha256="0" * 64),
                       lambda seed: seed["pack"].update(path="../outside")):
            self.title = copy.deepcopy(original)
            change(self.title["shader_preparation"])
            with self.assertRaises(ValueError): self.prepare()

    def transferable(self):
        self.title["shader_preparation"] = {
            "format": FORMAT, "mode": "citra_transferable", "descriptor_schema_version": 3,
            "dialect": "citra-legacy-v1", "caches": [self.artifact("cache.bin")],
            "importer": self.artifact("importer"), "compiler": self.artifact("compiler"),
            "dependencies": [self.artifact("shaderc.so")], "inventories": []}

    def fake_run(self, command, root):
        self.assertEqual(root, self.root)
        if "--output" in command:
            atomic_write_json(Path(command[command.index("--output") + 1]),
                              {"transferable_import": {"complete_import": True}})
        else:
            Path(command[command.index("--pack") + 1]).write_bytes(synthetic_pack())

    def test_translates_once_without_title_build_and_invalidates_on_dependency(self):
        self.transferable()
        with patch("shader_preparation._run", side_effect=self.fake_run) as run:
            first = self.prepare()
            self.assertEqual(run.call_count, 2)
            self.assertEqual(first, self.prepare())
            self.assertEqual(run.call_count, 2)
            self.title["shader_preparation"]["dependencies"] = [self.artifact("shaderc.so", b"new")]
            self.assertNotEqual(first, self.prepare())
            self.assertEqual(run.call_count, 4)

    def test_partial_import_is_not_activated(self):
        self.transferable()
        def partial(command, root):
            atomic_write_json(Path(command[command.index("--output") + 1]),
                              {"transferable_import": {"complete_import": False}})
        with patch("shader_preparation._run", side_effect=partial) as run:
            with self.assertRaisesRegex(ValueError, "incomplete"): self.prepare()
            self.assertEqual(run.call_count, 1)
        self.assertEqual(list(self.data.rglob("portable.o3ps")), [])

    def test_dialect_must_be_explicit(self):
        self.transferable()
        del self.title["shader_preparation"]["dialect"]
        with self.assertRaisesRegex(ValueError, "dialect"): self.prepare()

    @unittest.skipUnless(all(os.environ.get(key) for key in (
        "TRIAEVUM_TEST_CITRA_CACHE", "TRIAEVUM_TEST_CACHE_IMPORTER", "TRIAEVUM_TEST_SHADER_COMPILER")),
        "optional real shader-tool qualification")
    def test_real_native_tools(self):
        def artifact_from_file(variable, name):
            target = self.root / name
            shutil.copy2(Path(os.environ[variable]), target)
            return {"path": name, "bytes": target.stat().st_size, "sha256": sha256_file(target)}
        self.title["shader_preparation"] = {
            "format": FORMAT, "mode": "citra_transferable", "descriptor_schema_version": 3,
            "dialect": "citra-legacy-v1",
            "caches": [artifact_from_file("TRIAEVUM_TEST_CITRA_CACHE", "cache.bin")],
            "importer": artifact_from_file("TRIAEVUM_TEST_CACHE_IMPORTER", "importer"),
            "compiler": artifact_from_file("TRIAEVUM_TEST_SHADER_COMPILER", "compiler")}
        start = time.perf_counter()
        pack = self.prepare()
        cold = time.perf_counter() - start
        start = time.perf_counter()
        self.assertEqual(pack, self.prepare())
        warm = time.perf_counter() - start
        receipt = load_json_object(pack.parent / "preparation.json")
        self.assertGreater(receipt["modules"], 0)
        print(f"Forge real shader preparation: modules={receipt['modules']} cold={cold:.3f}s reuse={warm:.3f}s")


if __name__ == "__main__":
    unittest.main()
