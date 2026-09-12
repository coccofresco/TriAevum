import unittest
from tools.triaevum_release.probe_display import sequence, verify


class DisplayProbeTests(unittest.TestCase):
    def evidence(self, case='modes'):
        steps = sequence(case)
        log = '\n'.join(f"DISPLAY diagnostic frame={s['frame']} step={{}}" for s in steps)
        runtime = dict(run_frames=1500, compiled_functions=dict(whole_aot_memory_faults=0))
        frames = [dict(presentation_window_mode=i % 3) for i in range(1500)]
        frames[0].update(presentation_apply_count=8, presentation_rollback_count=1)
        return log, runtime, dict(frames=frames)

    def test_complete_modes(self):
        self.assertEqual(verify('modes', *self.evidence())['modes'], [0, 1, 2])

    def test_missing_hook_is_not_a_pass(self):
        _, runtime, gpu = self.evidence()
        with self.assertRaisesRegex(ValueError, 'sequence'):
            verify('modes', '', runtime, gpu)

    def test_truncated_gpu_history_is_not_coverage(self):
        log, runtime, gpu = self.evidence()
        gpu['frames'] = gpu['frames'][-240:]
        with self.assertRaisesRegex(ValueError, 'Incomplete'):
            verify('modes', log, runtime, gpu)

    def test_no_rollback_and_validation_errors_fail(self):
        log, runtime, gpu = self.evidence()
        gpu['frames'][0]['presentation_rollback_count'] = 0
        with self.assertRaisesRegex(ValueError, 'roll back'):
            verify('modes', log, runtime, gpu)
        gpu['frames'][0]['vulkan_validation_error_count'] = 1
        with self.assertRaisesRegex(ValueError, 'validation'):
            verify('modes', log, runtime, gpu)

    def test_recovery_requires_observed_failure(self):
        log, runtime, gpu = self.evidence('recovery')
        for frame in gpu['frames']:
            frame['presentation_window_mode'] = 0
        gpu['frames'][0]['presentation_apply_failure_count'] = 8
        self.assertEqual(verify('recovery', log, runtime, gpu)['modes'], [0])


if __name__ == '__main__':
    unittest.main()
