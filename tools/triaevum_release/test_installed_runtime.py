import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

import forge
import forge_gui
from common import atomic_write_json, sha256_file
from installed_runtime import validate_installed_runtime
from activation_transaction import journal_path
from release_platform import host_platform, LINUX, WINDOWS


class InstalledRuntimeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.exe = self.root / host_platform().runtime
        self.exe.write_bytes(b"host")
        self.plugin = self.root / host_platform().title_module
        self.plugin.write_bytes(b"title")
        self.data = self.root / "custom-data"
        self.title = self.data / "titles" / "fixture"
        self.title.mkdir(parents=True)
        self.profile = self.root / "TriAevum.launch.json"
        arguments = []
        for option, path in (
            ("--a32-process-manifest", self.title / "process-manifest.json"),
            ("--config", self.data / "config" / "TriAevum.json"),
            ("--topscreen-config", self.data / "config" / "topscreen_ui.json"),
            ("--save-data", self.data / "savedata"),
        ):
            if option != "--save-data":
                atomic_write_json(path, {})
            arguments.extend((option, str(path)))
        atomic_write_json(self.profile, {
            "format": "oot3d_native_game_launch_profile_v1", "arguments": arguments,
        })
        self.receipt = {
            "status": "ready", "runtime_sha256": sha256_file(self.exe),
            "plugin_sha256": sha256_file(self.plugin),
            "launch_profile": str(self.profile),
            "launch_profile_sha256": sha256_file(self.profile),
        }

    def validate(self):
        return validate_installed_runtime(self.exe, self.title, self.data, self.receipt)

    def test_matching_installation_and_legacy_receipt(self):
        self.assertEqual(self.validate(), self.profile)
        del self.receipt["launch_profile_sha256"]
        self.assertEqual(self.validate(), self.profile)

    def test_rejects_receipt_from_another_platform(self):
        self.receipt["target"] = WINDOWS.target if host_platform() == LINUX else LINUX.target
        with self.assertRaisesRegex(ValueError, "another platform"):
            self.validate()

    def test_rejects_changed_plugin_and_profile(self):
        self.plugin.write_bytes(b"another title")
        with self.assertRaisesRegex(ValueError, "identity mismatch"):
            self.validate()
        self.plugin.write_bytes(b"title")
        atomic_write_json(self.profile, {})
        with self.assertRaisesRegex(ValueError, "profile changed"):
            self.validate()

    def test_rejects_wrong_data_folder(self):
        with self.assertRaisesRegex(ValueError, "another installation"):
            validate_installed_runtime(self.exe, self.title, self.root, self.receipt)

    def test_launcher_uses_selected_data_and_validated_profile(self):
        active = SimpleNamespace(directory=self.title)
        prepared = SimpleNamespace(state={"runtime": self.receipt})
        with (patch("forge_gui.runtime_path", return_value=self.exe),
              patch("forge_gui.load_active_title", return_value=active) as load,
              patch("forge_gui.forge.load_prepared_content", return_value=prepared),
              patch("forge_gui.subprocess.Popen") as launch):
            forge_gui.launch_runtime(self.data)
            load.assert_called_once_with(self.data / "active-title.json")
            self.assertEqual(launch.call_args.args[0],
                             [str(self.exe), "--launch-profile", str(self.profile)])
            self.plugin.write_bytes(b"wrong")
            launch.reset_mock()
            with self.assertRaises(forge.ForgeError):
                forge_gui.launch_runtime(self.data)
            launch.assert_not_called()

    def test_bad_config_does_not_replace_plugin(self):
        config = self.data / "config" / "TriAevum.json"
        config.write_text("broken", encoding="utf-8")
        incoming = self.root / "new.dll"
        incoming.write_bytes(b"replacement")
        prepared = SimpleNamespace(directory=self.title, state={})
        with (patch("forge.load_prepared_content", return_value=prepared),
              patch("forge.query_product", return_value={"product": {}})):
            with self.assertRaises(ValueError):
                forge.publish_private_runtime(
                    self.title, plugin=incoming, runtime_plugin=self.plugin,
                    launch_profile=self.profile, data_root=self.data,
                )
        self.assertEqual(self.plugin.read_bytes(), b"title")
        self.assertEqual(sha256_file(self.profile), self.receipt["launch_profile_sha256"])

    def test_separate_package_is_never_modified_and_user_files_survive_reimport(self):
        from common import load_json_object
        from installation_context import expand_profile_argument
        package = self.root / "immutable package"
        package.mkdir()
        executable = package / host_platform().runtime
        executable.write_bytes(b"read-only runtime")
        incoming = package / host_platform().title_module
        incoming.write_bytes(b"read-only title")
        before = {p.name: p.read_bytes() for p in package.iterdir()}
        profile = self.data / "activation" / "TriAevum.launch.json"
        marker = self.data / "savedata" / "keep.save"
        marker.parent.mkdir()
        marker.write_bytes(b"existing save")
        config = self.data / "config" / "TriAevum.json"
        atomic_write_json(config, {"user_setting": 42})
        config_before = config.read_bytes()
        prepared = SimpleNamespace(directory=self.title, state={})
        old_mode = package.stat().st_mode
        package.chmod(0o555)
        try:
            with (patch("forge.load_prepared_content", return_value=prepared),
                  patch("forge.ensure_runtime_config"),
                  patch("forge.query_product", return_value={
                      "product": {}, "runtime_sha256": sha256_file(executable)}) as query):
                for _ in range(2):
                    forge.publish_private_runtime(
                        self.title, plugin=incoming,
                        runtime_plugin=profile.parent / host_platform().title_module,
                        launch_profile=profile, data_root=self.data, package_root=package)
                    self.assertEqual(validate_installed_runtime(
                        executable, self.title, self.data, prepared.state["runtime"]), profile)
                self.assertTrue(all(call.args[0] == executable for call in query.call_args_list))
        finally:
            package.chmod(old_mode)
        arguments = [expand_profile_argument(arg, profile) for arg in load_json_object(profile)["arguments"]]
        self.assertEqual(Path(arguments[arguments.index("--resource-root") + 1]), package / "resources")
        self.assertEqual({p.name: p.read_bytes() for p in package.iterdir()}, before)
        self.assertEqual(marker.read_bytes(), b"existing save")
        self.assertEqual(config.read_bytes(), config_before)

    def test_texture_pack_is_published_and_verified_with_launch_profile(self):
        pack = self.data / "mods/topscreen/atlas_overrides.o3tu"
        pack.parent.mkdir(parents=True)
        pack.write_bytes(b"O3TU fixture")
        prepared = SimpleNamespace(directory=self.title, state={})
        with (patch("forge.load_prepared_content", return_value=prepared),
              patch("forge.ensure_runtime_config"),
              patch("forge.query_product", return_value={
                  "product": {}, "runtime_sha256": sha256_file(self.exe)})):
            forge.publish_private_runtime(
                self.title, plugin=self.plugin, runtime_plugin=self.plugin,
                launch_profile=self.profile, data_root=self.data, topscreen_texture_pack=pack)
        self.receipt = prepared.state["runtime"]
        self.assertEqual(self.validate(), self.profile)
        pack.write_bytes(b"corrupt")
        with self.assertRaisesRegex(ValueError, "TopScreen textures changed"):
            self.validate()

    def test_launcher_refuses_pending_activation_before_loading_title(self):
        atomic_write_json(journal_path(self.root), {})
        with (patch("forge_gui.runtime_path", return_value=self.exe),
              patch("forge_gui.load_active_title") as load,
              patch("forge_gui.subprocess.Popen") as launch):
            with self.assertRaisesRegex(forge.ForgeError, "interrupted"):
                forge_gui.launch_runtime(self.data)
            load.assert_not_called()
            launch.assert_not_called()

    def test_immutable_generation_must_be_selected_by_profile(self):
        import json
        generation = self.root / "private-plugins" / "hash" / "triaevum_title_aot.dll"
        generation.parent.mkdir(parents=True)
        generation.write_bytes(self.plugin.read_bytes())
        self.receipt["plugin"] = str(generation)
        del self.receipt["launch_profile_sha256"]
        with self.assertRaisesRegex(ValueError, "does not select"):
            self.validate()
        payload = json.loads(self.profile.read_text())
        payload["arguments"] += ["--title-plugin", str(generation)]
        atomic_write_json(self.profile, payload)
        self.assertEqual(self.validate(), self.profile)
        payload["arguments"] += ["--title-plugin", str(self.plugin)]
        atomic_write_json(self.profile, payload)
        with self.assertRaisesRegex(ValueError, "another title"):
            self.validate()


if __name__ == "__main__":
    unittest.main()
