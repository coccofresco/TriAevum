import json
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from common import sha256_file
import interior_sun as sun


class InteriorSunTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.runtime = self.root / "TriAevum.exe"
        self.runtime.write_bytes(b"neutral runtime fixture")
        self.record = {"path": self.runtime.name, "bytes": self.runtime.stat().st_size,
                       "sha256": sha256_file(self.runtime)}
        self.cache = self.root / "renderer-cache"
        self.calls = []
        self.mutate = lambda result: None

    def invoke(self, command, **kwargs):
        self.calls.append((command, kwargs))
        self.assertEqual(command[:2], [str(self.runtime), "--prebuild-interior-sun"])
        cache = Path(command[command.index("--renderer-cache-directory") + 1])
        relative = "shadow-shells/0000000000000001.bin"
        path = cache / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        # Empty valid v3 mesh, with the renderer's documented checksum basis.
        data = struct.pack("<4sIQ6I", b"RSSH", 3, 1, 64, 512, 0, 0, 1, 0)
        checksum = 1469598103934665603
        for byte in data:
            checksum = ((checksum ^ byte) * 1099511628211) & ((1 << 64) - 1)
        path.write_bytes(data + struct.pack("<Q", checksum))
        result = {"format": sun.FORMAT, "criterion_version": 3, "game_booted": False,
                  "cache_directory": str(cache), "rooms": 1, "fallback": 0,
                  "entries": [{"status": "built", "fingerprint": "0000000000000001",
                               "interior": True, "path": relative}]}
        self.mutate(result)
        Path(command[command.index("--manifest") + 1]).write_text(json.dumps(result))
        return subprocess.CompletedProcess(command, 0, "", "")

    def prepare(self, identity="a" * 64):
        with patch.object(sun, "run_native", side_effect=self.invoke):
            return sun.prepare_interior_sun(root=self.root, data_root=self.root / "data",
                runtime_record=self.record, romfs=self.root / "user-romfs", romfs_sha256=identity,
                cache_directory=self.cache)

    def test_reuse_and_missing_or_corrupt_cache_regeneration(self):
        self.assertFalse(self.prepare()["receipt_reused"])
        self.assertTrue(self.prepare()["receipt_reused"])
        self.assertEqual(len(self.calls), 1)
        path = next((self.cache / "shadow-shells").glob("*.bin"))
        data = path.read_bytes()
        path.write_bytes(data[:-1] + bytes([data[-1] ^ 1]))
        self.assertFalse(self.prepare()["receipt_reused"])
        path.unlink()
        self.assertFalse(self.prepare()["receipt_reused"])
        self.assertEqual(len(self.calls), 3)
        self.assertIn("env", self.calls[0][1])
        self.assertEqual(self.calls[0][1]["timeout"], 3600)

    def test_rom_runtime_and_criterion_invalidate_receipt(self):
        self.prepare()
        self.assertFalse(self.prepare("b" * 64)["receipt_reused"])
        self.runtime.write_bytes(b"new runtime fixture")
        self.record.update(bytes=self.runtime.stat().st_size, sha256=sha256_file(self.runtime))
        self.assertFalse(self.prepare("b" * 64)["receipt_reused"])
        # A receipt from a previous criterion cannot match the active identity.
        for path in (self.root / "data" / "interior-sun").glob("*.json"):
            receipt = json.loads(path.read_text())
            receipt["identity"]["criterion_version"] = 2
            path.write_text(json.dumps(receipt))
        self.assertFalse(self.prepare("b" * 64)["receipt_reused"])

    def test_reject_wrong_class_path_version_and_game_boot(self):
        for mutate in (
            lambda r: r.update(game_booted=True),
            lambda r: r.update(criterion_version=2),
            lambda r: r["entries"][0].update(interior=False),
            lambda r: r["entries"][0].update(path="../outside.bin"),
            lambda r: r.update(entries=[]),
        ):
            with self.subTest(mutate=mutate):
                self.mutate = mutate
                with self.assertRaises(ValueError):
                    self.prepare()
        self.assertFalse(list((self.root / "data" / "interior-sun").glob("*.json")))

    def test_native_failure_does_not_publish_receipt(self):
        with patch.object(sun, "run_native", return_value=subprocess.CompletedProcess([], 1, "", "failure")):
            with self.assertRaisesRegex(ValueError, "failure"):
                sun.prepare_interior_sun(root=self.root, data_root=self.root / "data",
                    runtime_record=self.record, romfs=self.root / "romfs", romfs_sha256="a" * 64,
                    cache_directory=self.cache)
        self.assertFalse(list((self.root / "data" / "interior-sun").glob("*.json")))

    def test_release_mask_table_is_passed_and_bound_to_the_receipt(self):
        table = self.root / "interior-sun.masks"
        table.write_bytes(b"first table")
        self.mutate = lambda r: r["entries"][0].update(status="precomputed")
        with patch.object(sun, "run_native", side_effect=self.invoke):
            prepare = lambda: sun.prepare_interior_sun(root=self.root, data_root=self.root / "data",
                runtime_record=self.record, romfs=self.root / "user-romfs", romfs_sha256="a" * 64,
                cache_directory=self.cache, mask_table=table)
            self.assertFalse(prepare()["receipt_reused"])
            self.assertTrue(prepare()["receipt_reused"])
            table.write_bytes(b"second table")
            self.assertFalse(prepare()["receipt_reused"])
        command = self.calls[0][0]
        self.assertEqual(command[command.index("--mask-table") + 1], str(table.resolve()))
        self.assertFalse(self.prepare()["receipt_reused"])
        self.assertNotIn("--mask-table", self.calls[-1][0])

    def test_documented_native_fallback_is_reusable(self):
        self.mutate = lambda r: r.update(fallback=1, entries=[
            {"status": "fallback", "reason": "unsupported CMB geometry"}])
        self.assertEqual(self.prepare()["fallback"], 1)
        self.assertTrue(self.prepare()["receipt_reused"])


if __name__ == "__main__":
    unittest.main()
