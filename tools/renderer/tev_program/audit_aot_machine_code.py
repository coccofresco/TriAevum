"""Bounded existing-binary inspection; static counts are not CPU costs."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import subprocess
from sample_aot_windows import read_map, classify_symbol, sha256


def count_instructions(text, symbols, base):
    counts, calls = Counter(), Counter()
    for line in text.splitlines():
        m = re.match(r'^\s*([0-9a-f]+):\s+([a-z][a-z0-9]*)\s*(.*)$', line)
        if not m:
            continue
        op, operands = m[2], m[3]
        counts['instructions'] += 1
        if '[' in operands:
            counts['memory_operand_instructions_including_lea'] += 1
        if op == 'call':
            counts['call_sites'] += 1
            target = re.match(r'0x([0-9a-f]+)(?:\s|$)', operands)
            symbol = symbols.get(int(target[1], 16) - base) if target else None
            calls[classify_symbol(symbol)] += 1
        if op.startswith('j'):
            counts['branch_sites'] += 1
    return {'static_counts': dict(counts), 'call_site_categories': dict(calls)}


def main():
    p = argparse.ArgumentParser(__doc__)
    for name in ('module', 'map', 'objdump', 'output'):
        p.add_argument('--' + name, required=True, type=Path)
    p.add_argument('--entry', action='append', required=True)
    args = p.parse_args()
    entries = read_map(args.map)
    symbols = dict(entries)
    base = int(re.search(r'Preferred load address is ([0-9a-fA-F]+)',
                         args.map.read_text())[1], 16)
    rows = []
    for entry in args.entry:
        entry = f'{int(entry, 16):08X}'
        matches = [(i, pc, symbol) for i, (pc, symbol) in enumerate(entries)
                   if symbol.startswith('?Execute_') and f'_{entry}@' in symbol]
        if len(matches) != 1:
            raise ValueError(f'Expected one unmixed primary symbol for {entry}')
        index, start, symbol = matches[0]
        end = entries[index + 1][0]
        if not 0 < end - start <= 1024 * 1024:
            raise ValueError('Unbounded or invalid symbol extent')
        output = subprocess.run([str(args.objdump), '-d', '--no-show-raw-insn',
            '--x86-asm-syntax=intel', f'--start-address={base + start}',
            f'--stop-address={base + end}', str(args.module)],
            check=True, capture_output=True, text=True, timeout=30).stdout
        rows.append({'entry': entry, 'symbol': symbol, 'start_rva': start,
                     'next_symbol_rva': end, 'span_bytes': end - start,
                     **count_instructions(output, symbols, base)})
    report = {'limitations': ['static sites, not dynamic counts or timings',
        'map/module correspondence must be independently established',
        'next-symbol bounds may exclude outlined parts; no stack spill inference',
        'memory-operand count includes LEA and is not a count of actual loads'],
        'module_sha256': sha256(args.module), 'map_sha256': sha256(args.map),
        'functions': rows}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2))
    print(json.dumps(rows, indent=2))


if __name__ == '__main__':
    main()
