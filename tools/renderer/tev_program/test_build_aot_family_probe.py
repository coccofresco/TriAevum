import unittest
from build_aot_family_probe import closure


def function(entry, callees=(), **changes):
    result={'entry':entry,'closed_static_cfg':True,'indirect_sites':[],
        'unresolved_static_edges':[],'blocks':[entry],
        'direct_calls':[{'target':e} for e in callees],'tail_calls':[]}
    result.update(changes)
    return result


class FamilyTests(unittest.TestCase):
    def test_shared_callee_is_included_once(self):
        p={'functions':[function(1,[2,3]),function(2,[4]),function(3,[4]),function(4)]}
        self.assertEqual(closure(p,1,4),[1,2,3,4])

    def test_tail_closure(self):
        p={'functions':[function(1,tail_calls=[{'target':2}]),function(2)]}
        self.assertEqual(closure(p,1,2),[1,2])

    def test_cycle_rejected(self):
        with self.assertRaisesRegex(ValueError,'Recursive'):
            closure({'functions':[function(1,[2]),function(2,[1])]},1,10)

    def test_budget_rejected(self):
        with self.assertRaisesRegex(ValueError,'budget'):
            closure({'functions':[function(1,[2]),function(2)]},1,1)

    def test_open_boundaries_rejected(self):
        for changes in [{'indirect_sites':[2]}, {'closed_static_cfg':False},
                        {'unresolved_static_edges':[2]}, {'blocks':[]}]:
            with self.subTest(changes=changes),self.assertRaisesRegex(ValueError,'Open'):
                closure({'functions':[function(1,**changes)]},1,10)

    def test_missing_callee_not_ignored(self):
        with self.assertRaises(KeyError):
            closure({'functions':[function(1,[2])]},1,10)


if __name__=='__main__':
    unittest.main()
