#!/usr/bin/env python3
"""Build navigation reports from the Ghidra export.

The generated C-like files are only an automated baseline. These reports make
the baseline easier to triage by surfacing large functions, call fan-out, and
simple libc-like candidates that should be renamed early.
"""

from __future__ import annotations

import csv
import json
import re
from collections import Counter, defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXPORT = ROOT / "ghidra_export"
DECOMPILED = EXPORT / "decompiled"
OUT = ROOT / "analysis"

FUNC_FILE_RE = re.compile(r"^(?P<index>\d+)_(?P<entry>[0-9a-fA-F]{8})_(?P<name>.+)\.c$")
IDENT_CALL_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")


def parse_hex(value: str) -> int:
    return int(value.strip().lower().removeprefix("0x"), 16)


def load_functions() -> dict[str, dict[str, object]]:
    functions: dict[str, dict[str, object]] = {}
    with (EXPORT / "functions.csv").open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            start = parse_hex(row["body_min"])
            end = parse_hex(row["body_max"])
            name = row["name"]
            entry = row["entry"]
            functions[entry] = {
                "name": name,
                "entry": entry,
                "body_min": row["body_min"],
                "body_max": row["body_max"],
                "address_span": end - start + 1,
                "signature": row["signature"],
            }
    return functions


def scan_decompiled(functions: dict[str, dict[str, object]]) -> None:
    reverse_calls: dict[str, set[str]] = defaultdict(set)
    name_to_entries: dict[str, list[str]] = defaultdict(list)
    for entry, data in functions.items():
        name_to_entries[str(data["name"])].append(entry)
    known_names = set(name_to_entries)

    for path in DECOMPILED.glob("*.c"):
        match = FUNC_FILE_RE.match(path.name)
        if not match:
            continue
        caller_entry = match.group("entry")
        caller_name = match.group("name")
        text = path.read_text(encoding="utf-8", errors="replace")
        calls = sorted({name for name in IDENT_CALL_RE.findall(text) if name in known_names and name != caller_name})
        functions.setdefault(caller_entry, {"entry": caller_entry, "name": caller_name})["file"] = str(path.relative_to(ROOT)).replace("\\", "/")
        functions[caller_entry]["line_count"] = text.count("\n") + 1
        functions[caller_entry]["call_count"] = len(calls)
        functions[caller_entry]["calls"] = calls
        for callee in calls:
            reverse_calls[callee].add(caller_entry)

    for callee_name, callers in reverse_calls.items():
        for entry in name_to_entries.get(callee_name, []):
            functions[entry]["caller_count"] = len(callers)
            functions[entry]["callers"] = sorted(callers)

    for data in functions.values():
        data.setdefault("line_count", 0)
        data.setdefault("call_count", 0)
        data.setdefault("calls", [])
        data.setdefault("caller_count", 0)
        data.setdefault("callers", [])


def classify_libc_candidates(functions: dict[str, dict[str, object]]) -> list[dict[str, object]]:
    candidates: list[dict[str, object]] = []
    hints = [
        ("strcmp/strncmp-like", ["return uVar5 - uVar4", "param_3", "(byte)*param_1", "(byte)*param_2"]),
        ("memclear/memset-zero-like", ["*puVar2 = 0", "puVar2 < puVar1"]),
        ("copy/struct-copy-like", ["*param_1 = *param_2", "param_1[", "param_2["]),
    ]
    for data in functions.values():
        file_value = data.get("file")
        if not file_value:
            continue
        text = (ROOT / str(file_value)).read_text(encoding="utf-8", errors="replace")
        for label, needles in hints:
            if all(needle in text for needle in needles):
                candidates.append(
                    {
                        "candidate": label,
                        "name": data.get("name"),
                        "entry": data.get("entry"),
                        "address_span": data.get("address_span"),
                        "file": file_value,
                    }
                )
                break
    return candidates


def write_markdown(functions: dict[str, dict[str, object]], libc_candidates: list[dict[str, object]]) -> None:
    largest = sorted(functions.values(), key=lambda item: int(item.get("line_count", 0)), reverse=True)[:100]
    fanout = sorted(functions.values(), key=lambda item: int(item.get("call_count", 0)), reverse=True)[:100]
    fanin = sorted(functions.values(), key=lambda item: int(item.get("caller_count", 0)), reverse=True)[:100]

    def table(rows: list[dict[str, object]], columns: list[tuple[str, str]]) -> list[str]:
        out = ["| " + " | ".join(title for title, _ in columns) + " |"]
        out.append("| " + " | ".join("---" for _ in columns) + " |")
        for row in rows:
            out.append("| " + " | ".join(str(row.get(key, "")) for _, key in columns) + " |")
        return out

    lines: list[str] = [
        "# Export analysis",
        "",
        f"- Functions: {len(functions)}",
        f"- Decompiled files scanned: {sum(1 for f in functions.values() if f.get('file'))}",
        f"- Calls detected in pseudocode: {sum(int(f.get('call_count', 0)) for f in functions.values())}",
        f"- Libc-like candidates: {len(libc_candidates)}",
        "",
        "## Likely libc/helper candidates",
        "",
    ]
    lines.extend(table(libc_candidates[:50], [("Candidate", "candidate"), ("Name", "name"), ("Entry", "entry"), ("Address span", "address_span"), ("File", "file")]))
    lines.extend(["", "## Longest decompiled functions", ""])
    lines.extend(table(largest, [("Name", "name"), ("Entry", "entry"), ("Lines", "line_count"), ("Calls", "call_count"), ("Address span", "address_span"), ("File", "file")]))
    lines.extend(["", "## Highest call fan-out", ""])
    lines.extend(table(fanout, [("Name", "name"), ("Entry", "entry"), ("Calls", "call_count"), ("Lines", "line_count"), ("File", "file")]))
    lines.extend(["", "## Highest call fan-in", ""])
    lines.extend(table(fanin, [("Name", "name"), ("Entry", "entry"), ("Callers", "caller_count"), ("Lines", "line_count"), ("File", "file")]))
    lines.append("")
    (OUT / "export_analysis.md").write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    OUT.mkdir(exist_ok=True)
    functions = load_functions()
    scan_decompiled(functions)
    libc_candidates = classify_libc_candidates(functions)

    serializable = sorted(functions.values(), key=lambda item: str(item.get("entry", "")))
    (OUT / "functions_enriched.json").write_text(json.dumps(serializable, indent=2), encoding="utf-8")
    (OUT / "libc_candidates.json").write_text(json.dumps(libc_candidates, indent=2), encoding="utf-8")
    write_markdown(functions, libc_candidates)

    summary = {
        "functions": len(functions),
        "decompiled_files_scanned": sum(1 for f in functions.values() if f.get("file")),
        "calls_detected": sum(int(f.get("call_count", 0)) for f in functions.values()),
        "libc_candidates": len(libc_candidates),
    }
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
