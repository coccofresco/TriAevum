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
