import unittest
from summarize_cpu_budget import summarize


def fixture():
    return {'measured_frames': 1000, 'host_seconds': 10,
            'thread_cpu_ledger': {'requested': True, 'read_failures': 0,
                'phases': {
                    'aot': {'wall_seconds': 8, 'user_seconds': 2, 'kernel_seconds': 0,
                            'thread_cycles': 100},
                    'present_excluded': {'wall_seconds': 2, 'user_seconds': 7.5,
                                         'kernel_seconds': 0, 'thread_cycles': 20}}}}


class CpuBudgetTests(unittest.TestCase):
    def test_quantized_phase_cpu_is_not_used_as_exclusive_cpu(self):
        result = summarize(fixture())
        self.assertAlmostEqual(result['pre_nri_cpu_lower_ms'], 7.468)
        self.assertEqual(result['pre_nri_cpu_upper_ms'], 8)
        self.assertAlmostEqual(result['pre_nri_off_cpu_upper_ms'], .532)

    def test_invalid_accounting_rejected(self):
        value = fixture()
        value['host_seconds'] = 11
        with self.assertRaises(ValueError):
            summarize(value)
        value = fixture()
        value['thread_cpu_ledger']['read_failures'] = 1
        with self.assertRaises(ValueError):
            summarize(value)

    def test_window_cpu_schema(self):
        value = fixture()
        value['thread_cpu_ledger']['window_thread_cpu_seconds'] = 9.5
        for phase in value['thread_cpu_ledger']['phases'].values():
            del phase['user_seconds'], phase['kernel_seconds']
        self.assertAlmostEqual(summarize(value)['pre_nri_cpu_lower_ms'], 7.468)


if __name__ == '__main__':
    unittest.main()
