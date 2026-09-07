#!/usr/bin/env python3
"""Map OOT3D message-context usage against N64-derived message semantics."""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
DECOMPILED = ROOT / "ghidra_export" / "decompiled"
FUNCTIONS = ROOT / "ghidra_export" / "functions.csv"
MANUAL_SYMBOLS = ROOT / "symbols" / "manual_symbols.csv"
MESSAGE_CANDIDATES = ANALYSIS / "message_function_candidates.json"
MESSAGE_SEMANTICS = ANALYSIS / "n64_message_semantics.json"

ENTRY_RE = re.compile(r"_(?P<entry>[0-9a-fA-F]{8})_")
CALL_RE = re.compile(r"\bFUN_(?P<entry>[0-9a-fA-F]{8})\s*\(")
OFFSET_RE = re.compile(r"param_1\s*\+\s*(?P<offset>0x[0-9a-fA-F]+)")
WORD_OFFSET_RE = re.compile(r"\*\([^)]*\*\)\(param_1\s*\+\s*(?P<offset>0x[0-9a-fA-F]+)\)")
HEX_RE = re.compile(r"\b0x[0-9a-fA-F]+\b")

KNOWN_MESSAGE_ANCHORS = {
    "002d0320": "Message_StartTextbox analogue",
    "00419e18": "message resource loader for rom:/message/*.qm and .qbf",
    "0042cbcc": "message context update target",
    "0046b114": "sys8 message font initializer",
}
KNOWN_MESSAGE_ANCHOR_NAMES = {
    "oot3d_message_start_textbox": "002d0320",
    "oot3d_load_message_resources": "00419e18",
    "oot3d_message_context_update": "0042cbcc",
    "oot3d_init_sys_message_font": "0046b114",
}
KNOWN_MESSAGE_ANCHOR_NAME_RE = re.compile(
    r"\b(?P<name>" + "|".join(re.escape(name) for name in KNOWN_MESSAGE_ANCHOR_NAMES) + r")\s*\("
)

PLAYSTATE_OFFSETS = {
    0x32C0: "messageCtx",
    0x22F0: "message-adjacent initialized subsystem",
    0x23C8: "message-adjacent initialized subsystem",
    0x25F0: "message-adjacent per-frame subsystem",
    0x4290: "message-adjacent update subsystem",
    0x59A0: "message-adjacent update subsystem",
    0x72D4: "message-adjacent resource handle",
    0x7358: "message-adjacent initialized subsystem",
    0x7440: "message-adjacent callback slot",
}


def rel(path: Path) -> str:
    return str(path.relative_to(ROOT)).replace("\\", "/")


def load_csv_names(path: Path, key: str, value: str) -> dict[str, str]:
    if not path.is_file():
        return {}
    with path.open(newline="", encoding="utf-8") as handle:
        return {row[key].lower(): row[value] for row in csv.DictReader(handle)}


def entry_from_path(path: Path) -> str | None:
    match = ENTRY_RE.search(path.name)
    return match.group("entry").lower() if match else None


def load_function_files() -> dict[str, Path]:
    rows: dict[str, Path] = {}
    for path in DECOMPILED.glob("*.c"):
        entry = entry_from_path(path)
        if entry:
            rows[entry] = path
    return rows


def load_message_semantic_values(path: Path) -> dict[int, list[str]]:
    if not path.is_file():
        return {}
    report = json.loads(path.read_text(encoding="utf-8"))
    values: dict[int, list[str]] = defaultdict(list)
    for row in report.get("defines", []):
        values[int(row["value"])].append(str(row["name"]))
    for enum in report.get("enums", []):
        for entry in enum.get("entries", []):
            values[int(entry["value"])].append(str(entry["name"]))
    return dict(values)


def summarize_function(
    entry: str,
    path: Path,
    manual: dict[str, str],
    functions: dict[str, str],
    semantic_values: dict[int, list[str]],
) -> dict[str, object]:
    text = path.read_text(encoding="utf-8", errors="replace")
    calls = {match.group("entry").lower() for match in CALL_RE.finditer(text)}
    calls |= {KNOWN_MESSAGE_ANCHOR_NAMES[match.group("name")] for match in KNOWN_MESSAGE_ANCHOR_NAME_RE.finditer(text)}
    calls = sorted(calls - {entry})
    offsets = sorted({int(match.group("offset"), 16) for match in OFFSET_RE.finditer(text)})
    direct_field_offsets = sorted({int(match.group("offset"), 16) for match in WORD_OFFSET_RE.finditer(text)})

    matched_literals = []
    for literal in sorted({int(value, 16) for value in HEX_RE.findall(text)}):
        names = semantic_values.get(literal)
        if names:
            matched_literals.append({"value": f"0x{literal:x}", "names": sorted(names)[:10]})

    anchor_calls = [
        {"entry": call, "role": KNOWN_MESSAGE_ANCHORS[call]}
        for call in calls
        if call in KNOWN_MESSAGE_ANCHORS
    ]
    playstate_offsets = [
        {
            "offset": f"0x{offset:x}",
            "role": PLAYSTATE_OFFSETS.get(offset, "unclassified message-candidate offset"),
        }
        for offset in offsets
        if offset in PLAYSTATE_OFFSETS
    ]
    return {
        "entry": entry,
        "name": manual.get(entry, functions.get(entry, f"FUN_{entry}")),
        "file": rel(path),
        "known_role": KNOWN_MESSAGE_ANCHORS.get(entry),
        "anchor_calls": anchor_calls,
        "playstate_offsets": playstate_offsets,
        "direct_field_offsets": [f"0x{offset:x}" for offset in direct_field_offsets[:40]],
        "n64_semantic_literal_matches": matched_literals[:40],
    }


