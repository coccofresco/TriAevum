import copy
import io
from pathlib import Path
import struct
import tempfile
import unittest
import wave

from qualify_audio_restore import fnv1a64, qualify, read_pcm


class AudioRestoreAcceptanceTests(unittest.TestCase):
    def setUp(self):
        self.rate = 16
        tail = b''.join(struct.pack('<hh', i + 1, -i - 1) for i in range(96))
        self.pcm = bytes(64) + tail + tail
        self.runtime = {
            'dsp_audio': {
                'output_sample_rate': self.rate, 'stereo_samples_mixed': 208,
                'pcm_fnv1a64': fnv1a64(self.pcm), 'nonzero_channel_samples': 384,
                'host_output_enabled': True, 'output_initialized': True,
                'output_player_current': True, 'output_backend': 'wasapi',
                'output_underruns': 0,
                'output_queue': {'maximum_submit_gap_seconds': 0.02, 'checkpoints': [
                    {'reason': 'state-saved', 'host_frame': 300, 'pcm_stereo_frames': 16},
                    {'reason': 'state-loaded', 'host_frame': 300, 'pcm_stereo_frames': 112},
                ]},
            },
            'realtime_pacing': {'enabled': True},
            'frame_rate': {'guest_refreshes_dropped': 0},
        }

    def test_nonzero_replay(self):
        result = qualify(self.runtime, self.rate, self.pcm)
        self.assertTrue(result['native_replay_pass'])
        self.assertEqual(result['replay']['seconds'], 6)
        self.assertFalse(result['physical_device_or_listening_qualified'])

    def test_silence_never_passes(self):
        pcm = bytes(len(self.pcm))
        self.runtime['dsp_audio'].update(pcm_fnv1a64=fnv1a64(pcm), nonzero_channel_samples=0)
        result = qualify(self.runtime, self.rate, pcm)
        self.assertTrue(result['replay']['identical'])
        self.assertFalse(result['native_replay_pass'])
        self.assertFalse(result['checks']['non_silent_replay'])

    def test_capture_identity(self):
        for key in ('output_sample_rate', 'stereo_samples_mixed', 'pcm_fnv1a64', 'nonzero_channel_samples'):
            runtime = copy.deepcopy(self.runtime)
            runtime['dsp_audio'][key] += 1
            self.assertFalse(qualify(runtime, self.rate, self.pcm)['native_replay_pass'], key)

    def test_native_replay_does_not_claim_realtime_or_device_output(self):
        for key, value in [('host_output_enabled', False), ('output_initialized', False),
                           ('output_player_current', False), ('output_backend', 'null')]:
            runtime = copy.deepcopy(self.runtime)
            runtime['dsp_audio'][key] = value
            result = qualify(runtime, self.rate, self.pcm)
            self.assertTrue(result['native_replay_pass'])
            self.assertFalse(result['device_queue']['current_real_output'])
        self.runtime['realtime_pacing']['enabled'] = False
        result = qualify(self.runtime, self.rate, self.pcm)
        self.assertTrue(result['native_replay_pass'])
        self.assertFalse(result['device_queue']['realtime_pacing'])

    def test_underruns_do_not_disappear_in_replay_pass(self):
        self.runtime['dsp_audio']['output_underruns'] = 85
        result = qualify(self.runtime, self.rate, self.pcm)
        self.assertTrue(result['native_replay_pass'])
        self.assertFalse(result['device_queue']['zero_underruns'])
        self.assertEqual(result['device_queue']['output_underruns'], 85)

    def test_missing_duplicate_or_reversed_actions(self):
        cp = self.runtime['dsp_audio']['output_queue']['checkpoints']
        for changed in (cp[:1], cp[1:], cp[::-1], cp + cp, cp + cp[1:]):
            runtime = copy.deepcopy(self.runtime)
            runtime['dsp_audio']['output_queue']['checkpoints'] = changed
            self.assertFalse(qualify(runtime, self.rate, self.pcm)['native_replay_pass'])

    def test_invalid_offsets_and_wrong_restored_frame(self):
        for offset in (-1, 209, 1.5, True):
            runtime = copy.deepcopy(self.runtime)
            runtime['dsp_audio']['output_queue']['checkpoints'][1]['pcm_stereo_frames'] = offset
            self.assertFalse(qualify(runtime, self.rate, self.pcm)['native_replay_pass'])
        self.runtime['dsp_audio']['output_queue']['checkpoints'][1]['host_frame'] += 1
        self.assertFalse(qualify(self.runtime, self.rate, self.pcm)['native_replay_pass'])

    def test_short_and_different_tail(self):
        self.assertFalse(qualify(self.runtime, self.rate, self.pcm, 7)['native_replay_pass'])
        changed = self.pcm[:-2] + b'\x01\x01'
        self.runtime['dsp_audio']['pcm_fnv1a64'] = fnv1a64(changed)
        self.assertFalse(qualify(self.runtime, self.rate, changed)['checks']['exact_replay'])

    def test_invalid_duration(self):
        for duration in (0, -1, float('nan'), float('inf')):
            with self.assertRaises(ValueError):
                qualify(self.runtime, self.rate, self.pcm, duration)

    def test_wav_extent_and_format(self):
        stream = io.BytesIO()
        with wave.open(stream, 'wb') as wav:
            wav.setnchannels(2)
            wav.setsampwidth(2)
            wav.setframerate(self.rate)
            wav.writeframes(self.pcm)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'test.wav'
            path.write_bytes(stream.getvalue())
            self.assertEqual(read_pcm(path), (self.rate, self.pcm))
            path.write_bytes(stream.getvalue()[:-4])
            with self.assertRaisesRegex(ValueError, 'truncated'):
                read_pcm(path)


if __name__ == '__main__':
    unittest.main()
