#!/usr/bin/env python3
"""Tests of the Android release recipe (android_release.py): nothing is built."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import android_release  # noqa: E402
from android_release import SIGNING_VARIABLES, main, recipe  # noqa: E402

ARGS = dict(ndk=Path("/ndk"), work=Path("/work"), translated_sources=Path("/sources"),
            reference_package=Path("/reference"), source_commit="a" * 40, windows=False,
            a32_registry=Path("/overlay_r18"),
            rustflags="-C\x1flink-arg=-Wl,-z,max-page-size=16384")


def steps(title, environ=None):
    return {step: (command, extra) for step, command, extra in recipe(title, environ=environ or {}, **ARGS)}


class AndroidReleaseRecipeTests(unittest.TestCase):
    def test_mc_recipe(self):
        result = steps("mm3d")
        self.assertIn("-DTRIAEVUM_ANDROID_TITLE=mm3d", result["title configure"][0])
        self.assertIn("-DTRIAEVUM_ANDROID_TITLE_OPTIMIZATION=1", result["title configure"][0])
        self.assertEqual(result["forge build"][0][-2:], ["--features", "title-mm3d"])
        self.assertIn(":app:assembleMcRelease", result["apk"][0])
        self.assertIn("-PtriaevumUnsignedReleaseCandidate=true", result["apk"][0])
        package = result["forge package"][0]
        self.assertEqual(package[package.index("--title") + 1], "mm3d")
        self.assertNotIn("--sign", package)

    def test_mc_runtime_compiles_the_rule_d8_registry(self):
        self.assertIn(f"-DOOT3D_A32_AOT_GENERATED_DIR={Path('/overlay_r18')}", steps("mm3d")["runtime configure"][0])
        self.assertFalse(any("OOT3D_A32_AOT_GENERATED_DIR" in arg for arg in steps("oot3d")["runtime configure"][0]))
        with self.assertRaisesRegex(ValueError, "a32-registry"):
            recipe("mm3d", environ={}, **dict(ARGS, a32_registry=None))

    def test_eot_recipe_has_no_forge_features(self):
        result = steps("oot3d")
        self.assertIn("-DTRIAEVUM_ANDROID_TITLE=oot3d", result["title configure"][0])
        self.assertNotIn("--features", result["forge build"][0])
        self.assertIn(":app:assembleEotRelease", result["apk"][0])

    def test_signing_only_from_the_environment(self):
        environ = {name: "x" for name in SIGNING_VARIABLES}
        environ["TRIAEVUM_RELEASE_SIGNING_KEY"] = "k"
        result = steps("oot3d", environ)
        self.assertNotIn("-PtriaevumUnsignedReleaseCandidate=true", result["apk"][0])
        self.assertIn("--sign", result["forge package"][0])
        partial = dict(environ)
        del partial["TRIAEVUM_ANDROID_KEY_PASS"]
        self.assertIn("-PtriaevumUnsignedReleaseCandidate=true", steps("oot3d", partial)["apk"][0])

    def test_negative_bad_inputs(self):
        with self.assertRaises(ValueError):
            recipe("zelda", environ={}, **ARGS)
        with self.assertRaises(ValueError):
            recipe("mm3d", environ={}, **dict(ARGS, source_commit="abc"))
        with self.assertRaises(SystemExit):
            main(["--title", "mm3d", "--ndk", "n", "--work", str(android_release.ROOT / "build"),
                  "--translated-sources", "s", "--reference-package", "r", "--source-commit", "a" * 40])

    def test_runtime_and_forge_are_per_product(self):
        mc, eot = steps("mm3d"), steps("oot3d")
        self.assertIn("-DTRIAEVUM_TITLES=oot3d;mm3d", mc["runtime configure"][0])
        self.assertIn("-DTRIAEVUM_PRODUCT_TITLE=mm3d", mc["runtime configure"][0])
        self.assertIn("-DTRIAEVUM_TITLES=oot3d", eot["runtime configure"][0])
        self.assertIn("-DTRIAEVUM_PRODUCT_TITLE=oot3d", eot["runtime configure"][0])
        for result in (mc, eot):
            self.assertIn("-DTRIAEVUM_DEVELOPER_TOOLS=OFF", result["runtime configure"][0])
            self.assertIn("max-page-size=16384", result["forge build"][1]["CARGO_ENCODED_RUSTFLAGS"])
        self.assertNotEqual(mc["forge build"][1]["CARGO_TARGET_DIR"], eot["forge build"][1]["CARGO_TARGET_DIR"])
        self.assertNotEqual(mc["runtime build"][0], eot["runtime build"][0])

    def test_python_steps_start(self):
        # Every Python step must at least parse its arguments the way the recipe runs it.
        import subprocess
        for step in ("stage", "forge package"):
            command = steps("mm3d")[step][0]
            completed = subprocess.run([*command[:3], "--help"], cwd=android_release.ROOT,
                                       capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, f"{step}: {completed.stderr[-400:]}")

    def test_no_private_paths_in_the_recipe_source(self):
        text = Path(android_release.__file__).read_text(encoding="utf-8")
        for marker in ("C:/", "I:/", "E:/", "/home/", "Users"):
            self.assertNotIn(marker, text)


if __name__ == "__main__":
    unittest.main()
