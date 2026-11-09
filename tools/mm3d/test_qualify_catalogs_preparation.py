"""CPU-only launcher tests. Never operate on the shared GPU lock."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import qualify_catalogs_preparation as gate


class GateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.lock = self.root / "test.lock"

    def test_owned_lock_is_a_directory_and_released(self):
        with gate.owned_gpu_lock(self.lock, wait_seconds=0) as token:
            self.assertTrue(self.lock.is_dir())
            self.assertEqual((self.lock / "owner").read_text(), token)
        self.assertFalse(self.lock.exists())

    def test_exception_releases_only_our_lock(self):
        with self.assertRaisesRegex(ValueError, "failed helper"):
            with gate.owned_gpu_lock(self.lock, wait_seconds=0):
                raise ValueError("failed helper")
        self.assertFalse(self.lock.exists())

    def test_busy_directory_or_file_not_removed(self):
        for directory in (True, False):
            if directory:
                self.lock.mkdir()
            else:
                self.lock.write_text("foreign file")
            with self.assertRaises(TimeoutError):
                with gate.owned_gpu_lock(self.lock, wait_seconds=0):
                    self.fail("foreign lock acquired")
            if directory:
                self.lock.rmdir()
            else:
                self.assertEqual(self.lock.read_text(), "foreign file")
                self.lock.unlink()

    def test_gpu_lock_path_comes_from_environment_not_a_host_path(self):
        self.assertEqual(gate.default_gpu_lock({"TRIAEVUM_GPU_LOCK": str(self.lock)}), self.lock)
        self.assertEqual(gate.default_gpu_lock({"TRIAEVUM_WORK_DIR": str(self.root)}),
                         self.root / "gpu-run.lock")
        # Negative: nothing configured -> no built-in default (the CLI then refuses).
        self.assertIsNone(gate.default_gpu_lock({}))
        self.assertNotIn("_triaevum" + "_work", Path(gate.__file__).read_text(encoding="utf-8"))

    def test_replaced_token_never_removed(self):
        with gate.owned_gpu_lock(self.lock, wait_seconds=0):
            (self.lock / "owner").write_text("foreign owner")
        self.assertEqual((self.lock / "owner").read_text(), "foreign owner")

    def test_runtime_inputs_are_checked_not_trusted(self):
        source = self.root / "source.cpp"
        source.write_text("original")
        receipt = self.root / "inputs.json"
        receipt.write_text(json.dumps({"files": {"source.cpp": gate.sha256_file(source)}}))
        with patch.object(gate, "REPO", self.root):
            self.assertEqual(gate.verify_inputs(receipt), gate.sha256_file(receipt))
            source.write_text("changed")
            with self.assertRaisesRegex(ValueError, "stale runtime input"):
                gate.verify_inputs(receipt)
            receipt.write_text(json.dumps({"files": {"../escape": "0" * 64}}))
            with self.assertRaisesRegex(ValueError, "stale runtime input"):
                gate.verify_inputs(receipt)

    def test_package_records_are_exact_copies(self):
        source = self.root / "source"
        source.write_bytes(b"fixture")
        record = gate.artifact(self.root, source, "copy")
        self.assertEqual(record, {"path": "copy", "bytes": 7, "sha256": gate.sha256_file(source)})
        self.assertEqual((self.root / "copy").read_bytes(), b"fixture")


if __name__ == "__main__":
    unittest.main()
