import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch, MagicMock

import probe_renderer


class ProbeRendererTests(unittest.TestCase):
    def test_probe_isolates_configuration_and_persistent_outputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            install = root / "installation"
            install.mkdir()
            executable = install / "runtime"
            executable.touch()
            original = '{"Graphics":{"Effects":{"Reflections":{"Mode":"Off"}}}}'
            (install / "config.json").write_text(original)
            (install / "top.json").write_text("{}")
            (install / "TriAevum.launch.json").write_text(json.dumps({"arguments": [
                "--config", "${profile_dir}/config.json", "--topscreen-config", "${profile_dir}/top.json",
                "--save-data", "${profile_dir}/savedata", "--output", "${profile_dir}/runtime.json"]}))
            output = root / "probe"
            process = MagicMock()
            process.wait.return_value = 0
            argv = ["probe_renderer", str(install), str(executable), str(output),
                    "--reflections", "FidelityFXSSSR", "--material-hash", "123456789abcdef0"]
            with patch.object(sys, "argv", argv), patch.object(probe_renderer.subprocess, "Popen", return_value=process) as launch:
                with self.assertRaises(SystemExit) as stopped:
                    probe_renderer.main()
                self.assertEqual(stopped.exception.code, 0)
            self.assertEqual((install / "config.json").read_text(), original)
            command = launch.call_args.args[0]
            self.assertEqual(command[command.index("--save-data") + 1], (output / "savedata").as_posix())
            self.assertEqual(command[command.index("--config") + 1], (output / "config.json").as_posix())
            reflection = json.loads((output / "config.json").read_text())["Graphics"]["Effects"]["Reflections"]
            self.assertEqual(reflection["Mode"], "FidelityFXSSSR")
            self.assertEqual(reflection["Materials"][0]["Target"]["ContentHash"], "123456789abcdef0")
            self.assertEqual(launch.call_args.kwargs["env"]["TRIAEVUM_RENDERER_CACHE_DIR"], str(output / "cache"))
            process.wait.assert_called_once_with(timeout=75)

    def test_native_probe_replaces_timing_and_reuses_only_explicit_cache(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "runtime").touch()
            (root / "pack.o3ps").touch()
            (root / "config.json").write_text('{"Graphics":{"Preset":"Toon"}}')
            (root / "TriAevum.launch.json").write_text(json.dumps({"arguments": [
                "--config", "${profile_dir}/config.json", "--gameplay-timing", "native30_interpolated",
                "--presentation-rate", "90", "--frames", "100", "--max-seconds", "600"]}))
            argv = ["probe_renderer", str(root), str(root / "runtime"), str(root / "probe"),
                    "--native-fidelity", "--frames", "360", "--shader-pack", str(root / "pack.o3ps"),
                    "--cache-directory", str(root / "prepared")]
            process = MagicMock()
            process.wait.return_value = 0
            with patch.object(sys, "argv", argv), patch.object(probe_renderer.subprocess, "Popen", return_value=process) as launch:
                with self.assertRaises(SystemExit) as stopped:
                    probe_renderer.main()
                self.assertEqual(stopped.exception.code, 0)
            command = launch.call_args.args[0]
            for option, value in {"--gameplay-timing": "native30_no_interpolation",
                                  "--presentation-rate": "30", "--frames": "360", "--max-seconds": "45",
                                  "--pica-aot-shader-pack": (root / "pack.o3ps").as_posix()}.items():
                self.assertEqual(command.count(option), 1)
                self.assertEqual(command[command.index(option) + 1], value)
            self.assertEqual(json.loads((root / "probe/config.json").read_text())["Graphics"]["Preset"], "Authentic")
            self.assertEqual(launch.call_args.kwargs["env"]["TRIAEVUM_RENDERER_CACHE_DIR"], str(root / "prepared"))


if __name__ == "__main__":
    unittest.main()
