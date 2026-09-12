import copy
import unittest

from verify_topscreen_item_probes import verify


class TopScreenPhaseProbeTests(unittest.TestCase):
    def setUp(self):
        self.values = {
            "topscreen_input_native_updates": 400,
            "topscreen_input_zr_press_updates": 3,
            "topscreen_input_zl_press_updates": 2,
            "topscreen_items_selection_updates": 100,
            "topscreen_items_selection_begins": 2,
            "topscreen_items_selection_completions": 2,
            "topscreen_item_query_calls": [19, 18, 19, 18],
            "topscreen_item_query_true_results": [1, 0, 1, 0],
        }
        self.expected = dict(updates=400, zr_presses=3, zl_presses=2, assignments=2)

    def test_nested_matching_live_counters(self):
        self.assertEqual(verify([{"stats": self.values}, {"stats": self.values}],
                                **self.expected), self.values)

    def test_equal_but_inactive_probes_fail(self):
        self.values["topscreen_items_selection_completions"] = 0
        with self.assertRaisesRegex(ValueError, "expected 2"):
            verify([self.values, self.values], **self.expected)

    def test_phase_dependent_query_fails(self):
        other = copy.deepcopy(self.values)
        other["topscreen_item_query_true_results"][0] = 2
        with self.assertRaisesRegex(ValueError, "differs"):
            verify([self.values, other], **self.expected)

    def test_missing_duplicate_or_malformed_counter_fails(self):
        for changed in ({}, {"a": self.values, "b": self.values},
                        {**self.values, "topscreen_item_query_calls": []},
                        {**self.values, "topscreen_input_native_updates": True}):
            with self.assertRaises(ValueError):
                verify([self.values, changed], **self.expected)


if __name__ == "__main__":
    unittest.main()
