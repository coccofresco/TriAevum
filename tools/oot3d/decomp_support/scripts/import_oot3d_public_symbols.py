#!/usr/bin/env python3
"""Import safe symbol names from gamestabled/oot3d_before_public linker script."""

from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_REFERENCE = ROOT.parent / "external" / "oot3d_before_public"
DEFAULT_FUNCTIONS = ROOT / "ghidra_export" / "functions.csv"
DEFAULT_MANUAL = ROOT / "symbols" / "manual_symbols.csv"
DEFAULT_OUT_CSV = ROOT / "analysis" / "oot3d_public_symbol_promotions.csv"
DEFAULT_OUT_JSON = ROOT / "analysis" / "oot3d_public_symbol_promotions.json"
DEFAULT_OUT_MD = ROOT / "analysis" / "oot3d_public_symbol_promotions.md"
FIELDS = ["entry", "old_name", "new_name", "kind", "confidence", "source_file", "notes"]
LINKER_SYMBOL_RE = re.compile(r"^\s*([A-Za-z_.$][A-Za-z0-9_.$@?<>~]*)\s+0x([0-9A-Fa-f]{8})\s*$")
SAFE_C_NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")
FUN_NAME_RE = re.compile(r"^FUN_[0-9A-Fa-f]{8}$")


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def load_functions(path: Path) -> dict[str, dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return {row["entry"].lower(): row for row in csv.DictReader(handle)}


def load_manual(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def write_manual(path: Path, rows: list[dict[str, str]]) -> None:
    rows = sorted(rows, key=lambda row: int(row["entry"], 16))
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows)


def parse_linker_symbols(reference: Path) -> list[dict[str, str]]:
    linker = reference / "data" / "oot3d.ld"
    rows: list[dict[str, str]] = []
    for line_number, line in enumerate(linker.read_text(encoding="utf-8", errors="replace").splitlines(), start=1):
        match = LINKER_SYMBOL_RE.match(line)
        if not match:
            continue
        rows.append(
            {
                "line": str(line_number),
                "name": match.group(1),
                "entry": match.group(2).lower(),
            }
        )
    return rows


def build_candidates(
    reference: Path,
    functions: dict[str, dict[str, str]],
    manual: list[dict[str, str]],
    evidence_path: Path,
) -> tuple[list[dict[str, str]], dict[str, Any], list[dict[str, str]]]:
    manual_entries = {row["entry"].lower() for row in manual}
    manual_names = {row["new_name"] for row in manual}
    seen_names: dict[str, str] = {}
    candidates: list[dict[str, str]] = []
    skipped: list[dict[str, str]] = []
    summary: dict[str, Any] = {
        "reference": str(reference),
        "linker_script": str(reference / "data" / "oot3d.ld"),
        "linker_symbols": 0,
        "function_address_hits": 0,
        "safe_candidates": 0,
        "skipped_existing_manual_entry": 0,
        "skipped_existing_manual_name": 0,
        "skipped_duplicate_public_name": 0,
        "skipped_fun_name": 0,
        "skipped_unsafe_name": 0,
        "skipped_non_function_address": 0,
    }

    for symbol in parse_linker_symbols(reference):
        summary["linker_symbols"] += 1
        entry = symbol["entry"]
        public_name = symbol["name"]
        function = functions.get(entry)
        if function is None:
            summary["skipped_non_function_address"] += 1
            continue
        summary["function_address_hits"] += 1
        reason = ""
        if entry in manual_entries:
            reason = "existing-manual-entry"
            summary["skipped_existing_manual_entry"] += 1
        elif public_name in manual_names:
            reason = "existing-manual-name"
            summary["skipped_existing_manual_name"] += 1
        elif public_name in seen_names:
            reason = "duplicate-public-name"
            summary["skipped_duplicate_public_name"] += 1
        elif FUN_NAME_RE.match(public_name):
            reason = "fun-placeholder"
            summary["skipped_fun_name"] += 1
        elif not SAFE_C_NAME_RE.match(public_name):
            reason = "unsafe-or-mangled-name"
            summary["skipped_unsafe_name"] += 1

        if reason:
            skipped.append(
                {
                    "entry": entry,
                    "public_name": public_name,
                    "current_name": function["name"],
                    "reason": reason,
                    "linker_line": symbol["line"],
                }
            )
            continue

        seen_names[public_name] = entry
        candidates.append(
            {
                "entry": entry,
                "old_name": function["name"],
                "new_name": public_name,
                "kind": "function",
                "confidence": "high",
                "source_file": rel(evidence_path),
                "notes": (
                    "Imported from gamestabled/oot3d_before_public data/oot3d.ld "
                    f"(CC0) with explicit linker address 0x{entry}; linker line {symbol['line']}."
                ),
            }
        )

    summary["safe_candidates"] = len(candidates)
    return candidates, summary, skipped


def write_csv(path: Path, rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDS + ["approved"])
        writer.writeheader()
        for row in rows:
            writer.writerow({**row, "approved": "yes"})


def write_json(path: Path, summary: dict[str, Any], rows: list[dict[str, str]], skipped: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps({"summary": summary, "rows": rows, "skipped": skipped}, indent=2) + "\n",
        encoding="utf-8",
    )


def write_markdown(path: Path, summary: dict[str, Any], rows: list[dict[str, str]], skipped: list[dict[str, str]]) -> None:
    lines = [
        "# oot3d_before_public Symbol Promotions",
        "",
        "Generated from `scripts/import_oot3d_public_symbols.py`.",
        "",
        "Source: `external/oot3d_before_public/data/oot3d.ld` from `gamestabled/oot3d_before_public` (CC0).",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
    ]
    for key in (
        "linker_symbols",
        "function_address_hits",
        "safe_candidates",
        "skipped_existing_manual_entry",
        "skipped_existing_manual_name",
        "skipped_duplicate_public_name",
        "skipped_fun_name",
        "skipped_unsafe_name",
        "skipped_non_function_address",
    ):
        lines.append(f"| `{key}` | {summary[key]} |")

    lines.extend(
        [
            "",
            "## Approved Promotions",
            "",
            "| Entry | Current | Public name | Notes |",
            "| --- | --- | --- | --- |",
        ]
    )
    for row in rows:
        lines.append(f"| `{row['entry']}` | `{row['old_name']}` | `{row['new_name']}` | {row['notes']} |")

    lines.extend(
        [
            "",
            "## Skipped Examples",
            "",
            "| Entry | Current | Public name | Reason |",
            "| --- | --- | --- | --- |",
        ]
    )
    for row in skipped[:200]:
        lines.append(
            f"| `{row['entry']}` | `{row['current_name']}` | `{row['public_name']}` | `{row['reason']}` |"
        )
    lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, default=DEFAULT_REFERENCE)
    parser.add_argument("--functions", type=Path, default=DEFAULT_FUNCTIONS)
    parser.add_argument("--manual-symbols", type=Path, default=DEFAULT_MANUAL)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--apply", action="store_true", help="Append safe candidates to manual_symbols.csv")
    parser.add_argument("--limit", type=int, default=0, help="Limit promotions for staged imports; 0 means no limit")
    args = parser.parse_args()

    functions = load_functions(args.functions)
    manual = load_manual(args.manual_symbols)
    candidates, summary, skipped = build_candidates(args.reference, functions, manual, args.out_md)
    if args.limit > 0:
        candidates = candidates[: args.limit]
        summary = dict(summary)
        summary["safe_candidates"] = len(candidates)
        summary["limit"] = args.limit

    write_markdown(args.out_md, summary, candidates, skipped)
    write_csv(args.out_csv, candidates)
    write_json(args.out_json, summary, candidates, skipped)

    if args.apply and candidates:
        existing_entries = {row["entry"].lower() for row in manual}
        merged = [row for row in manual if row["entry"].lower() not in {candidate["entry"] for candidate in candidates}]
        for candidate in candidates:
            if candidate["entry"] not in existing_entries:
                merged.append({field: candidate[field] for field in FIELDS})
        write_manual(args.manual_symbols, merged)

    print(
        "oot3d public symbol import: "
        f"{summary['safe_candidates']} safe candidates from {summary['function_address_hits']} function-address hits"
    )
    if args.apply:
        print(f"applied {len(candidates)} promotions to {rel(args.manual_symbols)}")
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
