from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from merge_llvm_profiles import merge_profiles


class MergeLlvmProfilesTests(unittest.TestCase):
    def test_merges_existing_profiles_in_stable_order(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            tool = root / "llvm" / "bin" / "llvm-profdata.exe"
            tool.parent.mkdir(parents=True)
            tool.write_bytes(b"tool")
            first = root / "b.profraw"
            second = root / "a.profraw"
            first.write_bytes(b"b")
            second.write_bytes(b"a")
            output = root / "profiles" / "oot3d.profdata"

            def complete(command: list[str], check: bool) -> None:
                self.assertTrue(check)
                self.assertEqual(command[2], "-sparse")
                self.assertEqual(command[3:5], [str(second), str(first)])
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_bytes(b"merged")

            with patch("subprocess.run", side_effect=complete):
                report = merge_profiles(root / "llvm", [first, second], output)
            self.assertEqual(report["profile_count"], 2)
            self.assertEqual(report["output_bytes"], 6)

    def test_rejects_empty_input(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(ValueError, "no LLVM raw profiles"):
                merge_profiles(Path(temporary), [], Path(temporary) / "out")


if __name__ == "__main__":
    unittest.main()
