from pathlib import Path
import unittest
from analyze_aot_leaf_ranges import summarize


class LeafRangeTests(unittest.TestCase):
    def test_exclusive_leaf_and_relocated_base(self):
        module = Path('module.dll')
        data = {'modules': [{'path': str(module.resolve()), 'base': 4096, 'size': 256}],
                'stacks': [{'pcs': [4112, 4128], 'count': 3},
                           {'pcs': [4128], 'count': 2},
                           {'pcs': [9000, 4112], 'count': 4}]}
        ranges = [{'name': 'first', 'begin_rva': 16, 'end_rva': 32}]
        r = summarize(data, module, ranges)
        self.assertEqual(r['aot_observations'], 5)
        self.assertEqual(r['ranges'], {'first': 3})
        self.assertEqual(r['outside_selected_ranges'], 2)

    def test_overlap_rejected(self):
        module = Path('module.dll')
        data = {'modules': [{'path': str(module.resolve()), 'base': 0, 'size': 256}],
                'stacks': []}
        with self.assertRaises(ValueError):
            summarize(data, module, [{'name': 'a', 'begin_rva': 1, 'end_rva': 3},
                                     {'name': 'b', 'begin_rva': 2, 'end_rva': 4}])

    def test_missing_module_rejected(self):
        with self.assertRaises(ValueError):
            summarize({'modules': [], 'stacks': []}, Path('module.dll'), [])


if __name__ == '__main__':
    unittest.main()
