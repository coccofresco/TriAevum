#!/usr/bin/env python3
"""Promote multiple reviewed manual symbols from a CSV decision file."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FUNCTIONS = ROOT / "ghidra_export" / "functions.csv"
SYMBOLS = ROOT / "symbols" / "manual_symbols.csv"
FIELDS = ["entry", "old_name", "new_name", "kind", "confidence", "source_file", "notes"]
REQUIRED_DECISION_FIELDS = ["entry", "new_name", "source_file", "confidence", "notes"]
TRUTHY = {"1", "true", "yes", "y", "approved", "promote"}


def normalize_entry(entry: str) -> str:
    value = entry.strip().lower().removeprefix("0x")
    if len(value) != 8:
        raise ValueError(f"entry must be 8 hex digits: {entry}")
    int(value, 16)
    return value


def load_functions() -> dict[str, str]:
    with FUNCTIONS.open(newline="", encoding="utf-8") as handle:
        return {row["entry"].lower(): row["name"] for row in csv.DictReader(handle)}


def load_symbols() -> list[dict[str, str]]:
    if not SYMBOLS.exists():
        return []
    with SYMBOLS.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def write_symbols(rows: list[dict[str, str]]) -> None:
    rows = sorted(rows, key=lambda row: int(row["entry"], 16))
    with SYMBOLS.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows)


def approved(row: dict[str, str]) -> bool:
    value = row.get("approved")
    if value is None or value.strip() == "":
        return True
    return value.strip().lower() in TRUTHY


def load_decisions(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8-sig") as handle:
        reader = csv.DictReader(handle)
        missing = [field for field in REQUIRED_DECISION_FIELDS if field not in (reader.fieldnames or [])]
        if missing:
            raise SystemExit(f"decision CSV is missing required columns: {', '.join(missing)}")
        rows = []
        for line_number, row in enumerate(reader, start=2):
            if not approved(row):
                continue
            decision = {key: (value or "").strip() for key, value in row.items()}
            decision["line_number"] = str(line_number)
            decision["entry"] = normalize_entry(decision["entry"])
            decision["kind"] = decision.get("kind") or "function"
            if decision["confidence"] not in {"low", "medium", "high"}:
                raise SystemExit(
                    f"line {line_number}: confidence must be low, medium, or high: {decision['confidence']}"
                )
            rows.append(decision)
    return rows


def validate_decisions(
    decisions: list[dict[str, str]],
    functions: dict[str, str],
    existing: list[dict[str, str]],
    update: bool,
) -> list[dict[str, str]]:
    errors: list[str] = []
    existing_by_entry = {row["entry"].lower(): row for row in existing}
    existing_name_owner = {row["new_name"]: row["entry"].lower() for row in existing}
    batch_entries: set[str] = set()
    batch_name_owner: dict[str, str] = {}
    output = []

    for decision in decisions:
        line = decision["line_number"]
        entry = decision["entry"]
        new_name = decision["new_name"]
        source_file = decision["source_file"].replace("\\", "/")

        if entry in batch_entries:
            errors.append(f"line {line}: duplicate batch entry {entry}")
        batch_entries.add(entry)

        other_batch_entry = batch_name_owner.get(new_name)
        if other_batch_entry is not None:
            errors.append(f"line {line}: duplicate batch symbol name {new_name} for {other_batch_entry} and {entry}")
        batch_name_owner[new_name] = entry

        if entry not in functions:
            errors.append(f"line {line}: entry {entry} is not present in {FUNCTIONS.relative_to(ROOT)}")
            continue

        if entry in existing_by_entry and not update:
            errors.append(f"line {line}: entry {entry} already exists; pass --update to modify it")

        existing_owner = existing_name_owner.get(new_name)
        if existing_owner is not None and existing_owner != entry:
            errors.append(f"line {line}: symbol name {new_name} already belongs to entry {existing_owner}")

        source_path = ROOT / source_file
        if not source_path.is_file():
            errors.append(f"line {line}: source file does not exist: {source_file}")
        else:
            text = source_path.read_text(encoding="utf-8", errors="replace")
            if new_name not in text:
                errors.append(f"line {line}: source file {source_file} does not contain {new_name}")

        current = existing_by_entry.get(entry)
        old_name = decision.get("old_name") or (current["old_name"] if current else functions[entry])
        output.append(
            {
                "entry": entry,
                "old_name": old_name,
                "new_name": new_name,
                "kind": decision["kind"],
                "confidence": decision["confidence"],
                "source_file": source_file,
                "notes": decision["notes"],
            }
        )

    if errors:
        for error in errors:
            print(error)
        raise SystemExit(1)
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--decisions", type=Path, required=True, help="CSV with entry,new_name,source_file,confidence,notes")
    parser.add_argument("--update", action="store_true", help="Update existing entries")
    parser.add_argument("--dry-run", action="store_true", help="Validate and print planned promotions without writing")
    args = parser.parse_args()

    decisions = load_decisions(args.decisions)
    functions = load_functions()
    existing = load_symbols()
    promotions = validate_decisions(decisions, functions, existing, args.update)

    if args.dry_run:
        for row in promotions:
            print(f"would promote {row['entry']}: {row['old_name']} -> {row['new_name']}")
        print(f"validated {len(promotions)} batch promotions")
        return 0

    promotion_entries = {row["entry"] for row in promotions}
    rows = [row for row in existing if row["entry"].lower() not in promotion_entries]
    rows.extend(promotions)
    write_symbols(rows)
    for row in promotions:
        print(f"promoted {row['entry']}: {row['old_name']} -> {row['new_name']}")
    print(f"wrote {len(promotions)} batch promotions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
