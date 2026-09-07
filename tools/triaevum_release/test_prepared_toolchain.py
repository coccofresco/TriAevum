import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from common import atomic_write_json, sha256_file
from prepared_toolchain import select_sysroot


class PreparedToolchainTests(unittest.TestCase):
    def test_missing_is_optional_but_invalid_present_generation_is_not(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            kwargs = {"compiler": root / "compiler", "support": root / "support"}
            generation = root / "toolchain"
            self.assertIsNone(select_sysroot(generation, **kwargs))
            generation.mkdir()
            with self.assertRaises((ValueError, OSError)):
                select_sysroot(generation, **kwargs)

    def test_requires_current_binary_and_sysroot_identities(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            compiler, support = root / "compiler", root / "support"
            compiler.write_bytes(b"compiler")
            support.write_bytes(b"support")
            (root / "sysroot").mkdir()
            proof = {"status": "passed", "sysroot_identity": "identity",
                     "compiler_sha256": sha256_file(compiler), "support_sha256": sha256_file(support),
                     "checks": {name: True for name in (
                         "arithmetic", "memory", "callback", "tls", "observable_exit", "abi_rejection")}}
            receipt = {"format": "triaevum_prepared_toolchain_v1", "status": "native_probe_passed",
                       "sysroot": "sysroot", "identity": "identity", "native_probe": proof}
            atomic_write_json(root / "toolchain.json", receipt)
            with patch("prepared_toolchain.load_sysroot", return_value=SimpleNamespace(
                    identity="identity", root=root / "sysroot")):
                self.assertEqual(select_sysroot(root, compiler=compiler, support=support), root / "sysroot")
                proof["checks"]["tls"] = False
                atomic_write_json(root / "toolchain.json", receipt)
                with self.assertRaisesRegex(ValueError, "incomplete"):
                    select_sysroot(root, compiler=compiler, support=support)
                proof["checks"]["tls"] = True
                atomic_write_json(root / "toolchain.json", receipt)
                support.write_bytes(b"changed")
                with self.assertRaisesRegex(ValueError, "stale"):
                    select_sysroot(root, compiler=compiler, support=support)
                previous = (root / "toolchain.json").read_bytes()
                with self.assertRaisesRegex(ValueError, "invalid"):
                    select_sysroot(root, compiler=compiler, support=support, reprobe=lambda _: {})
                self.assertEqual((root / "toolchain.json").read_bytes(), previous)
                updated = {**proof, "support_sha256": sha256_file(support)}
                self.assertEqual(select_sysroot(root, compiler=compiler, support=support,
                    reprobe=lambda _: updated), root / "sysroot")
                def unexpected(_):
                    self.fail("unchanged toolchain must not recompile the proof")
                self.assertEqual(select_sysroot(root, compiler=compiler, support=support,
                    reprobe=unexpected), root / "sysroot")
