import tempfile
import unittest
from pathlib import Path

from prepare_release import visual_cpp_runtime_artifacts


class RuntimeRedistTests(unittest.TestCase):
    def test_only_required_release_runtime_files_are_packaged(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            expected = {"msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"}
            for name in expected | {"vcruntime140d.dll", "unrelated.dll"}:
                (root / name).write_bytes(b"fixture")
            result = visual_cpp_runtime_artifacts(root)
            self.assertEqual(set(result), expected)
            self.assertTrue(all(path.parent == root for path in result.values()))

    def test_missing_dependency_does_not_fall_back_to_developer_system(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "msvcp140.dll").write_bytes(b"fixture")
            with self.assertRaisesRegex(ValueError, "--vc-redist-dir"):
                visual_cpp_runtime_artifacts(root)
