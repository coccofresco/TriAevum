import threading
import unittest

from bounded_build import run_bounded


class BoundedBuildTests(unittest.TestCase):
    def test_success_runs_every_item_once(self):
        seen = []
        run_bounded(seen.append, range(100), 3)
        self.assertEqual(sorted(seen), list(range(100)))

    def test_failure_does_not_drain_queue(self):
        seen = []
        lock = threading.Lock()
        def worker(item):
            with lock:
                seen.append(item)
            if item == 0:
                raise ValueError("compile failed")
        with self.assertRaisesRegex(ValueError, "compile failed"):
            run_bounded(worker, range(1000), 3)
        self.assertLessEqual(len(seen), 3)

    def test_invalid_worker_count(self):
        with self.assertRaises(ValueError):
            run_bounded(lambda _: None, [], 0)
