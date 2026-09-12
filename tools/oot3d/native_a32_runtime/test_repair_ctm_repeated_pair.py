import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('repair_ctm', Path(__file__).with_name('repair_ctm_repeated_pair.py'))
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


class RepairTests(unittest.TestCase):
    pair = bytes.fromhex('00 00 00 6d 00 6d 00 01 00 00 00 00 00 00')

    def movie(self, body):
        header = bytearray(256)
        header[:4] = b'CTM\x1b'
        struct.pack_into('<Q', header, 0x54, sum(body[i] == 0 for i in range(0,len(body),7)))
        return bytes(header)+body

    def test_exact_rotation_recovered(self):
        pair = self.pair
        data = self.movie(pair + pair[1:]+pair[:1] + pair)
        fixed, report = tool.repair(data)
        self.assertEqual(fixed[256:], pair*3)
        self.assertEqual(report['declared_pad_count_after'], 3)
        self.assertEqual(len(report['repairs']), 1)
        self.assertFalse(report['sync_verified'])

    def test_clean_input_is_unchanged(self):
        data = self.movie(self.pair*3)
        self.assertEqual(tool.repair(data)[0], data)

    def test_different_neighbors_rejected(self):
        other = bytes.fromhex('00 00 00 6c 00 6c 00 01 00 00 00 00 00 00')
        with self.assertRaisesRegex(ValueError, 'Not a single-byte rotation'):
            tool.repair(self.movie(self.pair + self.pair[1:]+self.pair[:1] + other))

    def test_non_rotation_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Not a single-byte rotation'):
            tool.repair(self.movie(self.pair + bytes(14) + self.pair))

    def test_missing_reference_rejected(self):
        with self.assertRaisesRegex(ValueError, 'No unambiguous'):
            tool.repair(self.movie(self.pair[1:]+self.pair[:1]+self.pair))


if __name__ == '__main__':
    unittest.main()
