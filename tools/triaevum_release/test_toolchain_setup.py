import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from common import atomic_write_json, sha256_file
from toolchain_setup import load_setup, run_setup


class SetupTests(unittest.TestCase):
    def test_pinned_plan_license_and_explicit_consent(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            plan, license_file, config = root / "plan.json", root / "license.txt", root / "setup.json"
            self.assertIsNone(load_setup(config))
            atomic_write_json(plan, {"fixture": True})
            license_file.write_text("fixture license", encoding="utf-8")
            atomic_write_json(config, {"format": "triaevum_toolchain_setup_v1", "plan": "plan.json",
                "plan_sha256": sha256_file(plan), "license": "license.txt", "license_sha256": sha256_file(license_file),
                "clang_resource": "clang", "clang_version": "22", "msvc_version": "14.44.35207", "sdk_version": "10.0.26100.0"})
            setup = load_setup(config)
            self.assertEqual(setup.license_text, "fixture license")
            kwargs = dict(compiler=root / "cc", archiver=root / "ar", support=root / "lib", include=root / "inc")
            with patch("toolchain_setup.prepare", return_value={"status": "passed"}) as prepare:
                with self.assertRaisesRegex(ValueError, "acceptance"):
                    run_setup(setup, root / "data", accepted_identity="wrong", **kwargs)
                prepare.assert_not_called()
                run_setup(setup, root / "data", accepted_identity=setup.identity, **kwargs)
                self.assertEqual(prepare.call_args.kwargs["output"], root / "data/toolchain")
            license_file.write_text("changed", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "license changed"):
                load_setup(config)
