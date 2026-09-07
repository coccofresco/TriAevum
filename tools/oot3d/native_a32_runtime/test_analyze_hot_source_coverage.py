"""Tests for hot A32 function/source coverage analysis."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from analyze_hot_source_coverage import FunctionInterval, analyze


class HotSourceCoverageTests(unittest.TestCase):
    def test_aggregates_blocks_and_prefers_narrowest_interval(self) -> None:
        profile = {
            "format": "oot3d_a32_hot_block_profile_v1",
            "total_samples": 100,
            "selected_samples": 90,
            "blocks": [
                {"pc": 0x1010, "samples": 50},
                {"pc": 0x1020, "samples": 30},
                {"pc": 0x3000, "samples": 10},
            ],
        }
        intervals = [
            FunctionInterval(0x1000, 0x1100, "Wide"),
            FunctionInterval(0x1010, 0x1030, "HotFunction"),
        ]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "src"
            ghidra = root / "ghidra"
            source.mkdir()
            ghidra.mkdir()
            (source / "hot.c").write_text(
                "void HotFunction(void) {}\n", encoding="utf-8"
            )
            nested_ghidra = ghidra / "export" / "decompiled"
            nested_ghidra.mkdir(parents=True)
            (nested_ghidra / "00001_00001010_HotFunction.c").write_text(
                "", encoding="utf-8"
            )
            report = analyze(profile, intervals, source, ghidra)

        self.assertEqual(report["mapped_samples"], 80)
        self.assertEqual(report["unmapped_samples"], 10)
        self.assertEqual(report["mapped_functions"], 1)
        function = report["functions"][0]
        self.assertEqual(function["name"], "HotFunction")
        self.assertEqual(function["profiled_blocks"], 2)
        self.assertEqual(function["maintained_source_mentions"], ["hot.c"])
        self.assertEqual(
            function["ghidra_pseudocode"],
            ["export/decompiled/00001_00001010_HotFunction.c"],
        )


if __name__ == "__main__":
    unittest.main()
