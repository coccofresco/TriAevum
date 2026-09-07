#!/usr/bin/env python3
"""Resolve DAT/PTR references that contain OOT3D function pointers."""

from __future__ import annotations

import argparse
import csv
import json
import re
import struct
from collections import Counter, defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
DECOMPILED = ROOT / "ghidra_export" / "decompiled"
FUNCTIONS = ROOT / "ghidra_export" / "functions.csv"
DEFAULT_CODE_BIN = ROOT.parent / "work" / "extract" / "exefs" / "code.bin"
DEFAULT_CODE_BASE = 0x00100000

FILE_ENTRY_RE = re.compile(r"^[0-9]+_(?P<entry>[0-9a-fA-F]{8})_")
DAT_REF_RE = re.compile(
    r"\b(?P<name>(?:DAT|PTR|DWORD|WORD|BYTE|FLOAT|s|u)_?(?P<addr>[0-9a-fA-F]{8}))\b"
)
FIELDS = [
    "source_entry",
    "source_name",
    "source_file",
    "line",
    "ref_name",
    "literal_address",
    "pointer_value",
    "target_entry",
    "target_name",
    "thumb_adjusted",
    "context",
]


def normalize_addr(value: int) -> str:
    return f"{value:08x}"


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        try:
            return str(path.relative_to(ROOT.parent)).replace("\\", "/")
        except ValueError:
            return str(path).replace("\\", "/")


def fmt_hex(value: int) -> str:
    return f"0x{value:08X}"


def load_functions() -> dict[str, dict[str, str]]:
    with FUNCTIONS.open(newline="", encoding="utf-8") as handle:
        return {
            row["entry"].lower(): {
                "entry": row["entry"].lower(),
                "name": row["name"],
                "body_min": row["body_min"].lower(),
                "body_max": row["body_max"].lower(),
            }
            for row in csv.DictReader(handle)
        }


def read_u32(data: bytes, code_base: int, address: int) -> int | None:
    offset = address - code_base
    if offset < 0 or offset + 4 > len(data):
        return None
    return struct.unpack_from("<I", data, offset)[0]


def resolve_function_pointer(
    value: int,
    functions: dict[str, dict[str, str]],
) -> tuple[dict[str, str] | None, bool]:
    exact = functions.get(normalize_addr(value))
    if exact is not None:
        return exact, False

    thumb_value = value & ~1
    thumb = functions.get(normalize_addr(thumb_value))
    if thumb is not None and value & 1:
        return thumb, True
    return None, False


def iter_decompiled_entries(entries: set[str]) -> list[Path]:
    paths = []
    for path in sorted(DECOMPILED.glob("*.c")):
        match = FILE_ENTRY_RE.match(path.name)
        if not match:
            continue
        entry = match.group("entry").lower()
        if entries and entry not in entries:
            continue
        paths.append(path)
    return paths


def truncate_context(text: str, limit: int = 180) -> str:
    collapsed = " ".join(text.strip().split())
    if len(collapsed) <= limit:
        return collapsed
    return collapsed[: limit - 3] + "..."


def scan_refs(
    code: bytes,
    code_base: int,
    functions: dict[str, dict[str, str]],
    entries: set[str],
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for path in iter_decompiled_entries(entries):
        file_match = FILE_ENTRY_RE.match(path.name)
        if not file_match:
            continue
        source_entry = file_match.group("entry").lower()
        source = functions.get(source_entry, {"name": path.stem})
        for line_number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1):
            for match in DAT_REF_RE.finditer(line):
                literal_address = int(match.group("addr"), 16)
                value = read_u32(code, code_base, literal_address)
                if value is None:
                    continue
                target, thumb_adjusted = resolve_function_pointer(value, functions)
                if target is None:
                    continue
                rows.append(
                    {
                        "source_entry": source_entry,
                        "source_name": source["name"],
                        "source_file": rel(path),
                        "line": line_number,
                        "ref_name": match.group("name"),
                        "literal_address": normalize_addr(literal_address),
                        "pointer_value": normalize_addr(value),
                        "target_entry": target["entry"],
                        "target_name": target["name"],
                        "thumb_adjusted": thumb_adjusted,
                        "context": truncate_context(line),
                    }
                )
    rows.sort(
        key=lambda row: (
            int(str(row["source_entry"]), 16),
            int(str(row["line"])),
            int(str(row["literal_address"]), 16),
            int(str(row["target_entry"]), 16),
        )
    )
    return rows


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDS)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row[field] for field in FIELDS})


def unique_edges(rows: list[dict[str, object]]) -> list[dict[str, object]]:
    grouped: dict[tuple[str, str], dict[str, object]] = {}
    for row in rows:
        key = (str(row["source_entry"]), str(row["target_entry"]))
        current = grouped.get(key)
        if current is None:
            grouped[key] = {
                "source_entry": row["source_entry"],
                "source_name": row["source_name"],
                "source_file": row["source_file"],
                "target_entry": row["target_entry"],
                "target_name": row["target_name"],
                "refs": 1,
                "literal_addresses": {row["literal_address"]},
                "ref_names": {row["ref_name"]},
            }
        else:
            current["refs"] = int(current["refs"]) + 1
            current["literal_addresses"].add(row["literal_address"])
            current["ref_names"].add(row["ref_name"])

    edges = []
    for edge in grouped.values():
        edges.append(
            {
                **edge,
                "literal_addresses": sorted(edge["literal_addresses"], key=lambda value: int(str(value), 16)),
                "ref_names": sorted(edge["ref_names"]),
            }
        )
    edges.sort(key=lambda row: (-int(row["refs"]), str(row["source_entry"]), str(row["target_entry"])))
    return edges


