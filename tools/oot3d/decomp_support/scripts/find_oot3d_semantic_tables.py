#!/usr/bin/env python3
"""Find OOT3D data-table candidates using N64-derived semantic ID sequences."""

from __future__ import annotations

import argparse
import json
import re
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
DEFAULT_CODE_BIN = ROOT.parent / "work" / "extract" / "exefs" / "code.bin"
DEFAULT_SEMANTICS = ANALYSIS / "n64_actor_object_semantics.json"
DISASSEMBLY = ROOT / "ghidra_export" / "disassembly.txt"
FUNCTIONS = ROOT / "ghidra_export" / "functions.csv"
DECOMPILED = ROOT / "ghidra_export" / "decompiled"

HEADER_RE = re.compile(r"^//\s+(?P<name>\S+)\s+@\s+(?P<entry>[0-9a-fA-F]+)\s*$")
LITERAL_LOAD_RE = re.compile(r"\[(?:0x)?(?P<addr>[0-9a-fA-F]{6,8})\]")
DAT_REF_RE = re.compile(r"\b(?:DAT|PTR|DWORD|WORD|BYTE|s|u|FLOAT)_?(?P<addr>[0-9a-fA-F]{8})\b")


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


def load_function_files() -> dict[str, str]:
    rows: dict[str, str] = {}
    if not DECOMPILED.is_dir():
        return rows
    for path in DECOMPILED.glob("*.c"):
        parts = path.name.split("_", 2)
        if len(parts) >= 2 and re.fullmatch(r"[0-9a-fA-F]{8}", parts[1]):
            rows[parts[1].lower()] = rel(path)
    return rows


def load_disassembly_index() -> tuple[dict[str, dict[str, str]], dict[str, list[dict[str, object]]]]:
    by_entry: dict[str, dict[str, str]] = {}
    literal_refs: dict[str, list[dict[str, object]]] = {}
    if not DISASSEMBLY.is_file():
        return by_entry, literal_refs

    current_entry: str | None = None
    current_name = ""
    for line in DISASSEMBLY.read_text(encoding="utf-8", errors="replace").splitlines():
        header = HEADER_RE.match(line)
        if header:
            current_entry = header.group("entry").lower()
            current_name = header.group("name")
            by_entry[current_entry] = {"entry": current_entry, "name": current_name}
            continue
        if current_entry is None:
            continue
        if not line.strip():
            current_entry = None
            current_name = ""
            continue
        for match in LITERAL_LOAD_RE.finditer(line):
            addr = normalize_addr(int(match.group("addr"), 16))
            literal_refs.setdefault(addr, []).append(
                {
                    "function_entry": current_entry,
                    "function_name": current_name,
                    "line": line.strip(),
                }
            )
    return by_entry, literal_refs


def load_decompiled_ref_index(function_files: dict[str, str]) -> dict[str, list[dict[str, str]]]:
    refs: dict[str, list[dict[str, str]]] = {}
    for entry, file_name in function_files.items():
        path = ROOT / file_name
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for match in DAT_REF_RE.finditer(text):
            refs.setdefault(match.group("addr").lower(), []).append({"function_entry": entry, "file": file_name})
    return refs


def load_semantic_sequences(path: Path) -> list[dict[str, object]]:
    report = json.loads(path.read_text(encoding="utf-8"))
    sequences: list[dict[str, object]] = [
        {
            "domain": "actor",
            "name": "ActorId",
            "entries": [{"name": row["name"], "value": int(row["value"])} for row in report["actors"]],
        },
        {
            "domain": "object",
            "name": "ObjectId",
            "entries": [{"name": row["name"], "value": int(row["value"])} for row in report["objects"]],
        },
    ]
    for enum in report["item_enums"]:
        sequences.append(
            {
                "domain": "item",
                "name": enum["name"],
                "entries": [{"name": row["name"], "value": int(row["value"])} for row in enum["entries"]],
            }
        )
    return sequences


def decode_units(data: bytes, unit: int) -> tuple[int, ...]:
    count = len(data) // unit
    fmt = "<" + str(count) + ("H" if unit == 2 else "I")
    return struct.unpack(fmt, data[: count * unit])


