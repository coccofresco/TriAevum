import tempfile
import unittest
from pathlib import Path

from prepare_release import clang_header_layout


class ClangHeaderLayoutTests(unittest.TestCase):
    def test_explicit_headers_only_from_llvm_resource_tree(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaisesRegex(ValueError, "missing"):
                clang_header_layout(root)
            include = root / "lib/clang/22/include"
            include.mkdir(parents=True)
            (include / "stddef.h").write_text("fixture")
            (root / "private.bin").write_bytes(b"private")
            self.assertEqual(clang_header_layout(root), [{"source": str((include / "stddef.h").resolve()),
                "path": "forge/clang/include/stddef.h", "role": "forge_clang_header"}])
