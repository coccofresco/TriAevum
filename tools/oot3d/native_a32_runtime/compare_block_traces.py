"""Report the first semantic divergence between two A32 block traces."""

from __future__ import annotations

import argparse
import json
from itertools import zip_longest
from pathlib import Path
from typing import TextIO


def _read_header(stream: TextIO, path: Path) -> dict:
    line = stream.readline()
    if not line:
        raise ValueError(f"empty trace: {path}")
    header = json.loads(line)
    if header.get("format") not in {
        "oot3d_a32_block_trace_v1",
        "oot3d_a32_block_trace_v2",
    }:
        raise ValueError(f"unsupported trace format: {path}")
    if header.get("truncated", False):
        raise ValueError(f"cannot compare truncated trace: {path}")
    return header


def _differences(expected: dict, actual: dict) -> list[str]:
    result: list[str] = []
    for field in (
        "host_frame",
        "pc",
        "memory_write_generation",
        "memory_write_fingerprint",
        "fast_read_mismatch_count",
        "last_fast_read_mismatch_address",
        "last_fast_read_mismatch_value",
        "last_checked_read_mismatch_value",
        "cpsr",
        "fpscr",
        "thread_pointer",
        "exclusive_address",
        "exclusive_token",
        "exclusive_size",
        "exclusive_valid",
    ):
        if expected[field] != actual[field]:
            result.append(f"{field}: {expected[field]} != {actual[field]}")
    # Whole-AOT keeps the current PC in generated control flow and only
    # materializes r15 at execution boundaries. The explicit `pc` field is the
    # canonical block-entry PC, so comparing the transient r15 value here would
    # report a false divergence on every inlined call.
    register_fields = (
        ("r", expected["r"][:15], actual["r"][:15]),
        ("vfp", expected["vfp"], actual["vfp"]),
    )
    for field, expected_values, actual_values in register_fields:
        for index, (left, right) in enumerate(
            zip(expected_values, actual_values)
        ):
            if left != right:
                result.append(f"{field}[{index}]: 0x{left:08X} != 0x{right:08X}")
    return result


def compare(expected_path: Path, actual_path: Path) -> int:
    with expected_path.open(encoding="utf-8") as expected_stream, actual_path.open(
        encoding="utf-8"
    ) as actual_stream:
        expected_header = _read_header(expected_stream, expected_path)
        actual_header = _read_header(actual_stream, actual_path)
        for index, pair in enumerate(
            zip_longest(expected_stream, actual_stream), start=0
        ):
            expected_line, actual_line = pair
            if expected_line is None or actual_line is None:
                print(
                    f"length divergence at record {index}: "
                    f"expected={expected_header['records']} actual={actual_header['records']}"
                )
                return 1
            expected = json.loads(expected_line)
            actual = json.loads(actual_line)
            differences = _differences(expected, actual)
            if differences:
                print(f"first divergence at record {index}")
                for difference in differences:
                    print(difference)
                return 1
    print(f"equivalent traces: {expected_header['records']} records")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("expected", type=Path)
    parser.add_argument("actual", type=Path)
    args = parser.parse_args()
    return compare(args.expected, args.actual)


if __name__ == "__main__":
    raise SystemExit(main())
