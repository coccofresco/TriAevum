import copy
import json
import os
import shutil
import tempfile
import unittest
from pathlib import Path

from precompiled_titles import checked_file
from release_platform import WINDOWS, LINUX
from shader_release_layout import bind_renderer_compiler
from shader_preparation import prepare_renderer_shader_cache


class ShaderReleaseLayoutTests(unittest.TestCase):
    def test_both_platforms_all_revisions_and_relocated_integrity(self):
        for platform in (WINDOWS, LINUX):
            with self.subTest(platform=platform.target), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                compiler = root / "compiler"
                compiler.write_bytes(b"compiler fixture")
                dep = root / ("shaderc_shared.dll" if platform == WINDOWS else "libshaderc_shared.so")
                dep.write_bytes(b"dependency fixture")
                seed = {"format": "private-fixture", "identity": "unchanged"}
                catalog = {"target": platform.target, "titles": [
                    {"recipe": "eur", "shader_preparation": seed}, {"recipe": "usa"}]}
                before = copy.deepcopy(catalog)
                result, files = bind_renderer_compiler(catalog, compiler, [dep])
                self.assertEqual(catalog, before)
                self.assertEqual(result["titles"][0]["shader_preparation"], seed)
                self.assertEqual(len(files), 2)
                moved = root / "relocated"
                for item in files:
                    dest = moved / item["path"]
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(item["source"], dest)
                contracts = [item["renderer_shader_preparation"] for item in result["titles"]]
                self.assertEqual(contracts[0], contracts[1])
                for record in [contracts[0]["compiler"], *contracts[0]["dependencies"]]:
                    checked_file(moved, record)
                (moved / contracts[0]["dependencies"][0]["path"]).write_bytes(b"broken")
                with self.assertRaisesRegex(ValueError, "integrity"):
                    checked_file(moved, contracts[0]["dependencies"][0])

    def test_missing_and_duplicate_dependencies_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            compiler = root / "compiler"
            compiler.write_bytes(b"fixture")
            catalog = {"target": WINDOWS.target, "titles": [{"recipe": "test"}]}
            with self.assertRaisesRegex(ValueError, "Invalid release artifact"):
                bind_renderer_compiler(catalog, compiler, [root / "missing.dll"])
            with self.assertRaisesRegex(ValueError, "Duplicate"):
                bind_renderer_compiler(catalog, compiler, [compiler, compiler])

    @unittest.skipUnless(os.environ.get("TRIAEVUM_TEST_SHADER_COMPILER"), "optional built shader compiler")
    def test_real_forge_preparation_after_package_relocation(self):
        compiler = Path(os.environ["TRIAEVUM_TEST_SHADER_COMPILER"])
        dependencies = [Path(path) for path in json.loads(
            os.environ.get("TRIAEVUM_TEST_SHADER_DEPENDENCIES", "[]"))]
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "relocated package"
            root.mkdir()
            catalog, items = bind_renderer_compiler({"target": WINDOWS.target if os.name == "nt" else LINUX.target,
                "titles": [{"recipe": "no-game-needed"}]}, compiler, dependencies)
            for item in items:
                target = root / item["path"]
                target.parent.mkdir(exist_ok=True)
                shutil.copy2(item["source"], target)
            receipts = []
            for attempt in range(2):
                if attempt:
                    # A harmless executable overlay changes the host identity,
                    # not shaderc. Cache reuse must survive relinks/packaging.
                    tool = root / catalog["titles"][0]["renderer_shader_preparation"]["compiler"]["path"]
                    with tool.open("ab") as stream:
                        stream.write(b"TriAevum compiler host identity regression fixture")
                    from precompiled_title_layout import artifact
                    contract = catalog["titles"][0]["renderer_shader_preparation"]
                    contract["compiler"] = artifact(tool, contract["compiler"]["path"])
                result = prepare_renderer_shader_cache(root=root, data_root=root / "data",
                    title=catalog["titles"][0], cache_directory=root / "data/cache/renderer")
                self.assertEqual(result["renderer_shader_preparation"], "complete", result)
                self.assertFalse(result["game_booted"])
                receipts.append(result)
            self.assertGreater(receipts[0]["compiled"], 0)
            self.assertEqual(receipts[1]["compiled"], 0)
            self.assertEqual(receipts[0]["modules"], receipts[1]["hits"])
            print("Relocated Forge shader preparation: " + json.dumps([
                {key: result[key] for key in ("modules", "compiled", "hits", "writes")}
                for result in receipts]))


if __name__ == "__main__":
    unittest.main()
