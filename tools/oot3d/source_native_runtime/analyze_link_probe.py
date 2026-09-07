#!/usr/bin/env python3
import argparse
import json
import re
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--log", required=True, type=Path)
    parser.add_argument("--surface-contracts", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    text = args.log.read_text(encoding="utf-8", errors="replace")
    unresolved = sorted(set(re.findall(r"undefined reference to [`']([^`']+)", text)))
    duplicates = sorted(set(re.findall(r"multiple definition of [`']([^`']+)", text)))
    contracts = json.loads(args.surface_contracts.read_text(encoding="utf-8"))
    categories = {row["name"]: row["category"]
                  for row in contracts.get("external_symbols", [])}
    rows = [{"name": name, "category": categories.get(name, "unclassified")}
            for name in unresolved]
    counts: dict[str, int] = {}
    for row in rows:
        counts[row["category"]] = counts.get(row["category"], 0) + 1
    result = {
        "schema_version": 1,
        "reachable_unresolved_count": len(rows),
        "category_counts": dict(sorted(counts.items())),
        "multiple_definition_count": len(duplicates),
        "multiple_definitions": duplicates,
        "reachable_unresolved": rows,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "reachable_unresolved_count": len(rows),
        "category_counts": result["category_counts"],
        "multiple_definition_count": len(duplicates),
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
