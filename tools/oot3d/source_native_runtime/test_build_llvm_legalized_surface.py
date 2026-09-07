#!/usr/bin/env python3

import json
import sys
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest


sys.path.insert(0, str(Path(__file__).resolve().parent))

from build_llvm_legalized_surface import (
    REQUIRED_TARGET_WORD_LOWERING_CONTRACT,
    validate_lowered_source_root,
)


class LoweredSourceValidationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.snapshot = {
            "decomp_revision": "test-revision",
            "canonical_sources": [{"path": "one.c"}, {"path": "two.c"}],
        }

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write_manifest(self, **overrides: object) -> None:
        payload = {
            "lowering_contract": REQUIRED_TARGET_WORD_LOWERING_CONTRACT,
            "decomp_revision": "test-revision",
            "source_count": 2,
        }
        payload.update(overrides)
        (self.root / "lowered_snapshot_manifest.json").write_text(
            json.dumps(payload), encoding="utf-8"
        )

    def test_accepts_matching_lowered_snapshot(self) -> None:
        self.write_manifest()
        result = validate_lowered_source_root(self.root, self.snapshot)
        self.assertEqual(
            result["lowering_contract"],
            REQUIRED_TARGET_WORD_LOWERING_CONTRACT,
        )

    def test_rejects_unlowered_snapshot(self) -> None:
        with self.assertRaisesRegex(SystemExit, "requires a lowered"):
            validate_lowered_source_root(self.root, self.snapshot)

    def test_rejects_stale_lowering_contract(self) -> None:
        self.write_manifest(lowering_contract="obsolete")
        with self.assertRaisesRegex(SystemExit, "contract mismatch"):
            validate_lowered_source_root(self.root, self.snapshot)


if __name__ == "__main__":
    unittest.main()
