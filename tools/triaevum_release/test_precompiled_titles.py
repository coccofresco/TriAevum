import copy
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from types import SimpleNamespace

from common import atomic_write_json, sha256_file
from precompiled_titles import CATALOG, FORMAT, MODEL, checked_file, load_catalog, select_title, install_precompiled_title
from forge_gui import InstallRequest, install_private_title
from release_platform import host_platform


class PrecompiledTitleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.platform = host_platform()
        def artifact(name):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(name.encode())
            return {"path": name, "bytes": path.stat().st_size, "sha256": sha256_file(path)}
        self.recipe = {"id": "test", "inputs": {
            kind: {"sha256": str(i) * 64, "bytes": i} for i, kind in enumerate(("code", "exheader", "romfs"), 1)}}
        self.catalog = {"format": FORMAT, "install_model": MODEL, "target": self.platform.target,
                        "runtime": artifact(self.platform.runtime), "native_module": artifact(self.platform.native_module),
                        "titles": [{"recipe": "test", "inputs": self.recipe["inputs"], "abi_version": 2,
                                    "target": self.platform.target, "translator_identity_sha256": "a" * 64,
                                    "plugin": artifact("titles/test/contract.dll")}]}
        atomic_write_json(self.root / CATALOG, self.catalog)

    def test_selection_and_relocation(self):
        self.assertEqual(select_title(self.root, self.recipe)["recipe"], "test")
        moved = self.root / "relocated"
        import shutil
        source = self.root / "copy"
        source.mkdir()
        for name in (self.platform.runtime, "forge", "titles", "recipes"):
            item = self.root / name
            if item.is_dir():
                shutil.copytree(item, source / name)
            else:
                shutil.copy2(item, source / name)
        source.rename(moved)
        select_title(moved, self.recipe)

    def test_corruption_runtime_and_revision_rejected(self):
        for record in (self.catalog["runtime"], self.catalog["native_module"], self.catalog["titles"][0]["plugin"]):
            path = self.root / record["path"]
            original = path.read_bytes()
            path.write_bytes(original + b"changed")
            with self.assertRaisesRegex(ValueError, "integrity"):
                select_title(self.root, self.recipe)
            path.write_bytes(original)
        recipe = copy.deepcopy(self.recipe)
        recipe["inputs"]["code"]["sha256"] = "b" * 64
        with self.assertRaisesRegex(ValueError, "revision/ABI"):
            select_title(self.root, recipe)

    def test_missing_catalog_never_falls_back_to_compilation(self):
        (self.root / CATALOG).unlink()
        with patch("forge_gui.runtime_path", return_value=self.root / "TriAevum.exe"), \
                patch("forge_gui.forge.query_product"), \
                patch("forge_gui.forge.probe_toolchain") as probe, \
                patch("forge_gui.forge.build_private_title") as build, \
                patch("forge_gui.ctr_rom.extract_decrypted_rom") as extract:
            with self.assertRaises(OSError):
                install_private_title(InstallRequest(self.root / "rom.cci", self.root / "data"))
            probe.assert_not_called()
            build.assert_not_called()
            extract.assert_not_called()

    def test_unbound_content_family_cannot_bypass_catalog(self):
        self.recipe['data_compatibility'] = {'format': 'unbound'}
        with self.assertRaisesRegex(ValueError, 'content family differs'):
            select_title(self.root, self.recipe)

    def test_duplicate_and_path_escape_rejected(self):
        self.catalog["titles"] *= 2
        atomic_write_json(self.root / CATALOG, self.catalog)
        with self.assertRaisesRegex(ValueError, "duplicate"):
            load_catalog(self.root)
        with self.assertRaises(ValueError):
            checked_file(self.root, {"path": "../outside.dll"})

    def test_install_prepares_before_activation_and_routes_same_cache_to_game(self):
        title_directory = self.root / "data/titles/test"
        title_directory.mkdir(parents=True)
        prepared = SimpleNamespace(directory=title_directory, index={"recipe": "test"}, inputs={
            kind: SimpleNamespace(sha256=value["sha256"], bytes=value["bytes"], path=self.root / kind)
            for kind, value in self.recipe["inputs"].items()})
        pack = self.root / "data/shader-seeds/test/portable.o3ps"
        order = []
        def step(name, result):
            def invoke(*args, **kwargs):
                order.append(name)
                return result
            return invoke
        with patch("forge.load_prepared_content", return_value=prepared), \
                patch("device_pipeline_preparation.adopt_existing_cache"), \
                patch("topscreen_assets.prepare_topscreen_assets", return_value=None), \
                patch("shader_preparation.prepare_shader_seed", side_effect=step("shaders", pack)), \
                patch("shader_preparation.prepare_renderer_shader_cache",
                      side_effect=step("renderer", {"renderer_shader_preparation": "complete"})) as renderer, \
                patch("device_pipeline_preparation.prepare_device_pipelines",
                      side_effect=step("pipelines", {"device_pipeline_prewarm": "complete"})) as device, \
                patch("forge.package_private_module", side_effect=step("package", {})), \
                patch("forge.publish_private_runtime", side_effect=step("publish", {})) as publish, \
                patch("forge.activate_prepared_title", side_effect=step("activate", {"active_title": "test"})):
            result = install_precompiled_title(title_directory, root=self.root, recipe=self.recipe,
                data_root=self.root / "data", runtime_plugin=self.root / self.platform.title_module,
                launch_profile=self.root / "TriAevum.launch.json", active_title_state=self.root / "data/active-title.json")
        self.assertEqual(order, ["shaders", "renderer", "pipelines", "package", "publish", "activate"])
        cache = (self.root / "data/cache/renderer").resolve()
        self.assertEqual(device.call_args.kwargs["cache_directory"], cache)
        self.assertEqual(renderer.call_args.kwargs["cache_directory"], cache)
        self.assertEqual(publish.call_args.kwargs["renderer_cache_directory"], cache)
        self.assertEqual(publish.call_args.kwargs["pica_shader_pack"], pack)
        self.assertEqual(result["objects_compiled"], 0)


if __name__ == "__main__":
    unittest.main()
