import json
from pathlib import Path
import tempfile
import unittest

from prepare_linux_launch import prepare


class LinuxLaunchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.plugin = self.root / "title.so"
        self.plugin.write_bytes(b"fixture")
        self.original = self.root / "TriAevum.launch.json"
        self.profile = {
            "format": "oot3d_native_game_launch_profile_v1",
            "arguments": ["--title-plugin", "windows.dll", "--ui-profile", "topscreen"],
            "path_scopes": {"--title-plugin": "relative"},
        }

    def save(self):
        self.original.write_text(json.dumps(self.profile), encoding="utf-8")

    def test_preserves_original_and_other_arguments(self):
        self.save()
        before = self.original.read_bytes()
        result = json.loads(prepare(self.root, self.plugin).read_text())
        self.assertEqual(self.original.read_bytes(), before)
        self.assertEqual(result["arguments"], ["--title-plugin", str(self.plugin.resolve()),
                                              "--ui-profile", "topscreen"])

    def test_rejects_duplicate_plugin(self):
        self.profile["arguments"] += ["--title-plugin", "other.dll"]
        self.save()
        with self.assertRaisesRegex(ValueError, "exactly one"):
            prepare(self.root, self.plugin)

    def test_rejects_missing_plugin_value(self):
        self.profile["arguments"] = ["--title-plugin"]
        self.save()
        with self.assertRaisesRegex(ValueError, "Missing"):
            prepare(self.root, self.plugin)

    def test_rejects_missing_library(self):
        self.save()
        with self.assertRaises(FileNotFoundError):
            prepare(self.root, self.root / "missing.so")


if __name__ == "__main__":
    unittest.main()
