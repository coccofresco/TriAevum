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
