import contextlib
import io
import unittest
from pathlib import Path
from unittest.mock import patch

import forge


class NativeProbeCommandTests(unittest.TestCase):
    def test_probe_command_routes_explicit_paths(self):
        names = ("compiler", "archiver", "support", "include", "sysroot", "output")
        args = ["verify-toolchain"]
        for name in names:
            args.extend(["--" + name, name])
        with patch("validate_whole_aot_toolchain.validate", return_value={"status": "passed"}) as validate:
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(forge.main(args), 0)
            validate.assert_called_once_with(*(Path(name) for name in names))
