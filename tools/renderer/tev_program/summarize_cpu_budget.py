"""Bound pre-NRI CPU/off-CPU without misusing quantized per-scope thread times."""
import argparse
import json
from pathlib import Path


def summarize(window):
    ledger = window['thread_cpu_ledger']
    phases = ledger['phases']
    frames = window['measured_frames']
    if not ledger['requested'] or ledger['read_failures'] or frames <= 0:
        raise ValueError('Missing or invalid CPU ledger')
    wall = sum(p['wall_seconds'] for p in phases.values())
    # GetThreadTimes charges can spill across adjacent short scopes. Only their
    # telescoping window total is meaningful, even across thousands of scopes.
    cpu = ledger.get('window_thread_cpu_seconds')
    if cpu is None:
        cpu = sum(p['user_seconds'] + p['kernel_seconds'] for p in phases.values())
    excluded = sum(p['wall_seconds'] for name, p in phases.items() if name.endswith('_excluded'))
    pre_wall = wall - excluded
    # Conservative 32 ms allowance for the two quantized window endpoints.
    allowance = 0.032
    if abs(wall - window['host_seconds']) > .01 or cpu > wall + allowance:
        raise ValueError('CPU/window closure outside counter resolution')
    off_cpu_upper = max(0, wall - cpu + allowance)
    scale = 1000 / frames
    return {
        'measured_updates': frames,
        'diagnostic_only_not_speed_comparison': True,
        'window_wall_ms': wall * scale,
        'window_thread_cpu_ms': cpu * scale,
        'pre_nri_wall_ms': pre_wall * scale,
        'pre_nri_cpu_lower_ms': max(0, pre_wall - off_cpu_upper) * scale,
        'pre_nri_cpu_upper_ms': min(pre_wall, cpu + allowance) * scale,
        'pre_nri_off_cpu_upper_ms': min(pre_wall, off_cpu_upper) * scale,
        'counter_endpoint_allowance_ms_per_update': allowance * scale,
        'phases': {name: {'wall_ms': p['wall_seconds'] * scale,
                          'thread_cycles_per_update': p['thread_cycles'] / frames}
                   for name, p in phases.items()},
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('measurements', type=Path)
    args = parser.parse_args()
    rows = json.loads(args.measurements.read_text())
    print(json.dumps([summarize(row['benchmark']) for row in rows], indent=2))
