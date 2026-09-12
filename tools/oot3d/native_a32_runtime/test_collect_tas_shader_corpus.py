import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('tas_collector',
    Path(__file__).with_name('collect_tas_shader_corpus.py'))
collector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(collector)


class MovieTests(unittest.TestCase):
    def inspect(self, events, declared=1):
        header = bytearray(256)
        header[:4] = b'CTM\x1b'
        struct.pack_into('<Q', header, 4, 0x4000000033500)
        struct.pack_into('<Q', header, 0x54, declared)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'movie.ctm'
            path.write_bytes(header + events)
            return collector.inspect_ctm(path)

    def test_counts_pad_inputs_not_all_device_events(self):
        result = self.inspect(bytes(7) + bytes([1]) + bytes(6))
        self.assertEqual(result['pad_input_count'], 1)
        self.assertEqual(result['event_counts']['touch'], 1)
        self.assertEqual(result['title_id'], '0004000000033500')

    def test_rejects_truncation(self):
        with self.assertRaisesRegex(ValueError, 'Truncated'):
            self.inspect(bytes(8))

    def test_rejects_header_count_mismatch(self):
        with self.assertRaisesRegex(ValueError, 'count'):
            self.inspect(bytes(7), declared=2)

    def test_legacy_zero_count_is_derived(self):
        self.assertEqual(self.inspect(bytes(7), declared=0)['pad_input_count'], 1)

    def test_unknown_device_is_not_silently_accepted(self):
        with self.assertRaisesRegex(ValueError, 'Unknown'):
            self.inspect(bytes([6])+bytes(6))

    def test_log_tail_does_not_reread_old_messages(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'log'
            self.assertEqual(collector.read_appended_log(path, 0), ('', 0))
            path.write_bytes(b'first')
            self.assertEqual(collector.read_appended_log(path, 0), ('first', 5))
            path.write_bytes(b'firstsecond')
            self.assertEqual(collector.read_appended_log(path, 5), ('second', 11))


if __name__ == '__main__':
    unittest.main()
