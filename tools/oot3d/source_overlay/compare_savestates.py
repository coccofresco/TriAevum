#!/usr/bin/env python3
"""Compare source-overlay and whole-AOT native savestates."""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

import msgpack


MAGIC = b"OOT3DSV\0"
HEADER_SIZE = 40


def fnv1a64(data: bytes) -> int:
    result = 14695981039346656037
    for value in data:
        result ^= value
        result = (result * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return result


def load(path: Path, verify_checksum: bool) -> dict:
    encoded = path.read_bytes()
    if len(encoded) < HEADER_SIZE or encoded[:8] != MAGIC:
        raise ValueError(f"{path} is not an OOT3D native savestate")
    version, header_size, payload_size, payload_hash, reserved = (
        struct.unpack_from("<IIQQQ", encoded, 8)
    )
    payload = encoded[HEADER_SIZE:]
    if (
        version != 1
        or header_size != HEADER_SIZE
        or reserved != 0
        or len(payload) != payload_size
    ):
        raise ValueError(f"{path} has an invalid container header")
    if verify_checksum and fnv1a64(payload) != payload_hash:
        raise ValueError(f"{path} has an invalid payload checksum")
    return msgpack.unpackb(payload, raw=False, strict_map_key=False)


def memory_differences(baseline: dict, candidate: dict) -> tuple[int, list]:
    baseline_regions = {
        region["name"]: region
        for region in baseline["process"]["memory"]["regions"]
    }
    candidate_regions = {
        region["name"]: region
        for region in candidate["process"]["memory"]["regions"]
    }
    total = 0
    summaries = []
    for name in sorted(baseline_regions.keys() | candidate_regions.keys()):
        left = baseline_regions.get(name)
        right = candidate_regions.get(name)
        if left is None or right is None:
            summaries.append({"region": name, "kind": "region_set"})
            total += 1
            continue
        left_bytes = left["bytes"]
        right_bytes = right["bytes"]
        if left_bytes == right_bytes:
            continue
        offsets = [
            index
            for index, (left_byte, right_byte) in enumerate(
                zip(left_bytes, right_bytes)
            )
            if left_byte != right_byte
        ]
        count = len(offsets) + abs(len(left_bytes) - len(right_bytes))
        if count == 0:
            continue
        total += count
        base = int(left["base_address"])
        summaries.append(
            {
                "region": name,
                "kind": "content",
                "differing_bytes": count,
                "first_addresses": [
                    f"0x{base + offset:08X}" for offset in offsets[:16]
                ],
            }
        )
    return total, summaries


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument(
        "--verify-container-checksum",
        action="store_true",
        help="also recalculate the expensive FNV-1a payload checksums",
    )
    arguments = parser.parse_args()

    baseline = load(arguments.baseline, arguments.verify_container_checksum)
    candidate = load(arguments.candidate, arguments.verify_container_checksum)
    baseline_generation = baseline["process"]["memory"].get(
        "write_generation"
    )
    candidate_generation = candidate["process"]["memory"].get(
        "write_generation"
    )
    baseline["process"]["memory"]["write_generation"] = 0
    candidate["process"]["memory"]["write_generation"] = 0
    semantically_equal = baseline == candidate
    if semantically_equal:
        differing_bytes, regions = 0, []
    else:
        differing_bytes, regions = memory_differences(baseline, candidate)
    print(
        json.dumps(
            {
                "format": "oot3d_source_overlay_savestate_comparison_v1",
                "semantically_equal": semantically_equal,
                "differing_guest_bytes": differing_bytes,
                "differing_regions": regions,
                "baseline_write_generation": baseline_generation,
                "candidate_write_generation": candidate_generation,
                "write_generation_ignored": True,
                "container_checksum_verified": (
                    arguments.verify_container_checksum
                ),
            },
            indent=2,
        )
    )
    return 0 if semantically_equal else 1


if __name__ == "__main__":
    raise SystemExit(main())
