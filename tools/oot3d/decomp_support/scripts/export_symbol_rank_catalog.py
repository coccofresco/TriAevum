#!/usr/bin/env python3
"""Export a RedPepper-style function rank catalog from current OOT3D artifacts."""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path
from typing import Any

from audit_c_conversion_readiness import BASELINE_DEFINES, source_style
from compare_runtime_objects import read_target_functions


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT_JSON = ROOT / "analysis" / "symbol_rank_catalog.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "symbol_rank_catalog.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "symbol_rank_catalog.md"


def rel(path: Path | str) -> str:
    path = Path(path)
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def read_json(path: Path, default: Any) -> Any:
    if not path.is_file():
        return default
    return json.loads(path.read_text(encoding="utf-8"))


def object_stem(source: str) -> str:
    without_extension = source.rsplit(".", 1)[0]
    return without_extension.replace("\\", "_").replace("/", "_").replace(":", "_").replace(" ", "_")


def dump_to_source_map(manifest: dict[str, Any]) -> dict[str, str]:
    sources = [str(source) for source in manifest.get("sources", [])]
    by_dump = {f"{object_stem(source)}.dump": source for source in sources}
    for source, obj in zip(sources, [str(obj) for obj in manifest.get("objects", [])]):
        by_dump[Path(obj).with_suffix(".dump").name] = source
    return by_dump


def read_manual_symbols(path: Path) -> dict[str, dict[str, str]]:
    if not path.is_file():
        return {}
    rows: dict[str, dict[str, str]] = {}
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            if row.get("kind") != "function":
                continue
            entry = str(row.get("entry", "")).lower().zfill(8)
            rows[entry] = row
    return rows


def compare_category(row: dict[str, Any] | None) -> str:
    if not row:
        return "U"
    if row.get("exact_match"):
        return "O"
    target_count = int(row.get("target_instruction_count", 0) or 0)
    compiled_count = int(row.get("compiled_instruction_count", 0) or 0)
    lcs_ratio = float(row.get("lcs_target_ratio", 0.0) or 0.0)
    prefix = int(row.get("matching_prefix", 0) or 0)
    suffix = int(row.get("matching_suffix", 0) or 0)
    if abs(target_count - compiled_count) <= 2 and (lcs_ratio >= 0.75 or prefix >= 5 or suffix >= 5):
        return "m"
    return "M"


def first_difference_text(row: dict[str, Any] | None) -> str:
    if not row:
        return ""
    diff = row.get("first_difference")
    if not isinstance(diff, dict):
        return ""
    return f"{diff.get('index')}: {diff.get('target')} vs {diff.get('compiled')}"


def build_rows(args: argparse.Namespace) -> list[dict[str, Any]]:
    target_functions = read_target_functions(args.target_disassembly)
    manual_by_entry = read_manual_symbols(args.manual_symbols)
    compare_rows = read_json(args.compare_json, [])
    dump_sources = dump_to_source_map(read_json(args.manifest_json, {}))
    compare_by_name = {str(row.get("name", "")): row for row in compare_rows if isinstance(row, dict)}
    compare_by_entry = {
        str(row.get("target_entry", "")).lower().zfill(8): row for row in compare_rows if isinstance(row, dict)
    }

    rows: list[dict[str, Any]] = []
    for target_name, target in target_functions.items():
        entry = str(target.get("entry", "")).lower().zfill(8)
        manual = manual_by_entry.get(entry, {})
        name = manual.get("new_name") or target_name
        source_file = manual.get("source_file", "")
        compare = compare_by_name.get(name) or compare_by_entry.get(entry)
        rank = compare_category(compare)
        compiled_source = ""
        if compare:
            compiled_source = dump_sources.get(Path(str(compare.get("compiled_dump", ""))).name, "")
        style = ""
        if compiled_source and compare and compare.get("exact_match"):
            style = source_style(compiled_source, str(compare.get("name", name)), set(BASELINE_DEFINES))
        rows.append(
            {
                "rank": rank,
                "entry": entry,
                "name": name,
                "target_name": target_name,
                "source_file": source_file,
                "compiled_source": compiled_source,
                "source_style": style,
                "target_instruction_count": len(target.get("ops", [])),
                "compiled_instruction_count": int(compare.get("compiled_instruction_count", 0) or 0) if compare else 0,
                "matching_prefix": int(compare.get("matching_prefix", 0) or 0) if compare else 0,
                "matching_suffix": int(compare.get("matching_suffix", 0) or 0) if compare else 0,
                "lcs_instruction_count": int(compare.get("lcs_instruction_count", 0) or 0) if compare else 0,
                "lcs_target_ratio": float(compare.get("lcs_target_ratio", 0.0) or 0.0) if compare else 0.0,
                "first_difference": first_difference_text(compare),
                "compiled_dump": rel(compare.get("compiled_dump", "")) if compare else "",
                "manual_confidence": manual.get("confidence", ""),
                "manual_notes": manual.get("notes", ""),
            }
        )
    return sorted(rows, key=lambda row: (row["rank"] == "U", row["entry"]))


