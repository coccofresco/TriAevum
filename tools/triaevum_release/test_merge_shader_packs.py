import tempfile
import unittest
from pathlib import Path

try:
    from .merge_shader_packs import decode, encode, merge
except ImportError:
    from merge_shader_packs import decode, encode, merge

SPIRV = b"\x03\x02\x23\x07" + bytes(16)
KEY = (2, 1, 2, 3)


class MergeShaderPacksTests(unittest.TestCase):
    def test_roundtrip(self):
        modules = {KEY: SPIRV, (3, 4, 5, 6): SPIRV}
        self.assertEqual(decode(encode(3, modules)), (3, modules))

    def test_union_and_idempotence(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            a, b, out, receipt = [root / name for name in ("a", "b", "out", "receipt")]
            a.write_bytes(encode(3, {KEY: SPIRV}))
            b.write_bytes(encode(3, {KEY: SPIRV, (3, 4, 5, 6): SPIRV}))
            result = merge([a, b], out, receipt)
            self.assertEqual(result["modules"], 2)
            self.assertEqual([x["added"] for x in result["inputs"]], [1, 1])
            first = out.read_bytes()
            merge([b, a], out, receipt)
            self.assertEqual(first, out.read_bytes())

    def test_reject_conflict_and_schema_without_publishing(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            a, b, out, receipt = [root / name for name in ("a", "b", "out", "receipt")]
            a.write_bytes(encode(3, {KEY: SPIRV}))
            for schema, payload in ((4, SPIRV), (3, SPIRV + bytes(4))):
                b.write_bytes(encode(schema, {KEY: payload}))
                with self.assertRaises(ValueError):
                    merge([a, b], out, receipt)
                self.assertFalse(out.exists())
            with self.assertRaises(ValueError):
                merge([a], a, receipt)

    def test_reject_corruption_and_every_truncation(self):
        data = encode(3, {KEY: SPIRV})
        for size in range(len(data)):
            with self.assertRaises(ValueError):
                decode(data[:size])
        corrupt = bytearray(data)
        corrupt[-1] ^= 1
        with self.assertRaises(ValueError):
            decode(corrupt)

    def test_reject_duplicate_identity(self):
        data = bytearray(encode(3, {KEY: SPIRV, (3, 4, 5, 6): SPIRV}))
        data[80:112] = data[24:56]
        with self.assertRaises(ValueError):
            decode(data)


if __name__ == "__main__":
    unittest.main()
