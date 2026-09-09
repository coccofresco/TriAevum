import json
from pathlib import Path
import tempfile
import unittest
from prepare_intro_data import contained, prepare


class IntroDataTests(unittest.TestCase):
    def fixture(self, root):
        source = root / "install"
        source.mkdir()
        (source / "resources").mkdir()
        (source / "resources/test.txt").write_text("synthetic test resource")
        for name in ("code.bin", "exheader.bin", "romfs.bin", "do-not-copy.sav"):
            (source / name).write_bytes(b"test fixture, not game data")
        (source / "manifest.json").write_text(json.dumps({"source": {
            "code_bin_path": "code.bin", "exheader_path": "exheader.bin",
            "romfs_image_path": "romfs.bin"}}))
        profile = source / "launch.json"
        profile.write_text(json.dumps({"arguments": [
            "--title-plugin", "/old/desktop/title.so",
            "--a32-process-manifest", "${profile_dir}/manifest.json",
            "--resource-root", "${profile_dir}/resources"]}))
        return source, profile

    def test_only_required_inputs_and_fresh_boot(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, profile = self.fixture(root)
            output = root / "stage"
            prepare(source, profile, output)
            result = json.loads((output / "TriAevum.android.launch.json").read_text())
            self.assertNotIn("--title-plugin", result["arguments"])
            self.assertNotIn("--load-state", result["arguments"])
            self.assertIn("native30_no_interpolation", result["arguments"])
            self.assertFalse((output / "do-not-copy.sav").exists())
            self.assertEqual((output / "code.bin").read_bytes(), (source / "code.bin").read_bytes())
            with self.assertRaises(FileExistsError):
                prepare(source, profile, output)

    def test_rejects_manifest_escape_before_creating_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, profile = self.fixture(root)
            manifest = source / "manifest.json"
            data = json.loads(manifest.read_text())
            data["source"]["romfs_image_path"] = "../external.bin"
            manifest.write_text(json.dumps(data))
            with self.assertRaises(ValueError):
                prepare(source, profile, root / "stage")
            self.assertFalse((root / "stage").exists())

    def test_refuses_staging_inside_installation(self):
        with tempfile.TemporaryDirectory() as temporary:
            source, profile = self.fixture(Path(temporary))
            with self.assertRaises(ValueError):
                prepare(source, profile, source / "stage")

    def test_containment(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.assertEqual(contained(root, root / "child"), (root / "child").resolve())
            with self.assertRaises(ValueError):
                contained(root, root / ".." / "outside")


if __name__ == "__main__":
    unittest.main()