def make_markdown(report: dict[str, object]) -> str:
    lines = [
        "# Message Context Usage Map",
        "",
        "Generated from `scripts/map_message_context_usage.py`.",
        "",
        f"- Candidate functions analyzed: {report['candidate_count']}",
        f"- Functions with known message anchor calls: {report['functions_with_anchor_calls']}",
        f"- Functions with `PlayState+0x32c0` evidence: {report['functions_with_message_ctx_offset']}",
        f"- N64 semantic constants/enums used for literal matching: {report['semantic_value_count']}",
        "",
        "## Strong Anchors",
        "",
        "| Entry | Current name | Evidence | File |",
        "| --- | --- | --- | --- |",
    ]
    for row in report["functions"]:
        evidence = []
        if row["known_role"]:
            evidence.append(row["known_role"])
        for call in row["anchor_calls"]:
            evidence.append(f"`FUN_{call['entry']}`: {call['role']}")
        for offset in row["playstate_offsets"]:
            if offset["offset"] == "0x32c0":
                evidence.append("uses `PlayState+0x32c0` as `messageCtx`")
        if not evidence:
            continue
        lines.append(
            f"| `{row['entry']}` | `{row['name']}` | {'; '.join(evidence)} | `{row['file']}` |"
        )

    lines.extend(
        [
            "",
            "## PlayState Offsets Observed In Message Candidates",
            "",
            "| Offset | Proposed role | Functions |",
            "| --- | --- | --- |",
        ]
    )
    for row in report["playstate_offset_summary"]:
        functions = ", ".join(f"`{entry}`" for entry in row["entries"])
        lines.append(f"| `{row['offset']}` | {row['role']} | {functions} |")

    lines.extend(
        [
            "",
            "## N64 Semantic Literal Matches",
            "",
            "| Entry | Current name | Matched literals |",
            "| --- | --- | --- |",
        ]
    )
    for row in report["functions"]:
        matches = row["n64_semantic_literal_matches"]
        if not matches:
            continue
        display = ", ".join(
            f"`{match['value']}` ({', '.join(match['names'][:3])})" for match in matches[:8]
        )
        lines.append(f"| `{row['entry']}` | `{row['name']}` | {display} |")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-json", type=Path, default=ANALYSIS / "message_context_usage.json")
    parser.add_argument("--out-md", type=Path, default=ANALYSIS / "message_context_usage.md")
    args = parser.parse_args()

    candidates = json.loads(MESSAGE_CANDIDATES.read_text(encoding="utf-8"))["candidates"]
    candidate_entries = {row["entry"].lower() for row in candidates}
    candidate_entries |= set(KNOWN_MESSAGE_ANCHORS)
    files = load_function_files()
    functions = load_csv_names(FUNCTIONS, "entry", "name")
    manual = load_csv_names(MANUAL_SYMBOLS, "entry", "new_name")
    semantic_values = load_message_semantic_values(MESSAGE_SEMANTICS)

    function_rows = [
        summarize_function(entry, files[entry], manual, functions, semantic_values)
        for entry in sorted(candidate_entries, key=lambda value: int(value, 16))
        if entry in files
    ]

    offset_to_entries: dict[str, list[str]] = defaultdict(list)
    offset_roles: dict[str, str] = {}
    for row in function_rows:
        for offset in row["playstate_offsets"]:
            offset_to_entries[offset["offset"]].append(row["entry"])
            offset_roles[offset["offset"]] = offset["role"]

    report = {
        "candidate_count": len(function_rows),
        "semantic_value_count": len(semantic_values),
        "functions_with_anchor_calls": sum(1 for row in function_rows if row["anchor_calls"]),
        "functions_with_message_ctx_offset": sum(
            1
            for row in function_rows
            if any(offset["offset"] == "0x32c0" for offset in row["playstate_offsets"])
        ),
        "playstate_offset_summary": [
            {"offset": offset, "role": offset_roles[offset], "entries": sorted(entries)}
            for offset, entries in sorted(offset_to_entries.items(), key=lambda item: int(item[0], 16))
        ],
        "functions": function_rows,
    }
    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    args.out_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    args.out_md.write_text(make_markdown(report), encoding="utf-8")
    print(f"wrote {args.out_json}")
    print(f"wrote {args.out_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
