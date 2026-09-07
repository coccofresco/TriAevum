#!/usr/bin/env python3
"""Build and validate the record-level TopScreenMod source-port coverage."""

from __future__ import annotations

import argparse
import hashlib
import json
from collections import Counter
from pathlib import Path
from typing import Any


def load_json(path: Path) -> dict[str, Any]:
    with path.open(encoding="utf-8") as source:
        return json.load(source)


def record_ids(entry: dict[str, Any]) -> list[int]:
    if "record" in entry:
        return [int(entry["record"])]
    return [int(value) for value in entry.get("records", [])]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--raw-analysis", required=True, type=Path)
    parser.add_argument("--semantic-port", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    arguments = parser.parse_args()

    raw = load_json(arguments.raw_analysis)
    semantic = load_json(arguments.semantic_port)
    raw_records = raw.get("records", [])
    expected_count = int(semantic["source"]["record_count"])
    if len(raw_records) != expected_count:
        raise ValueError(
            f"raw analysis has {len(raw_records)} records, expected {expected_count}"
        )

    dispositions: dict[int, dict[str, Any]] = {}
    for disposition in semantic.get("dispositions", []):
        for record in record_ids(disposition):
            if record in dispositions:
                raise ValueError(f"record {record} has multiple dispositions")
            dispositions[record] = disposition

    expected_ids = set(range(expected_count))
    actual_ids = {int(record["index"]) for record in raw_records}
    if actual_ids != expected_ids:
        raise ValueError("raw record indices are not contiguous")
    if set(dispositions) != expected_ids:
        missing = sorted(expected_ids - set(dispositions))
        extra = sorted(set(dispositions) - expected_ids)
        raise ValueError(f"invalid disposition coverage: missing={missing}, extra={extra}")

    resolutions: dict[int, dict[str, Any]] = {}
    for resolution in semantic.get("record_resolutions", []):
        for record in record_ids(resolution):
            if record not in expected_ids:
                raise ValueError(f"resolution references unknown record {record}")
            if record in resolutions:
                raise ValueError(f"record {record} has multiple resolutions")
            resolutions[record] = resolution

    coverage_records: list[dict[str, Any]] = []
    for raw_record in raw_records:
        index = int(raw_record["index"])
        disposition = dispositions[index]
        resolution = resolutions.get(index)
        owner = raw_record.get("owner") or {}
        replacement = bytes.fromhex(raw_record.get("replacement_hex", ""))
        coverage_records.append(
            {
                "record": index,
                "address": raw_record["address"],
                "size": int(raw_record["size"]),
                "replacement_sha256": hashlib.sha256(replacement).hexdigest(),
                "classification": raw_record["classification"],
                "owner": owner.get("name"),
                "owner_file": owner.get("logical_file"),
                "disposition": disposition["id"],
                "required_action": disposition["action"],
                "status": (
                    resolution["status"] if resolution is not None
                    else disposition["status"]
                ),
                "resolution_action": (
                    resolution.get("action") if resolution is not None else None
                ),
                "evidence": (
                    resolution.get("evidence") if resolution is not None else None
                ),
            }
        )

    status_counts = Counter(record["status"] for record in coverage_records)
    unresolved = [
        record["record"]
        for record in coverage_records
        if record["status"] in {"partial", "unresolved", "pending"}
        or "pending" in record["status"]
    ]
    output = {
        "format": "oot3d_topscreen_mod_record_coverage_v1",
        "source": semantic["source"],
        "policy": semantic["policy"],
        "summary": {
            "record_count": len(coverage_records),
            "explicit_resolution_count": len(resolutions),
            "status_counts": dict(sorted(status_counts.items())),
            "records_requiring_resolution": unresolved,
        },
        "records": coverage_records,
    }
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(
        json.dumps(output, indent=2, sort_keys=False) + "\n", encoding="utf-8"
    )
    print(
        f"wrote {len(coverage_records)} records; "
        f"explicit={len(resolutions)} unresolved={len(unresolved)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
