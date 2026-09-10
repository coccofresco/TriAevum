import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from common import atomic_write_json, sha256_file
from shader_preparation import FORMAT, RENDERER_FORMAT, RENDERER_CONTRACT, prepare_renderer_shader_cache, prepare_shader_seed


class RendererShaderPreparationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        compiler = self.root / "compiler"
        compiler.write_bytes(b"fixture")
        self.title = {"shader_preparation": {"format": FORMAT,
            "compiler": {"path": "compiler", "bytes": 7, "sha256": sha256_file(compiler)}}}
        self.cache = self.root / "data/cache/renderer"

    def prepare(self):
        return prepare_renderer_shader_cache(root=self.root, data_root=self.root / "data",
                                              title=self.title, cache_directory=self.cache)

    def complete(self, command, root):
        self.assertEqual(root, self.root)
        self.assertEqual(command[1:3], ["--prepare-renderer-cache", str(self.cache)])
        self.assertNotIn("--pack", command)
        atomic_write_json(Path(command[command.index("--manifest") + 1]), {
            "format": RENDERER_FORMAT, "modules": 22, "hits": 22, "compiled": 0,
            "compile_failed": 0, "write_failed": 0, "writes": 0,
            "cache_directory": str(self.cache), "game_booted": False})

    def test_prepare_before_launch_rechecks_cache_without_title_or_gpu_boot(self):
        with patch("shader_preparation._run", side_effect=self.complete) as run:
            self.assertEqual(self.prepare()["renderer_shader_preparation"], "complete")
            self.assertEqual(self.prepare()["hits"], 22)
            self.assertEqual(run.call_count, 2)

    def test_older_catalog_without_compiler_is_optional(self):
        self.title = {}
        with patch("shader_preparation._run") as run:
            self.assertIsNone(self.prepare())
            run.assert_not_called()

    def test_public_renderer_contract_without_private_seed(self):
        seed = self.title.pop("shader_preparation")
        seed["format"] = RENDERER_CONTRACT
        self.title["renderer_shader_preparation"] = seed
        with patch("shader_preparation._run", side_effect=self.complete) as run:
            self.assertIsNone(prepare_shader_seed(root=self.root, data_root=self.root / "data", title=self.title))
            self.assertEqual(self.prepare()["renderer_shader_preparation"], "complete")
            self.assertEqual(run.call_count, 1)

    def test_explicit_renderer_contract_wins_over_legacy_seed(self):
        self.title["renderer_shader_preparation"] = {
            **self.title["shader_preparation"], "format": RENDERER_CONTRACT}
        self.title["shader_preparation"] = {"format": "not-a-renderer-compiler"}
        with patch("shader_preparation._run", side_effect=self.complete):
            self.assertEqual(self.prepare()["renderer_shader_preparation"], "complete")

    def test_invalid_artifact_fails_before_tool(self):
        (self.root / "compiler").write_bytes(b"changed")
        with patch("shader_preparation._run") as run:
            with self.assertRaises(ValueError):
                self.prepare()
            run.assert_not_called()

    def test_failed_or_misdirected_cache_is_not_reported_complete(self):
        with patch("shader_preparation._run", side_effect=ValueError("old tool")):
            self.assertEqual(self.prepare()["renderer_shader_preparation"], "failed")
        def wrong_cache(command, root):
            self.complete(command, root)
            manifest = Path(command[command.index("--manifest") + 1])
            import json
            result = json.loads(manifest.read_text())
            result["cache_directory"] = str(self.root / "wrong-cache")
            atomic_write_json(manifest, result)
        with patch("shader_preparation._run", side_effect=wrong_cache):
            self.assertEqual(self.prepare()["renderer_shader_preparation"], "failed")


if __name__ == "__main__":
    unittest.main()
