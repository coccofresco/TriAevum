"""The checked-in pinned A32 provenance must match the pinned local extensions.

generate_aot.py refuses to run when a local extension drifted from its
provenance record; this test makes the same check at test time so a drift
never reaches a build.
"""

from __future__ import annotations

import copy
import json
import unittest

import generate_aot


def load_provenance() -> dict[str, object]:
    return json.loads(generate_aot.PROVENANCE.read_text(encoding="utf-8"))


class PinnedLocalExtensionsTest(unittest.TestCase):
    def test_checked_in_provenance_matches_pinned_files(self) -> None:
        provenance = load_provenance()
        self.assertEqual(provenance.get("format"), "oot3d_pinned_a32_runtime_v1")
        self.assertTrue(provenance.get("local_extensions"))
        generate_aot.validate_local_extensions(provenance)

    def test_tampered_sha_is_refused(self) -> None:
        provenance = copy.deepcopy(load_provenance())
        provenance["local_extensions"][0]["working_sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "drifted"):
            generate_aot.validate_local_extensions(provenance)

    def test_tampered_size_is_refused(self) -> None:
        provenance = copy.deepcopy(load_provenance())
        provenance["local_extensions"][0]["working_size"] += 1
        with self.assertRaisesRegex(ValueError, "drifted"):
            generate_aot.validate_local_extensions(provenance)

    def test_missing_base_is_refused(self) -> None:
        provenance = copy.deepcopy(load_provenance())
        provenance["local_extensions"][0]["base_sha256"] = "1" * 64
        with self.assertRaisesRegex(ValueError, "no matching base"):
            generate_aot.validate_local_extensions(provenance)


if __name__ == "__main__":
    unittest.main()
