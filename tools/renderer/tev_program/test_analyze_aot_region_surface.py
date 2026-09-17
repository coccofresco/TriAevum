import unittest
from analyze_aot_region_surface import classify, summarize, memory_observer, code_hazards


def program():
    functions, blocks = [], []
    for entry, callees in ((1, []), (2, [1]), (3, [2, 1]), (4, [4]), (5, [6])):
        functions.append({'entry': entry, 'name': str(entry), 'blocks': [entry],
                          'closed_static_cfg': True, 'direct_calls': [{'target': c} for c in callees]})
        blocks.append({'id': entry, 'pc': entry, 'successors': [{'kind': 'return'}]})
    return {'functions': functions, 'blocks': blocks}


class RegionSurfaceTests(unittest.TestCase):
    def test_closures_exclude_recursion_and_unknown_callees(self):
        _, _, _, leaves, closed = classify(program(), set(), set())
        self.assertEqual(leaves, {1})
        self.assertEqual(closed, {1, 2, 3})

    def test_observer_or_external_propagates_to_callers(self):
        for external, hazards in (({1}, set()), (set(), {1})):
            self.assertFalse(classify(program(), external, hazards)[-1])

    def test_indirect_call_prevents_closed_family(self):
        p = program()
        p['functions'][0]['indirect_sites'] = [{'kind': 'indirect_call'}]
        self.assertFalse(classify(p, set(), set())[-1])

    def test_no_parent_child_or_shared_callee_double_count(self):
        a = {'aot_leaf_samples': 10, 'direct_owners': [
            {'entry': '1', 'category': 'memory_read_helper', 'count': 4},
            {'entry': '2', 'category': 'translated_body', 'count': 3}],
            'paths': [{'entries': ['1', '2', '3'], 'count': 4},
                      {'entries': ['2', '3'], 'count': 3}]}
        r = summarize(program(), [a])
        self.assertEqual(r['call_free']['exclusive_samples'], 4)
        self.assertEqual(r['closed_direct_calls']['exclusive_samples'], 7)
        self.assertEqual(r['closed_direct_calls']['share_of_aot_observations'], .7)
        root = next(f for f in r['ranked_closed_families'] if f['entry'] == '00000003')
        self.assertEqual(root['closure_samples_all_callers'], 7)
        self.assertEqual(root['observed_samples_under_root'], 7)
        self.assertEqual(r['greedy_observed_roots'][-1]['cumulative_distinct_samples'], 7)

    def test_unsampled_root_does_not_inherit_shared_callee_cost(self):
        a = {'aot_leaf_samples': 4, 'direct_owners': [
            {'entry': '1', 'category': 'memory_read_helper', 'count': 4}],
            'paths': [{'entries': ['1'], 'count': 4}]}
        r = summarize(program(), [a])
        self.assertEqual([f['entry'] for f in r['ranked_closed_families']], ['00000001'])

    def test_unaccounted_edge_excluded(self):
        p = program()
        p['blocks'][0]['successors'] = [{'kind': 'branch', 'target': 999}]
        self.assertFalse(classify(p, set(), set())[-1])

    def test_invalid_path_accounting_rejected(self):
        for paths in ([{'entries': [], 'count': 1}],
                      [{'entries': ['1'], 'count': -1}],
                      [{'entries': ['1'], 'count': 5}]):
            a = {'aot_leaf_samples': 4, 'direct_owners': [
                {'entry': '1', 'category': 'translated_body', 'count': 4}],
                'paths': paths}
            with self.assertRaises(ValueError):
                summarize(program(), [a])

    def test_arm_observers_and_code_identity(self):
        self.assertTrue(memory_observer(0xEF000001))
        self.assertTrue(memory_observer(0xE1900F9F))
        self.assertFalse(memory_observer(0xE5900000))
        self.assertTrue(memory_observer(0xEE100A10))
        self.assertFalse(memory_observer(0xEE100A10, True))
        self.assertTrue(memory_observer(0xEE100F10, True))
        with self.assertRaises(ValueError):
            code_hazards({'code_sha256': 'wrong'}, b'')


if __name__ == '__main__':
    unittest.main()
