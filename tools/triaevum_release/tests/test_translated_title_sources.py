"""Exercise the shared publisher CMake inventory parser, without compiling a title."""

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
CMAKE = shutil.which("cmake")


@unittest.skipUnless(CMAKE, "CMake is required for publisher inventory tests")
class TranslatedTitleSourceTests(unittest.TestCase):
    def configure(self, files=None, entries=None, format="triaevum_translated_title_source_v1"):
        files = files if files is not None else {"body.cpp": "int title_entry;\n", "types.h": "#pragma once\n"}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, content in files.items():
                (root / name).write_text(content, encoding="utf-8")
            if entries is None:
                entries = {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in files}
            (root / "TITLE_SOURCE_MANIFEST.json").write_text(json.dumps({"format": format, "files": entries}), encoding="utf-8")
            helper = ROOT / "tools/triaevum_release/cmake/TranslatedTitleSources.cmake"
            (root / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 3.26)\nproject(Inventory LANGUAGES NONE)\n'
                f'include("{helper.as_posix()}")\n'
                'triaevum_translated_title_sources("${CMAKE_CURRENT_SOURCE_DIR}" sources)\n'
                'list(LENGTH sources count)\nmessage(STATUS "Verified C++ sources: ${count}")\n', encoding="utf-8")
            result = subprocess.run([CMAKE, "-S", str(root), "-B", str(root / "build")],
                                    capture_output=True, text=True, timeout=30)
            return result.returncode, result.stdout + result.stderr

    def test_valid_manifest(self):
        code, output = self.configure()
        self.assertEqual(code, 0, output)
        self.assertIn("Verified C++ sources: 1", output)

    def test_wrong_format(self):
        code, output = self.configure(format="not_a_title")
        self.assertNotEqual(code, 0)
        self.assertIn("Unsupported translated source manifest", output)

    def test_hash_mismatch(self):
        code, output = self.configure(entries={"body.cpp": "0" * 64})
        self.assertNotEqual(code, 0)
        self.assertIn("hash mismatch", output)

    def test_path_escape(self):
        code, output = self.configure(entries={"../outside.cpp": "0" * 64})
        self.assertNotEqual(code, 0)
        self.assertIn("Invalid translated source name", output)

    def test_missing_file(self):
        code, output = self.configure(entries={"missing.cpp": "0" * 64})
        self.assertNotEqual(code, 0)
        self.assertIn("missing.cpp", output)

    def test_empty_inventory(self):
        code, output = self.configure(files={})
        self.assertNotEqual(code, 0)
        self.assertIn("inventory is empty", output)

    def test_headers_only(self):
        code, output = self.configure(files={"types.h": "#pragma once\n"})
        self.assertNotEqual(code, 0)
        self.assertIn("no C++ sources", output)


if __name__ == "__main__":
    unittest.main()
