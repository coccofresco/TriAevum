import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from cache_lock import acquire_cache_lock, release_cache_lock


class CacheLockTests(unittest.TestCase):
    def test_exclusive_and_reusable_without_deleting_lock_file(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "lock"
            lock = acquire_cache_lock(path)
            try:
                with self.assertRaises(OSError):
                    acquire_cache_lock(path)
            finally:
                release_cache_lock(lock)
            self.assertTrue(path.exists())
            release_cache_lock(acquire_cache_lock(path))

    def test_process_death_releases_the_lock(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "lock"
            subprocess.run(
                [sys.executable, "-c",
                 "import os,sys; from pathlib import Path; "
                 "from cache_lock import acquire_cache_lock; "
                 "acquire_cache_lock(Path(sys.argv[1])); os._exit(0)", str(path)],
                cwd=Path(__file__).parent, check=True, timeout=10,
            )
            release_cache_lock(acquire_cache_lock(path))

    def test_does_not_steal_legacy_lock(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "lock"
            path.write_text("pid=42\n")
            with self.assertRaises(ValueError):
                acquire_cache_lock(path)
