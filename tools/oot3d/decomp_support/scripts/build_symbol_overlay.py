#!/usr/bin/env python3
"""Report manual-symbol overlay state versus the current Ghidra export."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path

from symbol_overlay import MANUAL_SYMBOLS, load_manual_symbols


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
FUNCTIONS_CSV = ROOT / "ghidra_export" / "functions.csv"


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def load_export_names(path: Path) -> dict[str, str]:
    with path.open(newline="", encoding="utf-8") as handle:
        return {row["entry"].lower(): row["name"] for row in csv.DictReader(handle)}


def make_markdown(report: dict[str, object]) -> str:
    rows = list(report["rows"])
    lines = [
        "# Manual Symbol Overlay",
        "",
        "Generated from `scripts/build_symbol_overlay.py`.",
        "",
        f"- Manual symbols: {report['manual_symbol_count']}",
        f"- Already applied in Ghidra export: {report['applied_count']}",
        f"- Pending Ghidra apply/export: {report['pending_count']}",
        "",
        "## Pending Ghidra Apply/Export",
        "",
        "| Entry | Export name | Overlay name | Source | Notes |",
        "| --- | --- | --- | --- | --- |",
    ]
    for row in rows:
        if not row["pending_ghidra_export"]:
            continue
        lines.append(
            f"| `{row['entry']}` | `{row['export_name']}` | `{row['overlay_name']}` | "
            f"`{row['source_file']}` | {row['notes']} |"
        )
    lines.extend(
        [
            "",
            "## All Manual Symbols",
            "",
            "| Entry | Export name | Overlay name | State | Source |",
            "| --- | --- | --- | --- | --- |",
        ]
    )
    for row in rows:
        state = "pending" if row["pending_ghidra_export"] else "applied"
        lines.append(
            f"| `{row['entry']}` | `{row['export_name']}` | `{row['overlay_name']}` | "
            f"`{state}` | `{row['source_file']}` |"
        )
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--functions", type=Path, default=FUNCTIONS_CSV)
    parser.add_argument("--manual-symbols", type=Path, default=MANUAL_SYMBOLS)
    parser.add_argument("--out-json", type=Path, default=ANALYSIS / "symbol_overlay.json")
    parser.add_argument("--out-md", type=Path, default=ANALYSIS / "symbol_overlay.md")
    args = parser.parse_args()

    export_names = load_export_names(args.functions)
    rows = []
    for symbol in load_manual_symbols(args.manual_symbols):
        export_name = export_names.get(symbol.entry, "")
        rows.append(
            {
                "entry": symbol.entry,
                "export_name": export_name,
                "old_name": symbol.old_name,
                "overlay_name": symbol.new_name,
                "confidence": symbol.confidence,
                "source_file": symbol.source_file,
                "notes": symbol.notes,
                "pending_ghidra_export": export_name != symbol.new_name,
            }
        )
    rows.sort(key=lambda row: int(str(row["entry"]), 16))
    pending = [row for row in rows if row["pending_ghidra_export"]]
    report = {
        "functions_csv": rel(args.functions),
        "manual_symbols": rel(args.manual_symbols),
        "manual_symbol_count": len(rows),
        "applied_count": len(rows) - len(pending),
        "pending_count": len(pending),
        "rows": rows,
    }
    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    args.out_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    args.out_md.write_text(make_markdown(report), encoding="utf-8")
    print(json.dumps({key: report[key] for key in ("manual_symbol_count", "applied_count", "pending_count")}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