def longest_runs(units: tuple[int, ...], base: int, entries: list[dict[str, object]], unit: int, min_run: int) -> list[dict[str, object]]:
    values = [int(row["value"]) for row in entries]
    by_value: dict[int, list[int]] = {}
    max_value = 0xFFFF if unit == 2 else 0xFFFFFFFF
    for index, value in enumerate(values):
        if 0 <= value <= max_value:
            by_value.setdefault(value, []).append(index)

    rows: list[dict[str, object]] = []
    position = 0
    while position < len(units):
        value = units[position]
        best_index = -1
        best_run = 0
        for source_index in by_value.get(value, []):
            run = 1
            while (
                source_index + run < len(values)
                and position + run < len(units)
                and units[position + run] == values[source_index + run]
            ):
                run += 1
            if run > best_run:
                best_index = source_index
                best_run = run
        if best_run >= min_run:
            run_entries = entries[best_index : best_index + best_run]
            address = base + position * unit
            rows.append(
                {
                    "address": address,
                    "unit": f"u{unit * 8}",
                    "source_index": best_index,
                    "run_length": best_run,
                    "first_name": run_entries[0]["name"],
                    "last_name": run_entries[-1]["name"],
                    "first_value": int(run_entries[0]["value"]),
                    "last_value": int(run_entries[-1]["value"]),
                }
            )
            position += best_run
            continue
        position += 1
    return rows


def pointer_refs(data: bytes, base: int, target: int) -> list[str]:
    refs: list[str] = []
    pattern = struct.pack("<I", target)
    start = 0
    while True:
        offset = data.find(pattern, start)
        if offset < 0:
            break
        refs.append(normalize_addr(base + offset))
        start = offset + 1
    return refs


def merge_candidates(rows: list[dict[str, object]]) -> list[dict[str, object]]:
    merged: dict[tuple[int, str, int, int], dict[str, object]] = {}
    for row in rows:
        key = (int(row["address"]), str(row["unit"]), int(row["source_index"]), int(row["run_length"]))
        out = merged.setdefault(
            key,
            {
                "address": row["address"],
                "unit": row["unit"],
                "source_index": row["source_index"],
                "run_length": row["run_length"],
                "matches": [],
            },
        )
        out["matches"].append(
            {
                "domain": row["domain"],
                "sequence": row["sequence"],
                "first_name": row["first_name"],
                "last_name": row["last_name"],
                "first_value": row["first_value"],
                "last_value": row["last_value"],
            }
        )
    return list(merged.values())


def attach_references(
    candidates: list[dict[str, object]],
    data: bytes,
    base: int,
    literal_refs: dict[str, list[dict[str, object]]],
    decompiled_refs: dict[str, list[dict[str, str]]],
    function_files: dict[str, str],
) -> None:
    for row in candidates:
        refs = []
        for ref_addr in pointer_refs(data, base, int(row["address"])):
            functions: dict[str, dict[str, object]] = {}
            for ref in literal_refs.get(ref_addr, []):
                functions[ref["function_entry"]] = {
                    "entry": ref["function_entry"],
                    "name": ref["function_name"],
                    "file": function_files.get(str(ref["function_entry"]), ""),
                    "evidence": ref["line"],
                }
            for ref in decompiled_refs.get(ref_addr, []):
                functions.setdefault(
                    ref["function_entry"],
                    {
                        "entry": ref["function_entry"],
                        "name": "",
                        "file": ref["file"],
                        "evidence": f"decompiled DAT_{ref_addr}",
                    },
                )
            refs.append({"literal_address": ref_addr, "functions": sorted(functions.values(), key=lambda item: item["entry"])})
        row["pointer_refs"] = refs
        row["pointer_ref_count"] = len(refs)
        row["function_ref_count"] = sum(len(ref["functions"]) for ref in refs)


def confidence(row: dict[str, object]) -> str:
    run = int(row["run_length"])
    funcs = int(row["function_ref_count"])
    if funcs and run >= 16:
        return "high"
    if funcs and run >= 8:
        return "medium"
    if run >= 24:
        return "medium"
    return "low"


def sort_key(row: dict[str, object]) -> tuple[int, int, int]:
    confidence_rank = {"high": 3, "medium": 2, "low": 1}
    return (
        confidence_rank.get(str(row.get("confidence")), 0),
        int(row["function_ref_count"]),
        int(row["pointer_ref_count"]),
        int(row["run_length"]),
    )


