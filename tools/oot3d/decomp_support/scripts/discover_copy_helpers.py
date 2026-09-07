#!/usr/bin/env python3
"""Discover simple copy helper functions in the Ghidra pseudocode export."""

from __future__ import annotations

import csv
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXPORT = ROOT / "ghidra_export"
DECOMPILED = EXPORT / "decompiled"
ANALYSIS = ROOT / "analysis"

FUNC_RE = re.compile(
    r"void\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)\((?P<type>undefined[124])\s+\*param_1,"
    r"\s*(?P=type)\s+\*param_2\)"
)
ASSIGN_ZERO_RE = re.compile(r"\*param_1\s*=\s*\*param_2;")
ASSIGN_INDEX_RE = re.compile(r"param_1\[(?P<dst>(?:0x)?[0-9a-fA-F]+)\]\s*=\s*(?:uVar\d+|param_2\[(?P<src>(?:0x)?[0-9a-fA-F]+)\]);")
LOAD_RE = re.compile(r"uVar\d+\s*=\s*param_2\[(?P<src>(?:0x)?[0-9a-fA-F]+)\];")
SELF_CHECK_RE = re.compile(r"if\s*\(param_2\s*==\s*param_1\)\s*\{\s*return;?\s*\}", re.S)
FUNC_FILE_RE = re.compile(r"^(?P<index>\d+)_(?P<entry>[0-9a-fA-F]{8})_(?P<name>.+)\.c$")
EXTRA_PARAM1_WRITE_RE = re.compile(r"\*\([^)]*\)\s*\(?(?:\(int\))?param_1\s*\+")
CALL_RE = re.compile(r"\b(?:FUN|oot3d)_[A-Za-z0-9_]+\s*\(")


def parse_index(value: str) -> int:
    return int(value, 16) if value.lower().startswith("0x") else int(value)


def load_function_names() -> dict[str, str]:
    with (EXPORT / "functions.csv").open(newline="", encoding="utf-8") as f:
        return {row["entry"].lower(): row["name"] for row in csv.DictReader(f)}


def analyze_file(path: Path, exported_names: dict[str, str]) -> dict[str, object] | None:
    match = FUNC_FILE_RE.match(path.name)
    if not match:
        return None
    entry = match.group("entry").lower()
    text = path.read_text(encoding="utf-8", errors="replace")
    func = FUNC_RE.search(text)
    if not func:
        return None
    body = text.split("{", 1)[1] if "{" in text else text

    assignments: list[tuple[int, int]] = []
    if ASSIGN_ZERO_RE.search(body):
        assignments.append((0, 0))

    loads = [parse_index(src) for src in LOAD_RE.findall(body)]
    for assign in ASSIGN_INDEX_RE.finditer(body):
        dst = parse_index(assign.group("dst"))
        src = parse_index(assign.group("src")) if assign.group("src") is not None else None
        if src is None:
            if loads:
                src = loads.pop(0)
            else:
                continue
        assignments.append((dst, src))

    if len(assignments) < 2:
        return None

    assignments = sorted(set(assignments))
    dst_indices = [dst for dst, _ in assignments]
    src_indices = [src for _, src in assignments]
    sequential_dst = dst_indices == list(range(len(dst_indices)))
    sequential_src = src_indices == list(range(len(src_indices)))
    same_indices = dst_indices == src_indices

    extra_param1_writes = len(EXTRA_PARAM1_WRITE_RE.findall(body))
    calls = CALL_RE.findall(body)
    if extra_param1_writes or calls:
        category = "copy-with-extra-effects"
    elif not sequential_dst:
        category = "noncontiguous-destination-copy"
    elif same_indices and sequential_src:
        category = "sequential-copy"
    else:
        category = "packed-or-sparse-copy"

    width = {"undefined1": 1, "undefined2": 2, "undefined4": 4}[func.group("type")]
    return {
        "entry": entry,
        "name": exported_names.get(entry, func.group("name")),
        "type": func.group("type"),
        "element_width": width,
        "elements": len(assignments),
        "bytes": width * len(assignments),
        "category": category,
        "self_check": bool(SELF_CHECK_RE.search(body)),
        "extra_param1_writes": extra_param1_writes,
        "calls": len(calls),
        "assignments": [{"dst": dst, "src": src} for dst, src in assignments],
        "file": str(path.relative_to(ROOT)).replace("\\", "/"),
    }


def write_markdown(candidates: list[dict[str, object]]) -> None:
    lines = [
        "# Copy Helper Candidates",
        "",
        "Generated from `scripts/discover_copy_helpers.py`.",
        "",
        f"- Candidates: {len(candidates)}",
        f"- Sequential copies: {sum(1 for c in candidates if c['category'] == 'sequential-copy')}",
        f"- Packed/sparse copies: {sum(1 for c in candidates if c['category'] == 'packed-or-sparse-copy')}",
        f"- Copies with extra effects: {sum(1 for c in candidates if c['category'] == 'copy-with-extra-effects')}",
        "",
        "| Entry | Name | Category | Type | Elements | Bytes | Self-check | Extra writes | Calls | File |",
        "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |",
    ]
    for item in candidates:
        lines.append(
            "| {entry} | {name} | {category} | {type} | {elements} | {bytes} | {self_check} | {extra_param1_writes} | {calls} | {file} |".format(
                **item
            )
        )
    lines.append("")
    (ANALYSIS / "copy_helper_candidates.md").write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    ANALYSIS.mkdir(exist_ok=True)
    exported_names = load_function_names()
    candidates = []
    for path in DECOMPILED.glob("*.c"):
        item = analyze_file(path, exported_names)
        if item is not None:
            candidates.append(item)

    candidates.sort(key=lambda item: (str(item["category"]), int(item["element_width"]), int(item["elements"]), str(item["entry"])))
    (ANALYSIS / "copy_helper_candidates.json").write_text(json.dumps(candidates, indent=2), encoding="utf-8")
    write_markdown(candidates)

    summary = {
        "copy_helper_candidates": len(candidates),
        "sequential_copy": sum(1 for c in candidates if c["category"] == "sequential-copy"),
        "packed_or_sparse_copy": sum(1 for c in candidates if c["category"] == "packed-or-sparse-copy"),
        "copy_with_extra_effects": sum(1 for c in candidates if c["category"] == "copy-with-extra-effects"),
    }
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
