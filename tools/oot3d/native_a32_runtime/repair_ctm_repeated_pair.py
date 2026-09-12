"""Repair only byte-rotated CTM pad/touch pairs inside identical repeated input.

Offline recording repair, never an emulator input fallback. Originals are not
overwritten. A repaired movie still requires a full synchronization check.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def repair(data):
    if len(data) < 256 or data[:4] != b'CTM\x1b' or (len(data)-256) % 7:
        raise ValueError('Invalid CTM structure')
    records = [data[i:i+7] for i in range(256, len(data), 7)]
    if any(record[0] > 5 for record in records):
        raise ValueError('Unknown device event')
    fixed = bytearray(data)
    changes = []
    i = 0
    while i < len(records):
        if records[i][0] != 0:
            i += 1
            continue
        if i+1 < len(records) and records[i+1][0] == 1:
            i += 2
            continue
        previous = next((j for j in range(i-2, max(-1, i-9), -1)
                         if records[j][0] == 0 and records[j+1][0] == 1), None)
        following = next((j for j in range(i+2, min(len(records)-1, i+9))
                          if records[j][0] == 0 and records[j+1][0] == 1), None)
        if previous is None or following is None or i+1 >= len(records):
            raise ValueError(f'No unambiguous repeated pair at event {i}')
        expected = records[previous] + records[previous+1]
        after = records[following] + records[following+1]
        actual = records[i] + records[i+1]
        buttons, x, y = struct.unpack_from('<Hhh', expected, 1)
        if expected != after or actual != expected[1:] + expected[:1]:
            raise ValueError(f'Not a single-byte rotation of repeated input at event {i}')
        if abs(x) > 156 or abs(y) > 156 or buttons & 0xc000:
            raise ValueError('Repeated reference input is not a valid native circle-pad sample')
        offset = 256 + i*7
        fixed[offset:offset+14] = expected
        changes.append({'offset': offset, 'before_hex': actual.hex(),
                        'after_hex': expected.hex(),
                        'previous_reference_offset': 256+previous*7,
                        'next_reference_offset': 256+following*7})
        records[i:i+2] = [expected[:7], expected[7:]]
        i += 2
    declared = struct.unpack_from('<Q', data, 0x54)[0]
    pad_count = sum(record[0] == 0 for record in records)
    if changes and declared:
        struct.pack_into('<Q', fixed, 0x54, pad_count)
    report = {'source_sha256': hashlib.sha256(data).hexdigest(),
              'output_sha256': hashlib.sha256(fixed).hexdigest(),
              'repairs': changes, 'declared_pad_count_before': declared,
              'declared_pad_count_after': struct.unpack_from('<Q', fixed, 0x54)[0],
              'pad_input_count': pad_count, 'sync_verified': False}
    return bytes(fixed), report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    source = args.input.resolve(strict=True)
    output, report_path = args.output.resolve(), args.report.resolve()
    if len({source, output, report_path}) != 3 or output.exists() or report_path.exists():
        parser.error('Input, new output and new report must be distinct; no overwrites')
    result, report = repair(source.read_bytes())
    output.parent.mkdir(parents=True, exist_ok=True)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(result)
    report_path.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
