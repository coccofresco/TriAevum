from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from common import atomic_write_json
from readiness import DEFAULT_READINESS, evaluate_readiness


class ReleaseReadinessTests(unittest.TestCase):
    def test_v2_clean_machine_evidence_is_not_assumed_from_v1(self) -> None:
        result = evaluate_readiness(DEFAULT_READINESS)
        self.assertFalse(result.ready)
        self.assertGreaterEqual(len(result.complete), 7)
        self.assertTrue(any("generic_runtime_target" in item for item in result.complete))
        self.assertTrue(
            any("self_contained_forge_binary" in item for item in result.complete)
        )
        self.assertEqual(len(result.blockers), 1)
        self.assertTrue(any("fast_direct_aot_backend" in item for item in result.complete))
        self.assertTrue(any("clean_machine_forge_boot" in item for item in result.blockers))

    def test_accepts_all_complete_required_gates(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "readiness.json"
            atomic_write_json(
                path,
                {
                    "format": "triaevum_release_readiness_v1",
                    "gates": [
                        {"id": "one", "required": True, "status": "complete"},
                        {"id": "optional", "required": False, "status": "pending"},
                    ],
                },
            )
            result = evaluate_readiness(path)
            self.assertTrue(result.ready, result)


if __name__ == "__main__":
    unittest.main()
