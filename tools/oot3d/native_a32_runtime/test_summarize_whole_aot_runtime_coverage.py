"""Tests for sampled whole-AOT runtime owner coverage."""

from __future__ import annotations

import unittest

from summarize_whole_aot_runtime_coverage import summarize_coverage


class WholeAotRuntimeCoverageTests(unittest.TestCase):
    def setUp(self) -> None:
        self.guest_map = {
            "format": "oot3d_whole_aot_guest_map_v1",
            "code_sha256": "code",
            "program_sha256": "program",
            "function_count": 2,
            "dispatch_entry_count": 4,
            "functions": [
                {
                    "entry": 0x1000,
                    "name": "Boot",
                    "shard": 1,
                    "ranges": [[0x1000, 0x1010]],
                    "dispatch_entries": [0x1000, 0x1004],
                },
                {
                    "entry": 0x2000,
                    "name": "Update",
                    "shard": 2,
                    "ranges": [[0x2000, 0x2010]],
                    "dispatch_entries": [0x2000, 0x2004],
                },
            ],
        }

    def test_aggregates_sampled_pcs_by_exact_owner(self) -> None:
        runtime = {
            "run_frames": 120,
            "a32_block_profile": {
                "enabled": True,
                "block_entries": 6400,
                "total_samples": 100,
                "reported_samples": 90,
                "blocks": [
                    {"pc": 0x1000, "samples": 40},
                    {"pc": 0x1004, "samples": 30},
                    {"pc": 0x2000, "samples": 20},
                ],
            },
        }

        result = summarize_coverage(
            self.guest_map, [("runtime.json", runtime, "hash")]
        )

        self.assertEqual(result["summary"]["observed_function_count"], 2)
        self.assertEqual(result["summary"]["observed_dispatch_pc_count"], 3)
        self.assertEqual(result["summary"]["matched_reported_samples"], 90)
        self.assertEqual(result["observed_functions"][0]["entry"], 0x1000)
        self.assertEqual(result["observed_functions"][0]["samples"], 70)

    def test_reports_unknown_profile_pcs(self) -> None:
        runtime = {
            "a32_block_profile": {
                "enabled": True,
                "block_entries": 64,
                "total_samples": 1,
                "reported_samples": 1,
                "blocks": [{"pc": 0xDEAD, "samples": 1}],
            }
        }

        result = summarize_coverage(
            self.guest_map, [("runtime.json", runtime, "hash")]
        )

        self.assertEqual(result["summary"]["unmatched_reported_samples"], 1)
        self.assertEqual(result["unmatched_blocks"], [{"pc": 0xDEAD, "samples": 1}])

    def test_attributes_interior_hook_pc_by_owner_range(self) -> None:
        runtime = {
            "a32_block_profile": {
                "enabled": True,
                "block_entries": 64,
                "total_samples": 1,
                "reported_samples": 1,
                "blocks": [{"pc": 0x1002, "samples": 1}],
            }
        }

        result = summarize_coverage(
            self.guest_map, [("runtime.json", runtime, "hash")]
        )

        self.assertEqual(result["summary"]["observed_function_count"], 1)
        self.assertEqual(result["summary"]["range_only_reported_samples"], 1)
        self.assertEqual(result["range_only_blocks"], [{"pc": 0x1002, "samples": 1}])

    def test_rejects_disabled_profile(self) -> None:
        runtime = {"a32_block_profile": {"enabled": False}}
        with self.assertRaisesRegex(ValueError, "not enabled"):
            summarize_coverage(
                self.guest_map, [("runtime.json", runtime, "hash")]
            )


if __name__ == "__main__":
    unittest.main()
