#!/usr/bin/env python3
"""Tests of the Android release recipe (android_release.py): nothing is built."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import android_release  # noqa: E402
from android_release import SIGNING_VARIABLES, main, recipe  # noqa: E402

ARGS = dict(ndk=Path("/ndk"), work=Path("/work"), translated_sources=Path("/sources"),
            reference_package=Path("/reference"), source_commit="a" * 40, windows=False)


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

    def test_no_private_paths_in_the_recipe_source(self):
        text = Path(android_release.__file__).read_text(encoding="utf-8")
        for marker in ("C:/", "I:/", "E:/", "/home/", "Users"):
            self.assertNotIn(marker, text)


if __name__ == "__main__":
    unittest.main()
