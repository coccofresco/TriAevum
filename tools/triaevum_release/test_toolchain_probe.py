import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from toolchain_probe import probe_toolchain, recommended_compile_jobs


class ToolchainProbeTests(unittest.TestCase):
    def test_missing_tools_fail_before_invoking_compiler(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with patch("toolchain_probe.subprocess.run") as run:
                with self.assertRaisesRegex(ValueError, "incomplete"):
                    probe_toolchain(root / "clang-cl.exe", root / "llvm-lib.exe",
                                    root / "support.lib", root / "include")
                run.assert_not_called()

    def test_default_parallelism_keeps_headroom(self):
        with patch("toolchain_probe.os.cpu_count", return_value=12):
            self.assertLessEqual(recommended_compile_jobs(), 4)
            self.assertGreaterEqual(recommended_compile_jobs(), 1)
