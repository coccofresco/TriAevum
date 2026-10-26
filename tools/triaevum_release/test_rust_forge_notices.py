import hashlib
import json
import tempfile
import unittest
from pathlib import Path

import rust_forge_notices as notices


class RustForgeNoticeTests(unittest.TestCase):
    def test_checked_in_notices_cover_cargo_lock(self):
        self.assertEqual(notices.check_checked_in(), [])

    def test_every_shipped_crate_has_an_accepted_or_reviewed_license(self):
        inventory = json.loads(notices.INVENTORY.read_text(encoding="utf-8"))
        for record in inventory["crates"]:
            with self.subTest(crate=record["name"]):
                elected = notices.elect(record["license"])
                self.assertTrue(set(elected) <= set(notices.ACCEPTED + notices.PENDING_REVIEW))
                self.assertTrue(record["texts"])
        self.assertEqual(inventory["notices"], notices.NOTICES_RELATIVE)

    def test_new_lock_crate_without_notice_is_rejected(self):
        raw = notices.INVENTORY.read_bytes()
        inventory = json.loads(raw)
        text = notices.NOTICES.read_text(encoding="utf-8")
        digest = hashlib.sha256(raw).hexdigest()
        lock = notices.lock_packages() | {("left-pad", "9.9.9")}
        errors = notices.inventory_errors(inventory, lock, text, digest)
        self.assertTrue(any("left-pad 9.9.9" in error and "without a notice" in error for error in errors), errors)
        # A removed crate leaves a stale decision.
        stale = notices.inventory_errors(inventory, notices.lock_packages() - {(inventory["crates"][0]["name"],
                                         inventory["crates"][0]["version"])}, text, digest)
        self.assertTrue(any("absent from Cargo.lock" in error for error in stale), stale)

    def test_stale_or_edited_notices_are_rejected(self):
        raw = notices.INVENTORY.read_bytes()
        inventory = json.loads(raw)
        text = notices.NOTICES.read_text(encoding="utf-8")
        edited = dict(inventory, crates=[dict(inventory["crates"][0], license="GPL-2.0-only"),
                                         *inventory["crates"][1:]])
        errors = notices.inventory_errors(edited, notices.lock_packages(), text, "0" * 64)
        self.assertTrue(any("not generated from the current inventory" in error for error in errors))
        self.assertTrue(any("no accepted alternative" in error for error in errors), errors)
        missing_row = text.replace(f"| `{inventory['crates'][0]['name']}` |", "| `renamed` |")
        errors = notices.inventory_errors(inventory, notices.lock_packages(), missing_row,
                                          hashlib.sha256(raw).hexdigest())
        self.assertTrue(any("missing from" in error for error in errors))

    def test_license_election(self):
        self.assertEqual(notices.elect("MIT OR Apache-2.0"), ("MIT",))
        self.assertEqual(notices.elect("MIT/Apache-2.0"), ("MIT",))
        self.assertEqual(notices.elect("Apache-2.0 OR GPL-2.0-only"), ("Apache-2.0",))
        self.assertEqual(notices.elect("(MIT OR Apache-2.0) AND Unicode-3.0"), ("MIT", "Unicode-3.0"))
        self.assertEqual(notices.elect("Apache-2.0 WITH LLVM-exception OR Apache-2.0 OR MIT"), ("MIT",))
        self.assertEqual(notices.elect("Apache-2.0 AND ISC"), ("Apache-2.0", "ISC"))
        for rejected in ("GPL-2.0-only", "MPL-2.0", "MIT AND SSPL-1.0", "", "CC-BY-NC-ND-4.0 OR AGPL-3.0"):
            with self.subTest(expression=rejected), self.assertRaises(notices.NoticeError):
                notices.elect(rejected)

    def test_lock_parser_ignores_workspace_members(self):
        with tempfile.TemporaryDirectory() as directory:
            lock = Path(directory) / "Cargo.lock"
            lock.write_text('version = 4\n\n[[package]]\nname = "forge-base"\nversion = "0.1.0"\n\n'
                            '[[package]]\nname = "sha2"\nversion = "0.10.9"\n'
                            'source = "registry+https://github.com/rust-lang/crates.io-index"\n', encoding="utf-8")
            self.assertEqual(notices.lock_packages(lock), {("sha2", "0.10.9")})


if __name__ == "__main__":
    unittest.main()
