#!/usr/bin/env python3
"""Build data-symbol labels from OOT3D semantic table candidate evidence."""

from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
DEFAULT_CANDIDATES = ANALYSIS / "oot3d_semantic_table_candidates.json"
DEFAULT_SYMBOLS = ROOT / "symbols" / "data_symbols.csv"
FIELDS = ["address", "old_name", "new_name", "kind", "confidence", "source_file", "notes"]


def normalize_address(value: int | str) -> str:
    if isinstance(value, int):
        return f"{value:08x}"
    text = value.lower().removeprefix("0x")
    if not re.fullmatch(r"[0-9a-f]{8}", text):
        raise ValueError(f"address must be 8 hex digits: {value}")
    return text


def symbol_suffix(address: str) -> str:
    return address.lower()


def first_sequence(row: dict[str, object]) -> str:
    matches = row.get("matches", [])
    if not matches:
        return "semantic"
    return str(matches[0]["sequence"]).lower()


def make_table_name(row: dict[str, object]) -> str:
    address = normalize_address(row["address"])
    unit = str(row["unit"])
    confidence = str(row["confidence"])
    if confidence == "high":
        return f"oot3d_semantic_{first_sequence(row)}_{unit}_run_{symbol_suffix(address)}"
    return f"oot3d_candidate_semantic_{unit}_run_{symbol_suffix(address)}"


def make_pointer_name(table_name: str) -> str:
    stem = table_name.removeprefix("oot3d_")
    return f"oot3d_ptr_{stem}"


def notes_for_candidate(row: dict[str, object]) -> str:
    match_parts = []
    for match in row.get("matches", [])[:4]:
        match_parts.append(
            f"{match['sequence']}[{row['source_index']}..+{row['run_length']}] {match['first_name']}..{match['last_name']}"
        )
    if len(row.get("matches", [])) > 4:
        match_parts.append(f"+{len(row['matches']) - 4} ambiguous matches")
    refs = []
    for ref in row.get("pointer_refs", []):
        for function in ref.get("functions", []):
            refs.append(f"{function['entry']}:{function['name']} via 0x{ref['literal_address']}")
    ref_text = "; refs " + ", ".join(refs[:4]) if refs else ""
    if len(refs) > 4:
        ref_text += f", +{len(refs) - 4} more"
    return "; ".join(match_parts) + ref_text


def collect_rows(candidates_path: Path, min_confidence: str) -> list[dict[str, str]]:
    rank = {"low": 1, "medium": 2, "high": 3}
    threshold = rank[min_confidence]
    report = json.loads(candidates_path.read_text(encoding="utf-8"))
    rows: list[dict[str, str]] = []
    seen_table_addresses: set[str] = set()
    seen_pointer_addresses: set[str] = set()
    for candidate in report["candidates"]:
        confidence = str(candidate["confidence"])
        if rank[confidence] < threshold:
            continue
        if int(candidate.get("pointer_ref_count", 0)) == 0:
            continue
        address = normalize_address(candidate["address"])
        table_name = make_table_name(candidate)
        if address not in seen_table_addresses:
            seen_table_addresses.add(address)
            rows.append(
                {
                    "address": address,
                    "old_name": f"DAT_{address}",
                    "new_name": table_name,
                    "kind": "semantic_table_candidate",
                    "confidence": confidence,
                    "source_file": "analysis/oot3d_semantic_table_candidates.md",
                    "notes": notes_for_candidate(candidate),
                }
            )

        for ref in candidate.get("pointer_refs", []):
            pointer_address = normalize_address(ref["literal_address"])
            if pointer_address in seen_pointer_addresses:
                continue
            seen_pointer_addresses.add(pointer_address)
            function_refs = ", ".join(
                f"{function['entry']}:{function['name']}" for function in ref.get("functions", [])[:4]
            )
            rows.append(
                {
                    "address": pointer_address,
                    "old_name": f"DAT_{pointer_address}",
                    "new_name": make_pointer_name(table_name),
                    "kind": "semantic_table_pointer",
                    "confidence": confidence,
                    "source_file": "analysis/oot3d_semantic_table_candidates.md",
                    "notes": f"Literal pointer to {table_name} at 0x{address}; referenced by {function_refs}",
                }
            )

    return sorted(rows, key=lambda row: int(row["address"], 16))


def write_csv(rows: list[dict[str, str]], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows)


def write_markdown(rows: list[dict[str, str]], path: Path, symbols_path: Path) -> None:
    lines = [
        "# OOT3D Semantic Data Symbols",
        "",
        "Generated from `scripts/build_semantic_data_symbols.py`.",
        "",
        f"- Symbol CSV: `{symbols_path.relative_to(ROOT).as_posix()}`",
        f"- Symbols generated: {len(rows)}",
        "",
        "| Address | Name | Kind | Confidence | Notes |",
        "| --- | --- | --- | --- | --- |",
    ]
    for row in rows:
        notes = row["notes"].replace("|", "\\|")
        if len(notes) > 180:
            notes = notes[:177] + "..."
        lines.append(f"| `0x{row['address']}` | `{row['new_name']}` | `{row['kind']}` | {row['confidence']} | {notes} |")
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidates", type=Path, default=DEFAULT_CANDIDATES)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_SYMBOLS)
    parser.add_argument("--out-md", type=Path, default=ANALYSIS / "oot3d_semantic_data_symbols.md")
    parser.add_argument("--min-confidence", choices=["low", "medium", "high"], default="medium")
    args = parser.parse_args()

    rows = collect_rows(args.candidates, args.min_confidence)
    write_csv(rows, args.out_csv)
    write_markdown(rows, args.out_md, args.out_csv)
    print(json.dumps({"data_symbols": len(rows), "symbols": str(args.out_csv.relative_to(ROOT))}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
