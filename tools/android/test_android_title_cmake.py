"""TRIAEVUM_ANDROID_TITLE selection (ports/android/native/AndroidTitle.cmake) in CMake script mode.

No configure and no compiler: the function is evaluated with `cmake -P`.
Set CMAKE (or have cmake on PATH); the test is skipped without one.
"""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
MODULE = ROOT / "ports/android/native/AndroidTitle.cmake"
CMAKE = os.environ.get("CMAKE") or shutil.which("cmake")


def run(title: str, manifest_title: str | None = None):
    with tempfile.TemporaryDirectory() as temp:
        sources = Path(temp) / "sources"
        sources.mkdir()
        named = "" if manifest_title is None else f', "title": "{manifest_title}"'
        (sources / "TITLE_SOURCE_MANIFEST.json").write_text(
            '{"format": "triaevum_translated_title_source_v1"' + named + ', "files": {}}', encoding="utf-8")
        script = Path(temp) / "probe.cmake"
        script.write_text(
            'cmake_minimum_required(VERSION 3.30)\n'
            f'include("{MODULE.as_posix()}")\n'
            f'triaevum_android_title_plugin("{title}" "{ROOT.as_posix()}" source definition)\n'
            f'triaevum_android_require_source_title("{title}" "{sources.as_posix()}")\n'
            'message(STATUS "source=${source}")\nmessage(STATUS "definition=${definition}")\n', encoding="utf-8")
        return subprocess.run([CMAKE, "-P", str(script)], capture_output=True, text=True, timeout=60)


@unittest.skipUnless(CMAKE, "cmake is not available")
class AndroidTitleCmakeTests(unittest.TestCase):
    def test_titles_use_the_desktop_wrapper(self):
        for title in ("oot3d", "mm3d"):
            with self.subTest(title=title):
                result = run(title, title)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("tools/oot3d/native_game_runtime/triaevum_title_whole_aot_plugin.cpp", result.stdout)
                self.assertIn("definition=OOT3D_NATIVE_GENERATED_WHOLE_AOT=1", result.stdout)
        self.assertEqual(run("oot3d").returncode, 0)  # manifests without a title field

    def test_default_is_oot3d(self):
        text = (ROOT / "ports/android/native/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn('set(TRIAEVUM_ANDROID_TITLE "oot3d" CACHE STRING', text)

    def test_negative_unknown_title_and_wrong_sources(self):
        result = run("windwaker")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Unknown TRIAEVUM_ANDROID_TITLE", result.stderr)
        result = run("mm3d", "oot3d")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not TRIAEVUM_ANDROID_TITLE 'mm3d'", result.stderr)


if __name__ == "__main__":
    unittest.main()
