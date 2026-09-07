#!/usr/bin/env python3
"""Validate function-level ownership of the TopScreen injected payload."""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import Counter
from pathlib import Path
from typing import Any


PAYLOAD_LOCAL_CALL = re.compile(r"(?:FUN_|func_0x)(005[c-d][0-9a-f]{4})\s*\(")


def load_json(path: Path) -> dict[str, Any]:
    with path.open(encoding="utf-8") as source:
        return json.load(source)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--functions", required=True, type=Path)
    parser.add_argument("--semantic-port", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--repo-root", type=Path, default=Path("."))
    parser.add_argument("--decompiled-dir", type=Path)
    arguments = parser.parse_args()

    with arguments.functions.open(newline="", encoding="utf-8") as source:
        selected = list(csv.DictReader(source))
    semantic = load_json(arguments.semantic_port)
    expected_count = int(semantic["source"]["expected_function_count"])
    if len(selected) != expected_count:
        raise ValueError(
            f"selected export has {len(selected)} functions, expected {expected_count}"
        )

    selected_by_entry = {row["entry"].lower(): row for row in selected}
    if len(selected_by_entry) != len(selected):
        raise ValueError("selected function export contains duplicate entries")

    decompiled_dir = (
        arguments.decompiled_dir
        if arguments.decompiled_dir is not None
        else arguments.functions.parent / "decompiled"
    )
    local_call_edges: set[tuple[str, str]] = set()
    missing_local_callees: set[tuple[str, str]] = set()
    for entry in selected_by_entry:
        decompiled = list(decompiled_dir.glob(f"*_{entry}_*.c"))
        if len(decompiled) != 1:
            raise ValueError(
                f"function {entry} has {len(decompiled)} decompiled files in "
                f"{decompiled_dir}, expected exactly one"
            )
        source_text = decompiled[0].read_text(
            encoding="utf-8", errors="replace"
        )
        for match in PAYLOAD_LOCAL_CALL.finditer(source_text):
            target = match.group(1).lower()
            local_call_edges.add((entry, target))
            if target not in selected_by_entry:
                missing_local_callees.add((entry, target))
    if missing_local_callees:
        formatted = [
            f"{source}->{target}"
            for source, target in sorted(missing_local_callees)
        ]
        raise ValueError(
            f"selected export is not payload-call-closed: {formatted}"
        )

    ownership: dict[str, dict[str, Any]] = {}
    for disposition in semantic.get("dispositions", []):
        semantic_roles = disposition.get("semantic_roles", [])
        if not semantic_roles or not all(
            isinstance(role, str) and role for role in semantic_roles
        ):
            raise ValueError(
                f"disposition {disposition['id']} has no semantic roles"
            )
        tests = disposition.get("tests", [])
        if not tests:
            raise ValueError(f"disposition {disposition['id']} has no tests")
        for test in tests:
            if not (arguments.repo_root / test).is_file():
                raise ValueError(
                    f"disposition {disposition['id']} references missing test {test}"
                )
        for owner in disposition.get("owners", []):
            if not (arguments.repo_root / owner).is_file():
                raise ValueError(
                    f"disposition {disposition['id']} references missing owner {owner}"
                )
        verified_symbols = 0
        for contract in disposition.get("owner_symbols", []):
            owner = contract.get("owner")
            if owner not in disposition.get("owners", []):
                raise ValueError(
                    f"disposition {disposition['id']} has a symbol contract for "
                    f"undeclared owner {owner}"
                )
            owner_text = (arguments.repo_root / owner).read_text(
                encoding="utf-8", errors="replace"
            )
            symbols = contract.get("symbols", [])
            if not symbols:
                raise ValueError(
                    f"disposition {disposition['id']} has an empty symbol contract "
                    f"for {owner}"
                )
            for symbol in symbols:
                if symbol not in owner_text:
                    raise ValueError(
                        f"disposition {disposition['id']} owner {owner} does not "
                        f"contain required symbol {symbol}"
                    )
                verified_symbols += 1
        if verified_symbols == 0:
            raise ValueError(
                f"disposition {disposition['id']} has no verified C++ symbols"
            )
        for raw_entry in disposition.get("functions", []):
            entry = str(raw_entry).lower()
            if entry in ownership:
                raise ValueError(f"function {entry} has multiple dispositions")
            ownership[entry] = disposition

    expected_entries = set(selected_by_entry)
    owned_entries = set(ownership)
    if owned_entries != expected_entries:
        missing = sorted(expected_entries - owned_entries)
        extra = sorted(owned_entries - expected_entries)
        raise ValueError(
            f"invalid function disposition coverage: missing={missing}, extra={extra}"
        )

    functions: list[dict[str, Any]] = []
    for entry in sorted(expected_entries):
        source = selected_by_entry[entry]
        disposition = ownership[entry]
        functions.append(
            {
                "entry": entry,
                "name": source["name"],
                "body_min": source["body_min"],
                "body_max": source["body_max"],
                "disposition": disposition["id"],
                "status": disposition["status"],
                "owners": disposition.get("owners", []),
                "semantic_roles": disposition["semantic_roles"],
                "owner_symbols": disposition["owner_symbols"],
                "tests": disposition["tests"],
            }
        )

    status_counts = Counter(function["status"] for function in functions)
    output = {
        "format": "oot3d_topscreen_payload_function_coverage_v2",
        "source": semantic["source"],
        "policy": semantic["policy"],
        "summary": {
            "function_count": len(functions),
            "disposition_count": len(semantic.get("dispositions", [])),
            "status_counts": dict(sorted(status_counts.items())),
            "unowned_functions": [],
            "payload_local_call_edges": len(local_call_edges),
            "unselected_payload_local_callees": [],
        },
        "functions": functions,
    }
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(
        json.dumps(output, indent=2) + "\n", encoding="utf-8"
    )
    print(
        f"wrote {len(functions)} functions in "
        f"{len(semantic.get('dispositions', []))} dispositions"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
