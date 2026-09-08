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
            process.wait.assert_called_once_with(timeout=75)


if __name__ == "__main__":
    unittest.main()