def make_markdown(rows: list[dict[str, object]], code_bin: Path, code_base: int, report_limit: int) -> str:
    edges = unique_edges(rows)
    by_source: dict[str, set[str]] = defaultdict(set)
    by_target: dict[str, set[str]] = defaultdict(set)
    source_names: dict[str, str] = {}
    target_names: dict[str, str] = {}
    source_files: dict[str, str] = {}
    source_ref_counts: Counter[str] = Counter()
    target_ref_counts: Counter[str] = Counter()

    for row in rows:
        src = str(row["source_entry"])
        dst = str(row["target_entry"])
        by_source[src].add(dst)
        by_target[dst].add(src)
        source_names[src] = str(row["source_name"])
        target_names[dst] = str(row["target_name"])
        source_files[src] = str(row["source_file"])
        source_ref_counts[src] += 1
        target_ref_counts[dst] += 1

    lines = [
        "# Function Pointer References",
        "",
        "Generated from `scripts/extract_function_pointer_refs.py`.",
        "",
        f"- Code image: `{rel(code_bin)}`",
        f"- Code base: `{fmt_hex(code_base)}`",
        f"- Resolved references: `{len(rows)}`",
        f"- Unique source->target edges: `{len(edges)}`",
        f"- Source functions with pointer refs: `{len(by_source)}`",
        f"- Target functions referenced by pointer: `{len(by_target)}`",
        "",
        "## Top Sources",
        "",
        "| Source | Refs | Targets | File |",
        "| --- | ---: | --- | --- |",
    ]
    for source, count in source_ref_counts.most_common(30):
        target_text = ", ".join(f"`{target}` `{target_names[target]}`" for target in sorted(by_source[source])[:8])
        if len(by_source[source]) > 8:
            target_text += f", +{len(by_source[source]) - 8}"
        lines.append(
            f"| `{source}` `{source_names[source]}` | {count} | {target_text or '-'} | `{source_files[source]}` |"
        )

    lines.extend(
        [
            "",
            "## Top Targets",
            "",
            "| Target | Refs | Sources |",
            "| --- | ---: | ---: |",
        ]
    )
    for target, count in target_ref_counts.most_common(30):
        lines.append(f"| `{target}` `{target_names[target]}` | {count} | {len(by_target[target])} |")

    shown_edges = edges[:report_limit] if report_limit else edges
    lines.extend(
        [
            "",
            "## Edges",
            "",
            "| Source | Target | Refs | Literal addresses |",
            "| --- | --- | ---: | --- |",
        ]
    )
    for edge in shown_edges:
        literal_text = ", ".join(f"`0x{addr}`" for addr in edge["literal_addresses"][:8])
        if len(edge["literal_addresses"]) > 8:
            literal_text += f", +{len(edge['literal_addresses']) - 8}"
        lines.append(
            f"| `{edge['source_entry']}` `{edge['source_name']}` | "
            f"`{edge['target_entry']}` `{edge['target_name']}` | {edge['refs']} | {literal_text or '-'} |"
        )

    shown_rows = rows[:report_limit] if report_limit else rows
    lines.extend(
        [
            "",
            "## References",
            "",
            "| Source | Line | DAT/PTR | Value | Target | Context |",
            "| --- | ---: | --- | --- | --- | --- |",
        ]
    )
    for row in shown_rows:
        context = str(row["context"]).replace("|", "\\|")
        lines.append(
            f"| `{row['source_entry']}` `{row['source_name']}` | {row['line']} | "
            f"`{row['ref_name']}` (`0x{row['literal_address']}`) | `0x{row['pointer_value']}` | "
            f"`{row['target_entry']}` `{row['target_name']}` | `{context}` |"
        )
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("entries", nargs="*", help="Optional source function entries to scan; defaults to all.")
    parser.add_argument("--code-bin", type=Path, default=DEFAULT_CODE_BIN)
    parser.add_argument("--code-base", type=lambda value: int(value, 0), default=DEFAULT_CODE_BASE)
    parser.add_argument("--out-json", type=Path, default=ANALYSIS / "function_pointer_refs.json")
    parser.add_argument("--out-md", type=Path, default=ANALYSIS / "function_pointer_refs.md")
    parser.add_argument("--out-csv", type=Path, default=ANALYSIS / "function_pointer_refs.csv")
    parser.add_argument("--report-limit", type=int, default=250)
    args = parser.parse_args()

    if not args.code_bin.is_file():
        raise SystemExit(f"missing code image: {args.code_bin}")
    if not FUNCTIONS.is_file():
        raise SystemExit(f"missing functions CSV: {FUNCTIONS}")

    functions = load_functions()
    entries = {entry.strip().lower().removeprefix("0x").zfill(8) for entry in args.entries}
    code = args.code_bin.read_bytes()
    rows = scan_refs(code, args.code_base, functions, entries)
    report = {
        "code_image": rel(args.code_bin),
        "code_base": fmt_hex(args.code_base),
        "resolved_reference_count": len(rows),
        "edge_count": len(unique_edges(rows)),
        "rows": rows,
    }

    args.out_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    write_csv(args.out_csv, rows)
    args.out_md.write_text(make_markdown(rows, args.code_bin, args.code_base, args.report_limit), encoding="utf-8")
    print(
        json.dumps(
            {
                "resolved_function_pointer_refs": len(rows),
                "unique_edges": len(unique_edges(rows)),
                "report": rel(args.out_md),
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