def summarize(rows: list[dict[str, Any]], args: argparse.Namespace) -> dict[str, Any]:
    ranks = Counter(row["rank"] for row in rows)
    styles = Counter(row["source_style"] for row in rows if row["rank"] == "O")
    mapped = [row for row in rows if row["source_file"]]
    return {
        "target_functions": len(rows),
        "rank_counts": {rank: ranks[rank] for rank in ["O", "m", "M", "U"]},
        "exact_plain_c": styles["plain-c"],
        "exact_inline_asm": styles["c-inline-asm"],
        "exact_naked_asm": styles["naked-asm"],
        "exact_unknown_style": sum(
            count for style, count in styles.items() if style not in {"plain-c", "c-inline-asm", "naked-asm"}
        ),
        "manual_mapped_functions": len(mapped),
        "manual_mapped_rank_counts": {rank: Counter(row["rank"] for row in mapped)[rank] for rank in ["O", "m", "M", "U"]},
        "target_instructions": sum(int(row["target_instruction_count"]) for row in rows),
        "exact_target_instructions": sum(int(row["target_instruction_count"]) for row in rows if row["rank"] == "O"),
        "compare_json": rel(args.compare_json),
        "target_disassembly": rel(args.target_disassembly),
        "manual_symbols": rel(args.manual_symbols),
    }


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "rank",
        "entry",
        "name",
        "target_name",
        "source_file",
        "compiled_source",
        "source_style",
        "target_instruction_count",
        "compiled_instruction_count",
        "matching_prefix",
        "matching_suffix",
        "lcs_instruction_count",
        "lcs_target_ratio",
        "first_difference",
        "compiled_dump",
        "manual_confidence",
        "manual_notes",
    ]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: row.get(key, "") for key in fieldnames})


def write_json(path: Path, rows: list[dict[str, Any]], summary: dict[str, Any], include_rows: bool) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    data: dict[str, Any] = {"summary": summary, "row_count": len(rows)}
    if include_rows:
        data["rows"] = rows
    else:
        data["rows_omitted"] = "full row data is written to the CSV report; pass --full-json to include rows here"
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def write_markdown(path: Path, rows: list[dict[str, Any]], summary: dict[str, Any]) -> None:
    rank_counts = summary["rank_counts"]
    mapped_counts = summary["manual_mapped_rank_counts"]
    lines = [
        "# Symbol Rank Catalog",
        "",
        "RedPepper-inspired function rank catalog generated from the current OOT3D disassembly, manual symbols, and matched-object comparison.",
        "",
        "Ranks: `O` exact, `m` minor/codegen-near, `M` major mismatch, `U` undecompiled or not compared.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Target functions | {summary['target_functions']} |",
        f"| Rank O exact | {rank_counts['O']} |",
        f"| Rank m minor | {rank_counts['m']} |",
        f"| Rank M major | {rank_counts['M']} |",
        f"| Rank U undecompiled/not compared | {rank_counts['U']} |",
        f"| Manual mapped functions | {summary['manual_mapped_functions']} |",
        f"| Manual mapped O/m/M/U | {mapped_counts['O']}/{mapped_counts['m']}/{mapped_counts['M']}/{mapped_counts['U']} |",
        f"| Exact plain-C functions | {summary['exact_plain_c']} |",
        f"| Exact inline-asm functions | {summary['exact_inline_asm']} |",
        f"| Exact naked-asm functions | {summary['exact_naked_asm']} |",
        f"| Exact target instructions | {summary['exact_target_instructions']} |",
        f"| Total target instructions | {summary['target_instructions']} |",
        "",
        "## Non-U Queue",
        "",
        "| Rank | Entry | Function | Source | Style | Target insns | Compiled insns | First difference |",
        "| --- | --- | --- | --- | --- | ---: | ---: | --- |",
    ]
    non_u = [row for row in rows if row["rank"] != "U"]
    for row in non_u:
        lines.append(
            f"| `{row['rank']}` | `{row['entry']}` | `{row['name']}` | `{row['compiled_source'] or row['source_file']}` | "
            f"`{row['source_style']}` | {row['target_instruction_count']} | "
            f"{row['compiled_instruction_count']} | `{row['first_difference']}` |"
        )
    lines.extend(
        [
            "",
            "## First U Rows",
            "",
            "| Entry | Function | Target insns | Manual source |",
            "| --- | --- | ---: | --- |",
        ]
    )
    for row in [row for row in rows if row["rank"] == "U"][:50]:
        lines.append(
            f"| `{row['entry']}` | `{row['name']}` | {row['target_instruction_count']} | `{row['source_file']}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target-disassembly", type=Path, default=ROOT / "ghidra_export" / "disassembly.txt")
    parser.add_argument("--manual-symbols", type=Path, default=ROOT / "symbols" / "manual_symbols.csv")
    parser.add_argument("--compare-json", type=Path, default=ROOT / "build" / "matched" / "compare_matched_objects.json")
    parser.add_argument("--manifest-json", type=Path, default=ROOT / "build" / "matched" / "manifest.json")
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--full-json", action="store_true", help="Include every symbol row in the JSON output.")
    args = parser.parse_args()

    rows = build_rows(args)
    summary = summarize(rows, args)
    write_json(args.out_json, rows, summary, args.full_json)
    write_csv(args.out_csv, rows)
    write_markdown(args.out_md, rows, summary)

    ranks = summary["rank_counts"]
    print(
        "symbol rank catalog: "
        f"{summary['target_functions']} target functions, "
        f"O/m/M/U={ranks['O']}/{ranks['m']}/{ranks['M']}/{ranks['U']}, "
        f"plain-C exact={summary['exact_plain_c']}"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
