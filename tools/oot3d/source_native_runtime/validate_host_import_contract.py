#!/usr/bin/env python3
"""Reject drift in the finite source-native host import surface."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


HOST_CATEGORIES = {
    "compiler_runtime", "host_c_runtime", "ctr_platform", "host_platform_hook"
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--surface-contracts", required=True, type=Path)
    parser.add_argument("--host-contract", required=True, type=Path)
    args = parser.parse_args()

    surface = json.loads(args.surface_contracts.read_text(encoding="utf-8"))
    contract = json.loads(args.host_contract.read_text(encoding="utf-8"))
    if contract.get("schema_version") != 1:
        raise ValueError("unsupported host import contract schema")

    expected: dict[str, set[str]] = {}
    for category, record in contract.get("imports", {}).items():
        if category not in HOST_CATEGORIES or not record.get("provider"):
            raise ValueError(f"invalid host import category: {category}")
        symbols = record.get("symbols", [])
        if len(symbols) != len(set(symbols)):
            raise ValueError(f"duplicate host imports in {category}")
        expected[category] = set(symbols)

    actual = {category: set() for category in HOST_CATEGORIES}
    for record in surface["external_symbols"]:
        category = record["category"]
        if category in HOST_CATEGORIES:
            actual[category].add(record["name"])

    errors: list[str] = []
    for category in sorted(HOST_CATEGORIES):
        missing = expected.get(category, set()) - actual[category]
        unexpected = actual[category] - expected.get(category, set())
        if missing:
            errors.append(f"{category}: expected but absent: {', '.join(sorted(missing))}")
        if unexpected:
            errors.append(f"{category}: not allowlisted: {', '.join(sorted(unexpected))}")
    if errors:
        raise ValueError("host import contract drift\n" + "\n".join(errors))

    count = sum(len(symbols) for symbols in actual.values())
    print(f"host import contract verified: {count} imports")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
