"""Summarize sampled runtime coverage against the closed whole-AOT owner map."""

from __future__ import annotations

import argparse
import bisect
import hashlib
import json
from collections import Counter
from pathlib import Path
from typing import Any


OUTPUT_FORMAT = "oot3d_whole_aot_runtime_coverage_v1"
GUEST_MAP_FORMAT = "oot3d_whole_aot_guest_map_v1"


def _load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"JSON root is not an object: {path}")
    return value


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def summarize_coverage(
    guest_map: dict[str, Any],
    runtimes: list[tuple[str, dict[str, Any], str]],
) -> dict[str, Any]:
    if guest_map.get("format") != GUEST_MAP_FORMAT:
        raise ValueError("unsupported whole-AOT guest map")
    functions = guest_map.get("functions")
    if not isinstance(functions, list) or not functions:
        raise ValueError("whole-AOT guest map has no functions")

    owner_by_pc: dict[int, int] = {}
    range_owners: list[tuple[int, int, int]] = []
    function_by_entry: dict[int, dict[str, Any]] = {}
    for function in functions:
        entry = int(function["entry"])
        function_by_entry[entry] = function
        for raw_pc in function.get("dispatch_entries", []):
            pc = int(raw_pc)
            previous = owner_by_pc.setdefault(pc, entry)
            if previous != entry:
                raise ValueError(f"dispatch PC 0x{pc:08X} has multiple owners")
        for raw_start, raw_end in function.get("ranges", []):
            start = int(raw_start)
            end = int(raw_end)
            if end <= start:
                raise ValueError(f"owner 0x{entry:08X} has an invalid range")
            range_owners.append((start, end, entry))
    range_owners.sort()
    range_starts = [item[0] for item in range_owners]

    def owner_for_pc(pc: int) -> tuple[int | None, bool]:
        owner = owner_by_pc.get(pc)
        if owner is not None:
            return owner, False
        index = bisect.bisect_right(range_starts, pc) - 1
        while index >= 0 and range_owners[index][0] <= pc:
            start, end, candidate = range_owners[index]
            if pc < end:
                return candidate, True
            if index == 0 or range_owners[index - 1][1] <= pc:
                break
            index -= 1
        return None, False

    owner_samples: Counter[int] = Counter()
    pc_samples: Counter[int] = Counter()
    unmatched_samples: Counter[int] = Counter()
    range_only_samples: Counter[int] = Counter()
    total_block_entries = 0
    total_samples = 0
    reported_samples = 0
    run_records: list[dict[str, Any]] = []
    for source, runtime, runtime_sha256 in runtimes:
        profile = runtime.get("a32_block_profile")
        if not isinstance(profile, dict) or not profile.get("enabled"):
            raise ValueError(f"runtime profile is not enabled: {source}")
        blocks = profile.get("blocks")
        if not isinstance(blocks, list):
            raise ValueError(f"runtime profile has no block list: {source}")
        run_block_entries = int(profile.get("block_entries", 0))
        run_total_samples = int(profile.get("total_samples", 0))
        run_reported_samples = int(profile.get("reported_samples", 0))
        if run_block_entries <= 0 or run_total_samples <= 0:
            raise ValueError(f"runtime profile has no observations: {source}")
        total_block_entries += run_block_entries
        total_samples += run_total_samples
        reported_samples += run_reported_samples
        matched_run_samples = 0
        for block in blocks:
            pc = int(block["pc"])
            samples = int(block["samples"])
            if samples <= 0:
                raise ValueError(f"runtime profile has invalid samples: {source}")
            pc_samples[pc] += samples
            owner, range_only = owner_for_pc(pc)
            if owner is None:
                unmatched_samples[pc] += samples
                continue
            if range_only:
                range_only_samples[pc] += samples
            owner_samples[owner] += samples
            matched_run_samples += samples
        run_records.append(
            {
                "path": source,
                "sha256": runtime_sha256,
                "frames": int(runtime.get("run_frames", runtime.get("frames", 0))),
                "block_entries": run_block_entries,
                "total_samples": run_total_samples,
                "reported_samples": run_reported_samples,
                "matched_reported_samples": matched_run_samples,
            }
        )

    observed_functions = []
    for entry, samples in sorted(
        owner_samples.items(), key=lambda item: (-item[1], item[0])
    ):
        function = function_by_entry[entry]
        observed_functions.append(
            {
                "entry": entry,
                "name": str(function["name"]),
                "shard": int(function["shard"]),
                "samples": samples,
                "estimated_block_entries": samples * 64,
            }
        )

    function_count = int(guest_map["function_count"])
    dispatch_count = int(guest_map["dispatch_entry_count"])
    matched_samples = sum(owner_samples.values())
    unmatched_sample_count = sum(unmatched_samples.values())
    return {
        "format": OUTPUT_FORMAT,
        "guest_map": {
            "function_count": function_count,
            "dispatch_entry_count": dispatch_count,
            "program_sha256": str(guest_map.get("program_sha256", "")),
            "code_sha256": str(guest_map.get("code_sha256", "")),
        },
        "summary": {
            "runtime_count": len(runtimes),
            "block_entries": total_block_entries,
            "total_samples": total_samples,
            "reported_samples": reported_samples,
            "reported_sample_percent": (
                100.0 * reported_samples / total_samples if total_samples else 0.0
            ),
            "matched_reported_samples": matched_samples,
            "unmatched_reported_samples": unmatched_sample_count,
            "range_only_reported_samples": sum(range_only_samples.values()),
            "observed_dispatch_pc_count": (
                len(pc_samples) - len(unmatched_samples) - len(range_only_samples)
            ),
            "observed_dispatch_pc_percent": (
                100.0
                * (len(pc_samples) - len(unmatched_samples) - len(range_only_samples))
                / dispatch_count
                if dispatch_count
                else 0.0
            ),
            "observed_guest_pc_count": len(pc_samples) - len(unmatched_samples),
            "observed_function_count": len(owner_samples),
            "observed_function_percent": (
                100.0 * len(owner_samples) / function_count if function_count else 0.0
            ),
        },
        "runtimes": run_records,
        "observed_functions": observed_functions,
        "unmatched_blocks": [
            {"pc": pc, "samples": samples}
            for pc, samples in sorted(
                unmatched_samples.items(), key=lambda item: (-item[1], item[0])
            )
        ],
        "range_only_blocks": [
            {"pc": pc, "samples": samples}
            for pc, samples in sorted(
                range_only_samples.items(), key=lambda item: (-item[1], item[0])
            )
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--guest-map", required=True, type=Path)
    parser.add_argument("--runtime", required=True, action="append", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    guest_map = _load_json(args.guest_map)
    runtimes = [
        (str(path.resolve()), _load_json(path), _sha256(path))
        for path in args.runtime
    ]
    result = summarize_coverage(guest_map, runtimes)
    result["guest_map"]["path"] = str(args.guest_map.resolve())
    result["guest_map"]["sha256"] = _sha256(args.guest_map)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_name(args.output.name + ".tmp")
    temporary.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    temporary.replace(args.output)
    print(json.dumps(result["summary"], sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
