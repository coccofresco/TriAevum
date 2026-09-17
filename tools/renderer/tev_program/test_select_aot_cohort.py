import unittest
from select_aot_cohort import select


class SelectionTests(unittest.TestCase):
    def test_shared_members_and_observations_are_not_added_twice(self):
        functions = {e: {'name': str(e)} for e in range(1, 5)}
        calls = {1: {3}, 2: {3, 4}, 3: set(), 4: set()}
        result = select(functions, calls, set(functions), [((1, 2), 10)], 4)
        self.assertEqual(result['functions'], ['00000001', '00000002', '00000003', '00000004'])
        self.assertEqual(result['observed_samples_under_roots'], 10)

    def test_never_pads_with_unobserved_roots(self):
        functions = {1: {'name': 'one'}, 2: {'name': 'two'}}
        with self.assertRaisesRegex(ValueError, 'only 1'):
            select(functions, {1: set(), 2: set()}, {1, 2}, [((1,), 1)], 2)

    def test_rejects_nonpositive_minimum(self):
        with self.assertRaises(ValueError):
            select({}, {}, set(), [], 0)
