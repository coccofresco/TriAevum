#!/usr/bin/env python3
"""Compare indexed OOT3D QM text IDs with audited N64 OoT text IDs."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
DEFAULT_QM_INDEX = ANALYSIS / "qm_message_index.json"
DEFAULT_N64_AUDIT = ANALYSIS / "n64_oot_reuse_audit.json"
DEFAULT_Z_MESSAGE = ROOT.parent / "external" / "oot" / "src" / "code" / "z_message.c"

HEX_ID_RE = re.compile(r"\b0x[0-9A-Fa-f]{1,4}\b")


def normalize_id(value: str) -> str:
    return f"0x{int(value, 16):04x}"


def load_qm_ids(path: Path) -> set[str]:
    report = json.loads(path.read_text(encoding="utf-8"))
    return {normalize_id(value) for value in report.get("text_ids", [])}


def load_n64_declared_ids(path: Path) -> set[str]:
    report = json.loads(path.read_text(encoding="utf-8"))
    full_ids = report.get("n64_oot", {}).get("message_ids", [])
    if full_ids:
        return {normalize_id(value) for value in full_ids}

    ids: set[str] = set()
    for source in report.get("n64_oot", {}).get("text_sources", []):
        for value in source.get("message_id_sample", []):
            ids.add(normalize_id(value))
    return ids


def load_n64_code_literal_ids(path: Path) -> set[str]:
    if not path.is_file():
        return set()
    ids = {normalize_id(match.group(0)) for match in HEX_ID_RE.finditer(path.read_text(encoding="utf-8", errors="replace"))}
    # Keep plausible message IDs and drop tiny constants that are generally state
    # values, colors, masks, or table dimensions in z_message.c.
    return {value for value in ids if int(value, 16) >= 0x0100}


def make_markdown(report: dict[str, object]) -> str:
    lines = [
        "# QM to N64 Text ID Comparison",
        "",
        "Generated from `scripts/compare_qm_n64_ids.py`.",
        "",
        f"- OOT3D QM IDs: {report['qm_id_count']}",
        f"- N64 declared text IDs currently available: {report['n64_declared_id_count']}",
        f"- N64 z_message.c code literal IDs: {report['n64_code_literal_id_count']}",
        f"- Declared ID overlap: {report['declared_overlap_count']}",
        f"- Code literal ID overlap: {report['code_literal_overlap_count']}",
        f"- OOT3D-only QM IDs: {report['qm_only_count']}",
        f"- N64-only declared IDs: {report['n64_only_count']}",
        "",
        "## Interpretation",
        "",
        "- Declared overlap is now based on extracted N64 text headers generated from the supplied `ntsc-1.2` baserom.",
        "- Overlapping IDs are strong reuse anchors for message naming, control-code mapping, and payload comparison.",
        "- Code literal overlap is a naming/triage signal only; these literals are not proof of identical text payloads.",
        "- The next strong comparison step is decoding OOT3D QM payload bytes, then comparing by text ID and control-code stream.",
        "",
        "## Overlap Samples",
        "",
        "| Set | Sample IDs |",
        "| --- | --- |",
        f"| Declared N64 text headers | {', '.join(report['declared_overlap_sample']) or '-'} |",
        f"| N64 z_message.c literals | {', '.join(report['code_literal_overlap_sample']) or '-'} |",
        f"| OOT3D-only QM IDs | {', '.join(report['qm_only_sample']) or '-'} |",
        f"| N64-only declared IDs | {', '.join(report['n64_only_sample']) or '-'} |",
        "",
        "## QM ID Range Sample",
        "",
        f"- First IDs: {', '.join(report['qm_first_ids'])}",
        f"- Last IDs: {', '.join(report['qm_last_ids'])}",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qm-index", type=Path, default=DEFAULT_QM_INDEX)
    parser.add_argument("--n64-audit", type=Path, default=DEFAULT_N64_AUDIT)
    parser.add_argument("--z-message", type=Path, default=DEFAULT_Z_MESSAGE)
    parser.add_argument("--out-json", type=Path, default=ANALYSIS / "qm_n64_text_id_comparison.json")
    parser.add_argument("--out-md", type=Path, default=ANALYSIS / "qm_n64_text_id_comparison.md")
    args = parser.parse_args()

    qm_ids = load_qm_ids(args.qm_index)
    declared_ids = load_n64_declared_ids(args.n64_audit)
    code_literal_ids = load_n64_code_literal_ids(args.z_message)
    declared_overlap = sorted(qm_ids & declared_ids, key=lambda value: int(value, 16))
    code_literal_overlap = sorted(qm_ids & code_literal_ids, key=lambda value: int(value, 16))
    qm_only = sorted(qm_ids - declared_ids, key=lambda value: int(value, 16))
    n64_only = sorted(declared_ids - qm_ids, key=lambda value: int(value, 16))
    ordered_qm_ids = sorted(qm_ids, key=lambda value: int(value, 16))

    report = {
        "qm_id_count": len(qm_ids),
        "n64_declared_id_count": len(declared_ids),
        "n64_code_literal_id_count": len(code_literal_ids),
        "declared_overlap_count": len(declared_overlap),
        "code_literal_overlap_count": len(code_literal_overlap),
        "qm_only_count": len(qm_only),
        "n64_only_count": len(n64_only),
        "declared_overlap_sample": declared_overlap[:32],
        "code_literal_overlap_sample": code_literal_overlap[:64],
        "qm_only_sample": qm_only[:32],
        "n64_only_sample": n64_only[:32],
        "qm_first_ids": ordered_qm_ids[:24],
        "qm_last_ids": ordered_qm_ids[-24:],
    }

    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    args.out_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    args.out_md.write_text(make_markdown(report), encoding="utf-8")
    print(f"wrote {args.out_json}")
    print(f"wrote {args.out_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
