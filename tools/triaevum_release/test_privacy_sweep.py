"""Privacy sweep: clean on the tree, refuses every planted leak."""
from __future__ import annotations

from pathlib import Path
import contextlib
import io
import tempfile
import unittest

import privacy_sweep

REPO = Path(__file__).resolve().parents[2]

# Planted leaks are assembled at run time so this file never contains them.
DRIVE = "Q" + ":/"
WORK = "_triaevum" + "_work"


class PrivacySweepTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def run_sweep(self, *arguments: str) -> tuple[int, str]:
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            status = privacy_sweep.main(["--root", str(self.root), "--no-host-terms", *arguments])
        return status, output.getvalue()

    def write(self, name: str, text: str) -> None:
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def test_documented_placeholders_are_clean(self) -> None:
        self.write("docs/a.md", "\n".join([
            "Evidence under `W/analysis/parity/` and `<work>/x`.",
            "Home: C:/Users/<user>/AppData, %USERPROFILE%, /home/<user>, ~/x, $HOME/y.",
            "Container: /home/work/forge and /home/src/scripts.",
            "Tools: C:/Program Files/LLVM, D:/Games/TriAevum, X:/fixture/a.json.",
            "Archive paths rom:/scene and sdmc:/save are guest paths.",
            "SDK 10.0.22621.0 and URL https://example.com/a are fine.",
        ]))
        status, output = self.run_sweep()
        self.assertEqual(status, 0, output)

    def test_every_planted_leak_is_refused(self) -> None:
        leaks = {
            "local-drive-path": f"see {DRIVE}TriAevum-integ/build",
            "user-profile-path": "C:" + "/Users/maintainer/AppData/x",
            "user-home-path": "/home/" + "maintainer/build",
            "work-directory-name": f"lock at {WORK}/gpu-run.lock",
            "private-key": "-----BEGIN OPENSSH " + "PRIVATE KEY-----",
            "private-address": "ssh 192.168" + ".1.20",
            "private-name": "built by Maintainer-Host",
        }
        for rule, line in leaks.items():
            with self.subTest(rule=rule):
                self.write("leak.txt", line + "\n")
                status, output = self.run_sweep("--term", "maintainer-host")
                self.assertEqual(status, 1, output)
                self.assertIn(f"leak.txt:1: {rule}", output)
                if rule == "private-name":
                    self.assertNotIn("maintainer-host", output.lower())

    def test_allow_marker_documents_an_exception(self) -> None:
        self.write("a.md", f"historic {DRIVE}x  <!-- privacy-sweep: allow -->\n")
        self.assertEqual(self.run_sweep()[0], 0)

    def test_tracked_tree_is_clean(self) -> None:
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            status = privacy_sweep.main(["--root", str(REPO)])
        self.assertEqual(status, 0, output.getvalue())


if __name__ == "__main__":
    unittest.main()
