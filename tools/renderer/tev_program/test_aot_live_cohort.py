import unittest
from run_aot_live_cohort_probe import select_roots


class LiveSelectionTests(unittest.TestCase):
    def test_only_matching_roots_can_be_activated(self):
        q={'passed_roots':['003723C0','002BBF74']}
        self.assertEqual(select_roots(q,['003723C0']),['003723C0'])
        with self.assertRaises(ValueError):select_roots(q,['00368704'])

    def test_empty_qualification_rejected(self):
        with self.assertRaises(ValueError):select_roots({'passed_roots':[]},None)

    def test_deduplicate_and_sort(self):
        self.assertEqual(select_roots({'passed_roots':['3000','2000','2000']},None),['00002000','00003000'])

    def test_unaligned_or_out_of_range_rejected(self):
        for pc in ['0','2001','100000000']:
            with self.assertRaises(ValueError):select_roots({'passed_roots':[pc]},None)
