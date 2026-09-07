import os
import queue
import subprocess
import sys
import threading
import unittest
from pathlib import Path


@unittest.skipUnless(os.name == "nt", "Windows Job Object integration")
class WorkerJobTests(unittest.TestCase):
    def test_owner_exit_terminates_worker_without_direct_kill(self):
        owner = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"],
                                  creationflags=subprocess.CREATE_NO_WINDOW)
        code = ("from worker_job import bind_worker_lifetime,watch_owner; import sys,time; "
                f"bind_worker_lifetime(); watch_owner({owner.pid}); "
                "print('watching',flush=True); time.sleep(60)")
        worker = subprocess.Popen([sys.executable, "-c", code], cwd=Path(__file__).parent,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
            creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            ready = queue.Queue()
            threading.Thread(target=lambda: ready.put(worker.stdout.readline()), daemon=True).start()
            self.assertIn("watching", ready.get(timeout=10))
            owner.kill()
            owner.wait(timeout=5)
            worker.communicate(timeout=5)
            self.assertEqual(worker.returncode, 125)
        finally:
            for process in (owner, worker):
                if process.poll() is None:
                    process.kill()
                process.communicate(timeout=5)

    def test_killing_worker_closes_descendant_inherited_pipe(self):
        code = ("from worker_job import bind_worker_lifetime; import subprocess,sys,time; "
                "bind_worker_lifetime(); "
                "child=subprocess.Popen([sys.executable,'-c','import time; time.sleep(60)']); "
                "print('child-started',flush=True); time.sleep(60)")
        worker = subprocess.Popen([sys.executable, "-c", code],
            cwd=Path(__file__).parent, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            ready = queue.Queue()
            threading.Thread(target=lambda: ready.put(worker.stdout.readline()), daemon=True).start()
            self.assertIn("child-started", ready.get(timeout=10))
            worker.kill()
            # The child inherits stdout: EOF is impossible while it survives.
            worker.communicate(timeout=5)
        finally:
            if worker.poll() is None:
                worker.kill()
            worker.communicate(timeout=5)
