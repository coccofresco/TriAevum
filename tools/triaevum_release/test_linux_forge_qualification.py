from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import Mock, patch

from qualify_linux_forge import installation_check, run_logged
from stage_linux_forge_candidate import stage
from release_platform import LINUX
from test_linux_precompiled_catalog import elf


class LinuxForgeQualificationTests(unittest.TestCase):
    def test_iconified_success_is_not_a_visual_pass(self):
        state = {"status": "failed", "installation_ready": True, "window_state": "iconic",
                 "widgets": [{}], "unmapped_widgets": [{}], "clipped_widgets": [],
                 "prepare_button_tracks_input": True, "notifications": [{"kind": "info"}]}
        self.assertFalse(installation_check(state))
        for changes in ({"installation_ready": False}, {"clipped_widgets": [{}]},
                        {"notifications": [{"kind": "error"}]}, {"window_state": "withdrawn"}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                installation_check({**state, **changes})
        self.assertTrue(installation_check({"status": "passed", "installation_ready": True}))

    def test_timed_out_process_is_terminated_and_reaped(self):
        process = Mock()
        process.wait.side_effect = [subprocess.TimeoutExpired("test", 1), 0]
        process.poll.return_value = None
        with tempfile.TemporaryDirectory() as temporary, \
             patch("qualify_linux_forge.subprocess.Popen", return_value=process), \
             self.assertRaises(subprocess.TimeoutExpired):
            run_logged(["test"], Path(temporary) / "log", cwd=Path(temporary), environment={}, timeout=1)
        process.terminate.assert_called_once_with()
        self.assertEqual(process.wait.call_count, 2)

    def test_candidate_rejects_existing_installation_and_title_stub(self):
        with tempfile.TemporaryDirectory() as temporary, \
             patch("stage_linux_forge_candidate.host_platform", return_value=LINUX):
            root = Path(temporary)
            with self.assertRaisesRegex(ValueError, "must not exist"):
                stage(root, root, root, root / "title.so", root, "1" * 40)
            for name in (LINUX.runtime, LINUX.title_module, LINUX.forge, "oot3d_game_module.so"):
                elf(root / name)
            with self.assertRaisesRegex(ValueError, "bootstrap"):
                stage(root, root, root, root / LINUX.title_module, root / "new", "1" * 40)
            self.assertFalse((root / "new").exists())

    def test_candidate_stages_the_rust_forge_without_python(self):
        with tempfile.TemporaryDirectory() as temporary, \
             patch("stage_linux_forge_candidate.host_platform", return_value=LINUX), \
             patch("stage_linux_forge_candidate.create_catalog") as catalog:
            root = Path(temporary)
            reference, bundle, build = root / "reference", root / "rust-forge", root / "build"
            for name in ("recipes/oot3d.json", "resources/gamecontrollerdb.txt", "source/titles/t.zip",
                         "LICENSES/SDL2-zlib.txt", "LICENSE", "LICENSE_SCOPE.md", "THIRD_PARTY_NOTICES.md"):
                (reference / name).parent.mkdir(parents=True, exist_ok=True)
                (reference / name).write_text("fixture")
            for path in (build / LINUX.runtime, build / LINUX.title_module, build / "oot3d_game_module.so",
                         root / "title.so", bundle / LINUX.forge, bundle / "forge/triaevum-forge"):
                elf(path)
            with (root / "title.so").open("ab") as title:
                title.write(b"title code")
            (bundle / "forge/rust-forge.json").write_text("{}")
            output = stage(reference, bundle, build, root / "title.so", root / "candidate", "1" * 40)
            for name in (LINUX.forge, "forge/triaevum-forge", "forge/rust-forge.json",
                         "LICENSES/Rust-Forge-crates.md", "LICENSES/SDL2-zlib.txt",
                         "docs/USER_GUIDE.md", "docs/TRIAEVUM_USER_DATA.md"):
                self.assertTrue((output / name).is_file(), name)
            self.assertFalse((output / "_internal").exists())
            self.assertIn('"forge_profile": "rust"', (output / "linux-candidate.json").read_text())
            catalog.assert_called_once()
            (bundle / "forge/rust-forge.json").unlink()
            with self.assertRaisesRegex(ValueError, "neither a Rust Forge build nor a PyInstaller bundle"):
                stage(reference, bundle, build, root / "title.so", root / "second", "1" * 40)
            self.assertFalse((root / "second").exists())
