#!/usr/bin/env python3
"""Find OOT3D functions that are likely part of the message subsystem."""

from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
DECOMPILED = ROOT / "ghidra_export" / "decompiled"
FUNCTIONS = ROOT / "ghidra_export" / "functions.csv"
MANUAL_SYMBOLS = ROOT / "symbols" / "manual_symbols.csv"

ENTRY_RE = re.compile(r"_(?P<entry>[0-9a-fA-F]{8})_")
CALL_RE = re.compile(r"\bFUN_(?P<entry>[0-9a-fA-F]{8})\s*\(")
MESSAGE_ID_LITERAL_RE = re.compile(r"\b0x(?:0?8[0-9a-fA-F]{2}|40[a-fA-F0-9]{2}|[0-7][0-9a-fA-F]{3})\b")
SIGNALS = {
    "message_resource_loader": re.compile(r"u_rom__message_|rom__message|\\.qm|\\.qbf"),
    "sys8_font_loader": re.compile(r"s_rom__message_sys8_qbf"),
    "message_context_offset": re.compile(r"0x32c0|\\+ -0x32c0"),
    "start_textbox_call": re.compile(r"(?:FUN_002d0320|oot3d_message_start_textbox)\s*\("),
    "message_update_call": re.compile(r"(?:FUN_0042cbcc|oot3d_message_context_update)\s*\("),
    "message_resource_call": re.compile(r"(?:FUN_00419e18|oot3d_load_message_resources)\s*\("),
}


def load_functions() -> dict[str, dict[str, str]]:
    with FUNCTIONS.open(newline="", encoding="utf-8") as handle:
        return {row["entry"].lower(): row for row in csv.DictReader(handle)}


def load_manual_symbols() -> dict[str, str]:
    if not MANUAL_SYMBOLS.is_file():
        return {}
    with MANUAL_SYMBOLS.open(newline="", encoding="utf-8") as handle:
        return {row["entry"].lower(): row["new_name"] for row in csv.DictReader(handle)}


def entry_from_path(path: Path) -> str | None:
    match = ENTRY_RE.search(path.name)
    return match.group("entry").lower() if match else None


def classify(signals: set[str], entry: str) -> tuple[str, str]:
    if entry == "002d0320":
        return "oot3d_message_start_textbox", "high"
    if entry == "0042cbcc":
        return "oot3d_message_context_update", "high"
    if entry == "0046b114":
        return "oot3d_init_sys_message_font", "high"
    if "message_resource_loader" in signals:
        return "oot3d_load_message_resources", "high"
    if "start_textbox_call" in signals and "message_context_offset" in signals:
        return "oot3d_message_textbox_caller", "medium"
    if "message_context_offset" in signals:
        return "oot3d_message_context_user", "medium"
    if "sys8_font_loader" in signals:
        return "oot3d_sys_message_font_user", "medium"
    return "oot3d_message_candidate", "low"


def make_markdown(report: dict[str, object]) -> str:
    lines = [
        "# Message Function Candidates",
        "",
        "Generated from `scripts/find_message_function_candidates.py`.",
        "",
        f"- Candidates: {report['candidate_count']}",
        f"- High confidence: {report['confidence_counts'].get('high', 0)}",
        f"- Medium confidence: {report['confidence_counts'].get('medium', 0)}",
        f"- Low confidence: {report['confidence_counts'].get('low', 0)}",
        "",
        "| Entry | Current name | Suggested role | Confidence | Signals | Text ID literals | File |",
        "| --- | --- | --- | --- | --- | --- | --- |",
    ]
    for row in report["candidates"]:
        literals = ", ".join(row["message_id_literals"][:8]) or "-"
        signals = ", ".join(row["signals"]) or "-"
        lines.append(
            f"| `{row['entry']}` | `{row['current_name']}` | `{row['suggested_role']}` | {row['confidence']} | {signals} | {literals} | `{row['file']}` |"
        )
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-json", type=Path, default=ANALYSIS / "message_function_candidates.json")
    parser.add_argument("--out-md", type=Path, default=ANALYSIS / "message_function_candidates.md")
    args = parser.parse_args()

    functions = load_functions()
    manual = load_manual_symbols()
    rows: list[dict[str, object]] = []
    for path in sorted(DECOMPILED.glob("*.c")):
        entry = entry_from_path(path)
        if entry is None:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        signals = {name for name, pattern in SIGNALS.items() if pattern.search(text)}
        if not signals and entry not in {"002d0320", "0042cbcc", "0046b114"}:
            continue
        role, confidence = classify(signals, entry)
        info = functions.get(entry, {})
        rows.append(
            {
                "entry": entry,
                "current_name": manual.get(entry, info.get("name", f"FUN_{entry}")),
                "suggested_role": role,
                "confidence": confidence,
                "signals": sorted(signals),
                "message_id_literals": sorted(set(MESSAGE_ID_LITERAL_RE.findall(text)), key=lambda value: int(value, 16)),
                "calls": sorted({match.group("entry").lower() for match in CALL_RE.finditer(text)}),
                "file": str(path.relative_to(ROOT)).replace("\\", "/"),
            }
        )

    order = {"high": 0, "medium": 1, "low": 2}
    rows.sort(key=lambda row: (order[row["confidence"]], int(row["entry"], 16)))
    confidence_counts = {
        confidence: sum(1 for row in rows if row["confidence"] == confidence)
        for confidence in ["high", "medium", "low"]
    }
    report = {
        "candidate_count": len(rows),
        "confidence_counts": confidence_counts,
        "candidates": rows,
    }
    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    args.out_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    args.out_md.write_text(make_markdown(report), encoding="utf-8")
    print(f"wrote {args.out_json}")
    print(f"wrote {args.out_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
