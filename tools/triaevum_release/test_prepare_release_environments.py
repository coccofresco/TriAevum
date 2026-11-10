import fnmatch
import json
import re
import tempfile
import unittest
from pathlib import Path

from prepare_release import ENVIRONMENTS, ROOT, environment_profile_layout

POLICY = Path(__file__).resolve().with_name("release_policy.json")
ADAPTER = ROOT / "tools/oot3d/native_game_runtime/triaevum_title_adapter.h"


class EnvironmentProfileLayoutTests(unittest.TestCase):
    """Every title's default environment profiles ship where the runtime reads
    them: OoT3D's at resources/environments, every other title's beneath
    resources/environments/<adapter key>."""

    def test_every_repository_profile_ships_including_title_subfolders(self):
        expected = {"resources/environments/" + p.relative_to(ENVIRONMENTS).as_posix()
                    for p in ENVIRONMENTS.rglob("*.environment.json")}
        layout = environment_profile_layout()
        self.assertEqual({item["path"] for item in layout}, expected)
        self.assertTrue(all(item["role"] == "default_environment_profile" for item in layout))
        for folder in (d for d in ENVIRONMENTS.iterdir() if d.is_dir()):
            if any(folder.glob("*.environment.json")):
                self.assertTrue(any(path.startswith(f"resources/environments/{folder.name}/")
                                    for path in expected), folder.name)

    def test_title_subfolders_are_adapter_keys(self):
        adapter = ADAPTER.read_text(encoding="utf-8")
        for folder in (d for d in ENVIRONMENTS.iterdir() if d.is_dir()):
            self.assertRegex(adapter, rf'return "{re.escape(folder.name)}"', folder.name)

    def test_shipped_paths_satisfy_the_release_policy(self):
        patterns = json.loads(POLICY.read_text(encoding="utf-8"))["allowed_roles"]["default_environment_profile"]
        for item in environment_profile_layout():
            self.assertTrue(any(fnmatch.fnmatchcase(item["path"].lower(), p.lower()) for p in patterns),
                            item["path"])

    def test_generic_over_any_title_subfolder(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "newtitle").mkdir()
            for name in ("scene-0001.environment.json", "newtitle/group-indoor.environment.json",
                         "newtitle/notes.txt"):
                (root / name).write_text("{}\n", encoding="utf-8")
            paths = {item["path"] for item in environment_profile_layout(root)}
            self.assertEqual(paths, {"resources/environments/scene-0001.environment.json",
                                     "resources/environments/newtitle/group-indoor.environment.json"})


if __name__ == "__main__":
    unittest.main()
