import unittest
from run_aot_invocation_probe import batch_result


def rows():
    return [['0','baseline','100','100','1','1'],
            ['1','candidate','100','100','1','1'],
            ['candidate_fast_hits','1'], ['visited_function','123','1'],
            ['summary','1','original_memory_untouched','1']]


class BatchTests(unittest.TestCase):
    def test_coverage_only_from_matching_native_candidate(self):
        self.assertEqual(batch_result(rows()), (True, {123}))

    def test_fallback_not_credited(self):
        data=rows(); data[2][1]='0'
        self.assertEqual(batch_result(data), (False, set()))

    def test_mismatch_or_live_mutation_not_credited(self):
        for row, column in [(1,5),(-1,3)]:
            data=rows(); data[row][column]='0'
            self.assertEqual(batch_result(data), (False,set()))

    def test_partial_output_not_credited(self):
        for data in [[],rows()[:-1],rows()[1:]]:
            self.assertEqual(batch_result(data), (False,set()))
