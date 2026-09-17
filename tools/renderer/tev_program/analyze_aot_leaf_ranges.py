"""Classify observed instruction pointers in hash-bound disassembly ranges.

Counts are intrusive observations, not timings or hardware stall attribution.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
from sample_aot_windows import sha256


def summarize(data, module_path, ranges):
    normalized = str(module_path.resolve()).lower()
    modules = [m for m in data['modules']
               if str(Path(m['path']).resolve()).lower() == normalized]
    if len(modules) != 1:
        raise ValueError('Expected exactly one captured target module')
    ordered = sorted(ranges, key=lambda r: r['begin_rva'])
    end = 0
    names = set()
    for r in ordered:
        if r['begin_rva'] < end or r['end_rva'] <= r['begin_rva']:
            raise ValueError('Invalid or overlapping instruction ranges')
        if r['name'] in names:
            raise ValueError('Duplicate range name')
        names.add(r['name'])
        end = r['end_rva']
    module = modules[0]
    counts = Counter({r['name']: 0 for r in ranges})
    total = 0
    for sample in data['stacks']:
        if not sample['pcs']:
            continue
        count = sample['count']
        if count < 0:
            raise ValueError('Negative observation count')
        rva = sample['pcs'][0] - module['base']
        if not 0 <= rva < module['size']:
            continue
        total += count
        for r in ordered:
            if r['begin_rva'] <= rva < r['end_rva']:
                counts[r['name']] += count
                break
    return {'aot_observations': total, 'ranges': dict(counts),
            'outside_selected_ranges': total - sum(counts.values())}


def main():
    p = argparse.ArgumentParser(__doc__)
    for name in ('module', 'ranges', 'output'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--capture', type=Path, action='append', required=True)
    args = p.parse_args()
    spec = json.loads(args.ranges.read_text())
    if sha256(args.module) != spec['module_sha256']:
        raise ValueError('Disassembly ranges belong to a different binary')
    reports = [summarize(json.loads(c.read_text()), args.module, spec['ranges'])
               for c in args.capture]
    counts = Counter()
    for r in reports:
        counts.update(r['ranges'])
    report = {'method': 'exclusive leaf IP observations, not CPU time or stall attribution',
        'limitations': ['sampling bias and instruction-pointer skid apply',
                       'capture provenance must establish the loaded binary identity',
                       'zero observations do not prove a path never executes'],
        'module_sha256': spec['module_sha256'],
        'input_sha256': {str(c): sha256(c) for c in [args.ranges, *args.capture]},
        'aot_observations': sum(r['aot_observations'] for r in reports),
        'ranges': dict(counts), 'runs': reports}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
