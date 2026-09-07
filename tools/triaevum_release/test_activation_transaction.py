import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from activation_transaction import activation_transaction, installation_lock, journal_path


class ActivationTransactionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.old = self.root / "plugin.dll"
        self.new = self.root / "profile.json"
        self.old.write_bytes(b"original")
        self.targets = [self.old, self.new]

    def test_commit(self):
        with activation_transaction(self.root, self.targets):
            self.old.write_bytes(b"updated")
            self.new.write_bytes(b"profile")
        self.assertEqual(self.old.read_bytes(), b"updated")
        self.assertFalse(journal_path(self.root).exists())
        self.assertFalse(list(journal_path(self.root).parent.glob("*.bak")))

    def test_failure_restores_old_and_removes_new(self):
        with self.assertRaisesRegex(RuntimeError, "injected"):
            with activation_transaction(self.root, self.targets):
                self.old.write_bytes(b"updated")
                self.new.write_bytes(b"profile")
                raise RuntimeError("injected")
        self.assertEqual(self.old.read_bytes(), b"original")
        self.assertFalse(self.new.exists())
        self.assertFalse(journal_path(self.root).exists())

    def interrupt_process(self):
        script = """
import os, sys
from pathlib import Path
from activation_transaction import activation_transaction
root = Path(sys.argv[1])
with activation_transaction(root, [root / 'plugin.dll', root / 'profile.json']):
    (root / 'plugin.dll').write_bytes(b'partial')
    (root / 'profile.json').write_bytes(b'partial')
    os._exit(71)
"""
        env = dict(os.environ, PYTHONPATH=str(Path(__file__).parent))
        result = subprocess.run([sys.executable, "-c", script, str(self.root)],
                                env=env, timeout=15, capture_output=True)
        self.assertEqual(result.returncode, 71, result.stderr)

    def test_process_termination_recovers_before_next_activation(self):
        self.interrupt_process()
        self.assertTrue(journal_path(self.root).exists())
        with activation_transaction(self.root, self.targets):
            self.assertEqual(self.old.read_bytes(), b"original")
            self.assertFalse(self.new.exists())

    def test_different_recovery_targets_are_not_written(self):
        self.interrupt_process()
        with self.assertRaisesRegex(ValueError, "targets differ"):
            with activation_transaction(self.root, [self.old, self.root / "other"]):
                self.fail("must not enter")
        self.assertEqual(self.old.read_bytes(), b"partial")
        self.assertTrue(journal_path(self.root).exists())

    def test_simultaneous_publication_is_rejected(self):
        with installation_lock(self.root):
            with self.assertRaises(OSError):
                with activation_transaction(self.root, self.targets):
                    self.fail("must not enter")


if __name__ == "__main__":
    unittest.main()
