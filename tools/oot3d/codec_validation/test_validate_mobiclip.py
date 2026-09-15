import tempfile
import unittest
from pathlib import Path

from validate_mobiclip import compare, passed, repair_planar


class ComparisonTests(unittest.TestCase):
    def check_streams(self, a, b):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root/'a').write_bytes(a)
            (root/'b').write_bytes(b)
            return compare(root/'a', root/'b', 4)

    def test_exact(self):
        result = self.check_streams(b'abcd1234', b'abcd1234')
        self.assertTrue(result['complete'])
        self.assertEqual(result['compared_frames'], 2)
        self.assertEqual(result['differing_bytes'], 0)

    def test_mismatch(self):
        result = self.check_streams(b'abcd1234', b'abcd2234')
        self.assertEqual(result['first_mismatch'], 1)
        self.assertEqual(result['differing_bytes'], 1)

    def test_truncation(self):
        for a, b in [(b'', b''), (b'abc', b'abc'), (b'abcd', b''),
                     (b'abcd1', b'abcd1'), (b'abcd', b'abcd1234')]:
            with self.subTest(a=a, b=b):
                self.assertFalse(self.check_streams(a, b)['complete'])

    def test_frame_count_required(self):
        entry = dict(returncode=0, decoder=dict(status=0, decoded_frames=1),
                     requested_frames=2, comparison=self.check_streams(b'abcd', b'abcd'))
        self.assertFalse(passed(entry))
        entry['requested_frames'] = 1
        self.assertTrue(passed(entry))

    def test_patch_is_explicit_and_guarded(self):
        old = 'const s32 shift = size == 16 ? 3 : 2;'
        self.assertIn('AdjustPlanar', repair_planar(old))
        for source in ['', old + old, repair_planar(old)]:
            with self.assertRaises(ValueError):
                repair_planar(source)


if __name__ == '__main__':
    unittest.main()
