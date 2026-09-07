import tempfile
import unittest
from contextlib import ExitStack
from pathlib import Path
from unittest.mock import patch

from prepare_windows_toolchain import prepare


class ToolchainPreparationTests(unittest.TestCase):
    def test_publication_requires_matching_native_proof_and_retains_components(self):
        with tempfile.TemporaryDirectory() as temporary, ExitStack() as stack:
            root = Path(temporary)
            def acquire(plan, cache, output, **kwargs):
                output.mkdir()
                (output / "license.txt").write_text("fixture")
                return {"fixture": True}
            def assemble(trees, **kwargs):
                kwargs["output"].mkdir()
                return {"identity": "verified"}
            stack.enter_context(patch("prepare_windows_toolchain.acquire_components", side_effect=acquire))
            stack.enter_context(patch("prepare_windows_toolchain.component_trees", return_value=[]))
            stack.enter_context(patch("prepare_windows_toolchain.assemble", side_effect=assemble))
            probe = stack.enter_context(patch("prepare_windows_toolchain.validate"))
            kwargs = dict(cache=root / "cache", output=root / "output", diagnostics=root / "logs",
                          compiler=root / "clang", archiver=root / "lib", support=root / "support",
                          include=root / "include", clang_resource=root / "resource", clang_version="22",
                          msvc_version="14.44.35207", sdk_version="10.0.26100.0", accept_licenses=True)
            for result in ({"status": "failed", "sysroot_identity": "verified"},
                           {"status": "passed", "sysroot_identity": "wrong"}):
                probe.return_value = result
                with self.assertRaisesRegex(ValueError, "does not match"):
                    prepare({}, **kwargs)
                self.assertFalse(kwargs["output"].exists())
                self.assertEqual(list(root.glob(".toolchain-prepare-*")), [])
            probe.return_value = {"status": "passed", "sysroot_identity": "verified"}
            result = prepare({}, **kwargs)
            self.assertEqual(result["status"], "native_probe_passed")
            self.assertEqual((kwargs["output"] / "components/license.txt").read_text(), "fixture")
            with self.assertRaises(FileExistsError):
                prepare({}, **kwargs)

    def test_no_consent_no_acquisition(self):
        with patch("prepare_windows_toolchain.acquire_components") as acquire:
            with self.assertRaisesRegex(ValueError, "licenses"):
                prepare({}, cache=Path("cache"), output=Path("output"), diagnostics=Path("logs"),
                        compiler=Path("cc"), archiver=Path("ar"), support=Path("lib"), include=Path("inc"),
                        clang_resource=Path("res"), clang_version="22", msvc_version="14.44.35207",
                        sdk_version="10.0.26100.0", accept_licenses=False)
            acquire.assert_not_called()
