"""R-A G6-F2: an Echoes of Time 0.8.0-beta.1.2 installation updated in place.

beta.1.2 installed the game as the root TriAevum.exe with
data/TriAevum.launch.json. From beta.2 the installed executable takes the
product name (Echoes_of_Time.exe) and the launch profile is named after it.
The Rust Forge test forge-core/tests/update.rs
(a_beta_1_2_installation_updates_in_place_to_the_product_executable) covers
the full signed update; this covers the shared Python release layer.
"""
from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

from host_layout import installation_state
from release_platform import (LEGACY_LAUNCH_PROFILE, LEGACY_WINDOWS_RUNTIME, LINUX, WINDOWS,
                              installed_runtime_path, launch_profile_name, place_installed_runtime)

USER_FILES = {
    "data/savedata/save01.bin": b"link's save",
    "data/savestates/slot1.oot3dsav": b"state",
    "data/config/TriAevum.json": b'{"user": "settings"}',
    "data/config/topscreen_ui.json": b'{"layout": "custom"}',
    "data/texture-packs/pack/a.png": b"png",
}


def beta_1_2_installation(root: Path) -> None:
    """The beta.1.2 layout plus the beta.2 package's runtime/ just extracted."""
    (root / "runtime").mkdir()
    (root / WINDOWS.runtime).write_bytes(b"beta.2 runtime")
    (root / LEGACY_WINDOWS_RUNTIME).write_bytes(b"beta.1.2 runtime")
    state = installation_state(root, "win32")
    state.mkdir(parents=True, exist_ok=True)
    (state / LEGACY_LAUNCH_PROFILE).write_text('{"arguments": ["--fullscreen"]}', encoding="utf-8")
    for relative, payload in USER_FILES.items():
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)


class BetaUpgradeLayoutTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def test_names_of_the_two_layouts(self) -> None:
        self.assertEqual(LEGACY_WINDOWS_RUNTIME, "TriAevum.exe")
        self.assertEqual(LEGACY_LAUNCH_PROFILE, "TriAevum.launch.json")
        self.assertEqual(WINDOWS.installed_runtime, "Echoes_of_Time.exe")
        self.assertEqual(launch_profile_name(WINDOWS), "Echoes_of_Time.launch.json")

    def test_beta_1_2_layout_takes_the_product_executable_and_keeps_user_files(self) -> None:
        beta_1_2_installation(self.root)
        installed = place_installed_runtime(self.root, WINDOWS)
        self.assertEqual(installed, self.root / "Echoes_of_Time.exe")
        self.assertEqual(installed.read_bytes(), b"beta.2 runtime")
        self.assertFalse((self.root / LEGACY_WINDOWS_RUNTIME).exists(), "the legacy TriAevum.exe is removed")
        self.assertFalse((self.root / WINDOWS.runtime).exists(), "the packaged runtime moved to the root")
        for relative, payload in USER_FILES.items():
            self.assertEqual((self.root / relative).read_bytes(), payload, relative)
        # The old profile is never deleted: the Forge reads it until it
        # writes the profile named after the product executable.
        self.assertTrue((self.root / "data" / LEGACY_LAUNCH_PROFILE).is_file())
        names = sorted(path.name for path in self.root.iterdir())
        self.assertEqual(names, ["Echoes_of_Time.exe", "data", "runtime"])
        # A second activation of the same package changes nothing.
        self.assertEqual(place_installed_runtime(self.root, WINDOWS), installed)
        self.assertEqual(installed.read_bytes(), b"beta.2 runtime")

    def test_the_untouched_beta_1_2_installation_runs_its_legacy_executable(self) -> None:
        beta_1_2_installation(self.root)
        (self.root / WINDOWS.runtime).unlink()  # before the new package is extracted
        # Inspection, repair and update see the old game as this installation's runtime.
        self.assertEqual(installed_runtime_path(self.root, WINDOWS), self.root / LEGACY_WINDOWS_RUNTIME)
        # Once the product executable exists it wins, and Linux has no legacy name.
        (self.root / WINDOWS.installed_runtime).write_bytes(b"product")
        self.assertEqual(installed_runtime_path(self.root, WINDOWS), self.root / WINDOWS.installed_runtime)
        linux = self.root / "linux-only"
        linux.mkdir()
        (linux / LEGACY_WINDOWS_RUNTIME).write_bytes(b"unrelated")
        self.assertEqual(installed_runtime_path(linux, LINUX), linux / LINUX.installed_runtime)

    def test_negatives_without_a_new_runtime_or_off_windows(self) -> None:
        beta_1_2_installation(self.root)
        # No packaged runtime (nothing new to place): the old game stays as is.
        (self.root / WINDOWS.runtime).unlink()
        place_installed_runtime(self.root, WINDOWS)
        self.assertEqual((self.root / LEGACY_WINDOWS_RUNTIME).read_bytes(), b"beta.1.2 runtime")
        self.assertFalse((self.root / "Echoes_of_Time.exe").exists())
        # Linux never removes a file called TriAevum.exe: the rename is Windows only.
        linux = self.root / "linux"
        linux.mkdir()
        (linux / LINUX.runtime).write_bytes(b"linux runtime")
        (linux / LEGACY_WINDOWS_RUNTIME).write_bytes(b"unrelated")
        installed = place_installed_runtime(linux, LINUX)
        self.assertEqual(installed, linux / LINUX.installed_runtime)
        self.assertEqual((linux / LEGACY_WINDOWS_RUNTIME).read_bytes(), b"unrelated")


if __name__ == "__main__":
    unittest.main()
