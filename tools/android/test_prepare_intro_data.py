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
            self.assertIn("--renderer-cache-directory", result["arguments"])
            self.assertFalse(any(arg.startswith("--screenshot") for arg in result["arguments"]))
            self.assertFalse((output / "do-not-copy.sav").exists())
            self.assertEqual((output / "code.bin").read_bytes(), (source / "code.bin").read_bytes())
            with self.assertRaises(FileExistsError):
                prepare(source, profile, output)

    def test_portable_shaders_relocate_but_desktop_driver_cache_does_not(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, profile = self.fixture(root)
            (source / "portable.o3ps").write_bytes(b"opaque pack fixture")
            cache = source / "desktop-cache"
            cache.mkdir()
            (cache / "nri_pipeline_cache.bin").write_bytes(b"wrong GPU")
            data = json.loads(profile.read_text())
            data["arguments"] += ["--pica-aot-shader-pack", "${profile_dir}/portable.o3ps",
                                  "--renderer-cache-directory", "${profile_dir}/desktop-cache"]
            profile.write_text(json.dumps(data))
            output = root / "stage"
            prepare(source, profile, output, capture=True)
            args = json.loads((output / "TriAevum.android.launch.json").read_text())["arguments"]
            self.assertEqual(args[args.index("--pica-aot-shader-pack") + 1], "${profile_dir}/portable.o3ps")
            self.assertEqual(args[args.index("--renderer-cache-directory") + 1], "${profile_dir}/data/cache/renderer")
            self.assertEqual((output / "portable.o3ps").read_bytes(), b"opaque pack fixture")
            self.assertFalse((output / "desktop-cache").exists())
            self.assertIn("--screenshot-sequence", args)

    def test_portable_pack_must_remain_inside_installation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, profile = self.fixture(root)
            data = json.loads(profile.read_text())
            data["arguments"] += ["--pica-aot-shader-pack", "${profile_dir}/../outside.o3ps"]
            profile.write_text(json.dumps(data))
            with self.assertRaises(ValueError):
                prepare(source, profile, root / "stage")
            self.assertFalse((root / "stage").exists())

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
