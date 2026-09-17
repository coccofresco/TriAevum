import unittest

from build_aot_direct_cohort import aot, render_family, render_shard
from test_build_aot_family_probe import function


def leaf(entry, raw=0xE12FFF1E):
    block = aot.Block(entry, entry, entry+4,
                      (aot.Instruction(entry, raw),), ({'kind': 'return'},))
    return aot.Function(entry, f'leaf_{entry:X}', (block,), (), (entry,))


class DirectCohortTests(unittest.TestCase):
    def setUp(self):
        self.program = {'functions': [function(0x1000)]}
        self.functions = {0x1000: leaf(0x1000)}

    def test_direct_wrapper_preserves_context_and_observed_path(self):
        code = render_shard(self.program, list(self.functions.values()),
                            self.functions, frozenset(), {0x1000})
        self.assertIn('pc == 0x00001000U', code)
        self.assertIn('::Eligible(c)', code)
        self.assertIn('~Restore() { c.BlockEntry = saved; }', code)
        self.assertIn('_Observed(f,c,s,pc)', code)
        self.assertNotIn('ObservableExit(', code)
        self.assertNotIn('original->', code)
        self.assertNotIn('BlocksRemaining +=', code)
        self.assertNotIn('state.Flush(0x7FFFU', code)

    def test_control_has_no_specialization(self):
        code = render_shard(self.program, list(self.functions.values()),
                            self.functions, frozenset(), set())
        self.assertNotIn('namespace Direct_', code)
        self.assertNotIn('struct Restore', code)

    def test_guard_checks_whole_closure_not_only_root(self):
        self.program['functions'] = [function(0x1000, [0x2000]), function(0x2000)]
        self.functions[0x2000] = leaf(0x2000)
        code = '\n'.join(render_family(self.program, 0x1000, self.functions))
        self.assertIn('if (!c.BlockEntry) return true;', code)
        self.assertIn('!c.BlockEntryPcs || !c.BlockEntryPcCount', code)
        self.assertIn('end,0x00002000U', code)
        self.assertIn('*p < 0x00002004U', code)

    def test_tail_target_stays_in_local_family(self):
        self.program['functions'] = [function(0x1000, tail_calls=[{'target': 0x2000}]),
                                     function(0x2000)]
        self.functions[0x2000] = leaf(0x2000)
        code = '\n'.join(render_family(self.program, 0x1000, self.functions))
        self.assertIn('switch(flow.Pc)', code)
        self.assertIn('case 0x00002000U: return Execute_', code)

    def test_svc_is_not_silently_unobserved(self):
        self.functions[0x1000] = leaf(0x1000, 0xEF000001)
        with self.assertRaisesRegex(ValueError, 'Observable'):
            render_family(self.program, 0x1000, self.functions)


if __name__ == '__main__':
    unittest.main()
