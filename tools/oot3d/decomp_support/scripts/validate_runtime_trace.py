#!/usr/bin/env python3
"""Validate OOT3D emulator trace JSONL files without third-party dependencies."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
SCHEMA = ROOT / "schemas" / "runtime_trace_event.schema.json"
HEX32 = re.compile(r"^0x[0-9a-fA-F]{8}$")


HEX_FIELDS = {
    "pc",
    "function_entry",
    "caller_pc",
    "sp",
    "lr",
    "address",
    "object_ptr",
}
INTEGER_FIELDS = {
    "tick",
    "frame",
    "thread_id",
    "size",
    "scene_id",
    "actor_id",
}


def load_allowed_events() -> set[str]:
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    return set(schema["properties"]["event"]["enum"])


def validate_event(event: dict[str, Any], allowed_events: set[str]) -> list[str]:
    errors: list[str] = []
    extra_fields = set(event) - {
        "event",
        "pc",
        "tick",
        "frame",
        "thread_id",
        "function_entry",
        "function_name",
        "caller_pc",
        "sp",
        "lr",
        "regs",
        "args",
        "address",
        "size",
        "value",
        "romfs_path",
        "scene_id",
        "actor_id",
        "object_ptr",
        "result",
        "notes",
    }
    if extra_fields:
        errors.append(f"unexpected fields: {', '.join(sorted(extra_fields))}")

    name = event.get("event")
    if name not in allowed_events:
        errors.append(f"invalid event: {name!r}")

    if "pc" not in event:
        errors.append("missing pc")

    for field in HEX_FIELDS:
        if field in event and not isinstance(event[field], str):
            errors.append(f"{field} must be a string")
        elif field in event and not HEX32.match(event[field]):
            errors.append(f"{field} must be 0x00000000 format")

    for field in INTEGER_FIELDS:
        if field in event and (not isinstance(event[field], int) or event[field] < 0):
            errors.append(f"{field} must be a non-negative integer")

    if "function_name" in event and not isinstance(event["function_name"], str):
        errors.append("function_name must be a string")
    if "romfs_path" in event and not isinstance(event["romfs_path"], str):
        errors.append("romfs_path must be a string")
    if "notes" in event and not isinstance(event["notes"], str):
        errors.append("notes must be a string")

    regs = event.get("regs")
    if regs is not None:
        if not isinstance(regs, dict):
            errors.append("regs must be an object")
        else:
            for reg, value in regs.items():
                if not isinstance(reg, str):
                    errors.append("register names must be strings")
                if not isinstance(value, str) or not HEX32.match(value):
                    errors.append(f"regs.{reg} must be 0x00000000 format")

    args = event.get("args")
    if args is not None:
        if not isinstance(args, list):
            errors.append("args must be an array")
        else:
            for index, value in enumerate(args):
                if not isinstance(value, str) or not HEX32.match(value):
                    errors.append(f"args[{index}] must be 0x00000000 format")

    value = event.get("value")
    if value is not None and not isinstance(value, int) and (not isinstance(value, str) or not HEX32.match(value)):
        errors.append("value must be an integer or 0x00000000 string")

    result = event.get("result")
    if result is not None and not isinstance(result, (str, int, bool)):
        errors.append("result must be a string, integer, boolean, or null")

    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path, help="JSONL trace file")
    args = parser.parse_args()

    allowed_events = load_allowed_events()
    total = 0
    failed = 0
    event_counts: dict[str, int] = {}

    with args.trace.open("r", encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, 1):
            stripped = line.strip()
            if not stripped:
                continue
            total += 1
            try:
                event = json.loads(stripped)
            except json.JSONDecodeError as exc:
                failed += 1
                print(f"{args.trace}:{line_number}: invalid JSON: {exc}", file=sys.stderr)
                continue
            if not isinstance(event, dict):
                failed += 1
                print(f"{args.trace}:{line_number}: event must be an object", file=sys.stderr)
                continue
            event_counts[str(event.get("event"))] = event_counts.get(str(event.get("event")), 0) + 1
            errors = validate_event(event, allowed_events)
            if errors:
                failed += 1
                print(f"{args.trace}:{line_number}: {'; '.join(errors)}", file=sys.stderr)

    summary = {
        "events": total,
        "failed": failed,
        "event_counts": dict(sorted(event_counts.items())),
    }
    print(json.dumps(summary, indent=2))
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
