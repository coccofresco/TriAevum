"""Tests for deterministic native A32 hot-block profile extraction."""

from __future__ import annotations

import unittest
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from extract_block_profile import extract_profile


class ExtractBlockProfileTests(unittest.TestCase):
    def setUp(self) -> None:
        self.source = {
            "schema": "oot3d_native_a32_vulkan_runtime_v1",
            "authority": "code.bin",
            "frames": 1201,
            "a32_block_profile": {
                "enabled": True,
                "sample_rate_denominator": 64,
                "block_entries": 6400,
                "total_samples": 100,
                "blocks": [
                    {"pc": 0x1000, "samples": 50, "estimated_entries": 3200},
                    {"pc": 0x2000, "samples": 30, "estimated_entries": 1920},
                    {"pc": 0x3000, "samples": 20, "estimated_entries": 1280},
                ],
            },
        }

    def test_preserves_ranked_blocks_and_reports_coverage(self) -> None:
        result = extract_profile(self.source, 2)

        self.assertEqual(result["format"], "oot3d_a32_hot_block_profile_v1")
        self.assertEqual(result["blocks"], [
            {"pc": 0x1000, "samples": 50},
            {"pc": 0x2000, "samples": 30},
        ])
        self.assertEqual(result["selected_samples"], 80)
        self.assertEqual(result["selected_coverage"], 0.8)

    def test_rejects_disabled_or_empty_profiles(self) -> None:
        self.source["a32_block_profile"]["enabled"] = False
        with self.assertRaisesRegex(ValueError, "enabled A32 block profile"):
            extract_profile(self.source, 2)

        self.source["a32_block_profile"]["enabled"] = True
        self.source["a32_block_profile"]["total_samples"] = 0
        with self.assertRaisesRegex(ValueError, "has no samples"):
            extract_profile(self.source, 2)


if __name__ == "__main__":
    unittest.main()