def make_markdown(report: dict[str, object], limit: int) -> str:
    candidates = report["candidates"][:limit] if limit else report["candidates"]
    lines = [
        "# OOT3D Semantic Table Candidates",
        "",
        "Generated from `scripts/find_oot3d_semantic_tables.py` using N64-derived actor/object/item ID sequences.",
        "",
        f"- Code image: `{report['code_image']}`",
        f"- Code base: `{report['code_base']}`",
        f"- Raw candidates: {report['raw_candidate_count']}",
        f"- Merged candidates: {report['candidate_count']}",
        f"- Candidates with pointer references: {report['referenced_candidate_count']}",
        "",
        "| Address | Confidence | Unit | Run | Matches | Pointer refs | Function refs |",
        "| --- | --- | --- | ---: | --- | ---: | ---: |",
    ]
    for row in candidates:
        match_text = ", ".join(
            f"{match['sequence']}[{row['source_index']}..+{row['run_length']}] `{match['first_name']}`..`{match['last_name']}`"
            for match in row["matches"][:3]
        )
        if len(row["matches"]) > 3:
            match_text += f", +{len(row['matches']) - 3} ambiguous"
        lines.append(
            f"| `{fmt_hex(int(row['address']))}` | {row['confidence']} | `{row['unit']}` | {row['run_length']} | {match_text} | {row['pointer_ref_count']} | {row['function_ref_count']} |"
        )

    lines.extend(["", "## Referenced Candidates", ""])
    shown_reference_addresses: set[int] = set()
    for row in candidates:
        if not row["pointer_refs"]:
            continue
        address = int(row["address"])
        if address in shown_reference_addresses:
            continue
        shown_reference_addresses.add(address)
        lines.append(f"### `{fmt_hex(int(row['address']))}`")
        lines.append("")
        for ref in row["pointer_refs"]:
            lines.append(f"- Literal pointer at `0x{ref['literal_address']}`")
            for func in ref["functions"]:
                file_text = f" ({func['file']})" if func.get("file") else ""
                name = func.get("name") or "unknown"
                lines.append(f"  - `{func['entry']}` `{name}`{file_text}: `{func['evidence']}`")
        lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--code-bin", type=Path, default=DEFAULT_CODE_BIN)
    parser.add_argument("--semantics", type=Path, default=DEFAULT_SEMANTICS)
    parser.add_argument("--code-base", type=lambda value: int(value, 0), default=0x00100000)
    parser.add_argument("--min-run-u16", type=int, default=8)
    parser.add_argument("--min-run-u32", type=int, default=6)
    parser.add_argument("--include-decompiled-refs", action="store_true", help="Also scan every decompiled C file for DAT/PTR references; slower.")
    parser.add_argument("--report-limit", type=int, default=80)
    parser.add_argument("--out-json", type=Path, default=ANALYSIS / "oot3d_semantic_table_candidates.json")
    parser.add_argument("--out-md", type=Path, default=ANALYSIS / "oot3d_semantic_table_candidates.md")
    args = parser.parse_args()

    if not args.code_bin.is_file():
        raise SystemExit(f"missing code image: {args.code_bin}")
    data = args.code_bin.read_bytes()
    sequences = load_semantic_sequences(args.semantics)
    decoded_units = {
        2: decode_units(data, 2),
        4: decode_units(data, 4),
    }

    raw: list[dict[str, object]] = []
    for sequence in sequences:
        entries = sequence["entries"]
        for unit, min_run in ((2, args.min_run_u16), (4, args.min_run_u32)):
            for row in longest_runs(decoded_units[unit], args.code_base, entries, unit, min_run):
                row["domain"] = sequence["domain"]
                row["sequence"] = sequence["name"]
                raw.append(row)

    candidates = merge_candidates(raw)
    function_files = load_function_files()
    _, literal_refs = load_disassembly_index()
    decompiled_refs = load_decompiled_ref_index(function_files) if args.include_decompiled_refs else {}
    attach_references(candidates, data, args.code_base, literal_refs, decompiled_refs, function_files)
    for row in candidates:
        row["confidence"] = confidence(row)
    candidates.sort(key=sort_key, reverse=True)

    report = {
        "code_image": rel(args.code_bin),
        "code_base": fmt_hex(args.code_base),
        "semantic_source": rel(args.semantics),
        "raw_candidate_count": len(raw),
        "candidate_count": len(candidates),
        "referenced_candidate_count": sum(1 for row in candidates if row["pointer_refs"]),
        "candidates": candidates,
    }
    args.out_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    args.out_md.write_text(make_markdown(report, args.report_limit), encoding="utf-8")
    print(
        json.dumps(
            {
                "semantic_table_candidates": len(candidates),
                "referenced_candidates": report["referenced_candidate_count"],
                "top_report": rel(args.out_md),
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
