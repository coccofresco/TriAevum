#!/usr/bin/env python3
"""Offline native PCM/state acceptance. Does not launch a game or use a device.

A matching silent tail is NOT restore evidence. PCM is the native DSP output,
not a recording of the host policy, WASAPI queue or physical speakers.
"""
import argparse
import array
import gzip
import hashlib
import io
import json
from pathlib import Path
import sys
import wave


def fnv1a64(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return value


def read_pcm(path):
    data = Path(path).read_bytes()
    if str(path).endswith('.gz'):
        data = gzip.decompress(data)
    with wave.open(io.BytesIO(data), 'rb') as wav:
        if wav.getnchannels() != 2 or wav.getsampwidth() != 2 or wav.getcomptype() != 'NONE':
            raise ValueError('expected uncompressed stereo PCM16')
        rate = wav.getframerate()
        count = wav.getnframes()
        pcm = wav.readframes(count)
        if len(pcm) != count * 4:
            raise ValueError('truncated PCM payload')
    return rate, pcm


def qualify(runtime, rate, pcm, minimum_seconds=5.0):
    if not minimum_seconds > 0 or not minimum_seconds < float('inf'):
        raise ValueError('minimum replay duration must be finite and positive')
    if rate <= 0 or len(pcm) % 4:
        raise ValueError('invalid stereo frame extent or sample rate')
    audio = runtime['dsp_audio']
    frames = len(pcm) // 4
    samples = array.array('h', pcm)
    if sys.byteorder != 'little':
        samples.byteswap()
    nonzero = sum(sample != 0 for sample in samples)
    digest = fnv1a64(pcm)
    checks = {
        'capture_matches_runtime': (
            rate == audio['output_sample_rate'] and frames == audio['stereo_samples_mixed']
            and digest == audio['pcm_fnv1a64'] and nonzero == audio['nonzero_channel_samples']),
        'current_real_output': (audio['host_output_enabled'] and audio['output_initialized']
                                and audio['output_player_current'] and audio['output_backend'] != 'null'),
        'realtime_pacing': runtime['realtime_pacing']['enabled'],
    }
    checkpoints = audio['output_queue']['checkpoints']
    # One save and one subsequent live load; initial loads are allowed. Never
    # silently select the last pair from a multi-attempt or malformed receipt.
    saves = [i for i, item in enumerate(checkpoints) if item['reason'] == 'state-saved']
    pair = None
    if len(saves) == 1:
        index = saves[0]
        later = [item for item in checkpoints[index + 1:] if item['reason'] == 'state-loaded']
        if len(later) == 1:
            pair = checkpoints[index], later[0]
    checks['single_live_restore_pair'] = pair is not None
    offsets = [item['pcm_stereo_frames'] for item in checkpoints]
    checks['valid_checkpoint_offsets'] = all(type(x) is int and 0 <= x <= frames for x in offsets) and offsets == sorted(offsets)
    replay = None
    if pair and checks['valid_checkpoint_offsets']:
        saved, loaded = pair
        first, second = saved['pcm_stereo_frames'], loaded['pcm_stereo_frames']
        count = min(second - first, frames - second)
        before = pcm[first * 4:(first + count) * 4]
        after = pcm[second * 4:(second + count) * 4]
        active_blocks = sum(any(before[i:i + rate * 4]) for i in range(0, len(before), rate * 4))
        replay = {
            'frames': count, 'seconds': count / rate,
            'identical': before == after,
            'nonzero_one_second_blocks': active_blocks,
            'before_sha256': hashlib.sha256(before).hexdigest(),
            'after_sha256': hashlib.sha256(after).hexdigest(),
            'differing_bytes': sum(a != b for a, b in zip(before, after)),
        }
        checks.update({
            'restored_host_frame': saved['host_frame'] == loaded['host_frame'],
            'sufficient_replay': count / rate >= minimum_seconds,
            'non_silent_replay': active_blocks >= int(minimum_seconds + 0.999999),
            'exact_replay': before == after,
        })
    replay_pass = all(checks.values()) and replay is not None
    return {
        'schema': 'triaevum.offline-audio-restore.v1',
        'native_replay_pass': replay_pass, 'checks': checks, 'replay': replay,
        'pcm': {'rate': rate, 'stereo_frames': frames, 'nonzero_channel_samples': nonzero,
                'peak': max(map(abs, samples), default=0), 'fnv1a64': digest,
                'sha256': hashlib.sha256(pcm).hexdigest()},
        'device_queue': {'output_underruns': audio['output_underruns'],
                         'zero_underruns': audio['output_underruns'] == 0,
                         'maximum_submit_gap_seconds': audio['output_queue']['maximum_submit_gap_seconds']},
        'checkpoints': checkpoints,
        'guest_refreshes_dropped': runtime['frame_rate']['guest_refreshes_dropped'],
        'physical_device_or_listening_qualified': False,
        'scope': 'Native DSP replay only; queue continuity is a separate result. No physical-device, reverb-tail or all-platform inference.',
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--pcm', type=Path, required=True, help='native PCM WAV or WAV.gz')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--minimum-seconds', type=float, default=5.0)
    args = parser.parse_args()
    try:
        rate, pcm = read_pcm(args.pcm)
        result = qualify(json.loads(args.runtime.read_text(encoding='utf-8-sig')), rate, pcm, args.minimum_seconds)
        result['inputs'] = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in (args.runtime, args.pcm)}
    except (OSError, ValueError, KeyError, TypeError, wave.Error, EOFError) as exc:
        result = {'native_replay_pass': False, 'error': str(exc)}
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result, indent=2))
    return 0 if result['native_replay_pass'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
