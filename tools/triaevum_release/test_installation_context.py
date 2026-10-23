import tempfile
import unittest
from pathlib import Path

from installation_context import InstallationContext, expand_profile_argument, resolve_reference


class InstallationContextTests(unittest.TestCase):
    def test_profile_paths_follow_new_root(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "original"
            moved = Path(temporary) / "moved installation"
            context = InstallationContext(root)
            profile = root / "TriAevum.launch.json"
            for suffix in ("private-plugins/hash/title.dll", "config/runtime.json", "saves"):
                argument = context.profile_argument(root / suffix, profile)
                expanded = expand_profile_argument(argument, moved / profile.name)
                self.assertEqual(Path(expanded), moved / suffix)

    def test_package_files_follow_the_runtime_across_mounts(self):
        # Issue 66: an AppImage is mounted at a new path on every launch, so
        # its resources are named from the runtime, not by absolute path.
        with tempfile.TemporaryDirectory() as temporary:
            data = Path(temporary) / "data" / "activation"
            first = Path(temporary) / ".mount_first" / "usr" / "lib" / "triaevum"
            second = Path(temporary) / ".mount_second" / "usr" / "lib" / "triaevum"
            profile = data / "TriAevum.launch.json"
            argument = InstallationContext(data).profile_argument(first / "resources", profile, runtime_dir=first)
            self.assertEqual(argument, "${runtime_dir}/resources")
            self.assertEqual(Path(expand_profile_argument(argument, profile, second)), (second / "resources").resolve())

    def test_external_inputs_remain_explicit(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "installation"
            external = Path(temporary) / "user ROM" / "code.bin"
            context = InstallationContext(root)
            self.assertEqual(context.scope(external), "external")
            self.assertEqual(context.reference(external, root), str(external.resolve()))
            self.assertEqual(resolve_reference(str(external), root), external.resolve())

    def test_external_base_cannot_make_internal_path_portable(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "installation"
            base = Path(temporary) / "external data"
            context = InstallationContext(root)
            self.assertEqual(context.reference(root / "game.dll", base), str((root / "game.dll").resolve()))


if __name__ == "__main__":
    unittest.main()
