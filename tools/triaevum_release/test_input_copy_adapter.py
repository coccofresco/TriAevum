import copy
from pathlib import Path
import tempfile
import unittest

from tools.triaevum_release.input_copy_adapter import apply_program, build_program, normalize_file


class InputCopyAdapterTests(unittest.TestCase):
    def setUp(self):
        self.source = bytes(range(256)) * 8
        self.target = self.source[100:700] + b"abc" + self.source[:80]
        self.program = build_program(self.source, self.target)

    def test_round_trip_reorder_and_reuse(self):
        self.assertEqual(apply_program(self.source, self.program), self.target)
        self.assertTrue(all(len(op) == 2 and all(type(x) is int for x in op) for op in self.program['copies']))

    def test_wrong_source_rejected(self):
        with self.assertRaisesRegex(ValueError, 'source identity'):
            apply_program(self.source + b'1', self.program)

    def test_changed_program_and_output_hash_rejected(self):
        program = copy.deepcopy(self.program)
        program['copies'][0][0] += 1
        with self.assertRaisesRegex(ValueError, 'output identity'):
            apply_program(self.source, program)

    def test_invalid_operations_rejected(self):
        for operation in ([-1, 10], [0, 0], [len(self.source), 1], [0, 100000], [True, 1], ['0', 1]):
            program = copy.deepcopy(self.program)
            program['copies'] = [operation]
            with self.assertRaises(ValueError):
                apply_program(self.source, program)

    def test_absent_byte_not_synthesized(self):
        with self.assertRaises(ValueError):
            build_program(b'a' * 32, b'b')

    def test_file_source_and_previous_output_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory)/'source', Path(directory)/'output'
            source.write_bytes(self.source)
            normalize_file(source, output, self.program)
            self.assertEqual(source.read_bytes(), self.source)
            self.assertEqual(output.read_bytes(), self.target)
            with self.assertRaises(ValueError):
                normalize_file(source, output, self.program)
            with self.assertRaises(ValueError):
                normalize_file(source, source, self.program)


if __name__ == '__main__':
    unittest.main()
