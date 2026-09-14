import struct
import unittest

from inspect_cmb_shader_surface import inspect_cmb, inspect_shbin


def model():
    data = bytearray(0x220)
    data[:4] = b"cmb "
    struct.pack_into("<II", data, 4, len(data), 6)
    struct.pack_into("<I", data, 0x28, 0x44)
    struct.pack_into("<I", data, 0x2C, 0x1D4)
    struct.pack_into("<I", data, 0x34, 0x200)
    data[0x44:0x48] = b"mats"
    struct.pack_into("<II", data, 0x48, 0x18C, 1)
    data[0x1D4:0x1D8] = b"tex "
    data[0x51] = 1
    struct.pack_into("<I", data, 0x50 + 0x120, 1)
    struct.pack_into("<HH", data, 0x50 + 0x15C, 0x2100, 0x1E01)
    data[0x200:0x204] = b"luts"
    struct.pack_into("<I", data, 0x204, 0x20)
    return data


class SurfaceTests(unittest.TestCase):
    def test_material_and_tev_table(self):
        count, luts, keys, ops, flags = inspect_cmb(model())
        self.assertEqual((count, luts, keys), (1, 0, 0))
        self.assertEqual(ops, {"2100/1E01": 1})
        self.assertEqual(flags, {"vertex=1 fragment=0": 1})

    def test_negative_stage_rejected(self):
        data = model()
        struct.pack_into("<h", data, 0x50 + 0x124, -1)
        with self.assertRaisesRegex(ValueError, "negative"):
            inspect_cmb(data)

    def test_stage_must_stay_inside_mats(self):
        data = model()
        struct.pack_into("<H", data, 0x50 + 0x124, 2)
        with self.assertRaisesRegex(ValueError, "out-of-bounds"):
            inspect_cmb(data)

    def test_too_many_stages(self):
        data = model()
        struct.pack_into("<I", data, 0x50 + 0x120, 7)
        with self.assertRaisesRegex(ValueError, "six"):
            inspect_cmb(data)

    def test_truncated_model(self):
        with self.assertRaises(ValueError):
            inspect_cmb(model()[:0x100])

    def test_truncated_shader(self):
        with self.assertRaises(ValueError):
            inspect_shbin(b"DVLB\x01\0\0\0")


if __name__ == "__main__":
    unittest.main()
