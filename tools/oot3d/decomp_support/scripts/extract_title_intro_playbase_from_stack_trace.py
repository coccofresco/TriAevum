#!/usr/bin/env python3
"""Extract the title-intro PlayState base from a stack writer trace.

Azahar's writer trace records CPU registers for any watched memory write. A
temporary stack watch around the title-intro frame can therefore recover the
`Gameplay_Draw` register state: in the native disassembly, `r4` is copied from
the function argument and remains the PlayState/global-context pointer through
the environment/fog setup block.
"""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path
from typing import Any


GAMEPLAY_DRAW_START = 0x002E25F0
GAMEPLAY_DRAW_FOG_BLOCK_END = 0x002E26F8
GAMEPLAY_DRAW_END = 0x002E2DDC


def parse_hex(value: str | None) -> int | None:
    if value is None or value == "":
        return None
    try:
        return int(value, 16)
    except ValueError:
        return None


def fmt_u32(value: int | None) -> str | None:
    if value is None:
        return None
    return f"0x{value & 0xFFFFFFFF:08X}"


def plausible_play_base(value: int | None) -> bool:
    if value is None:
        return False
    return 0x08000000 <= value < 0x18000000


def analyze_trace(path: Path) -> dict[str, Any]:
    total_rows = 0
    gameplay_rows = 0
    fog_block_rows = 0
    r4_counter: Counter[int] = Counter()
    fog_r4_counter: Counter[int] = Counter()
    sample_rows: list[dict[str, Any]] = []

    with path.open("r", newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            total_rows += 1
            pc = parse_hex(row.get("pc"))
            r4 = parse_hex(row.get("r4"))
            if pc is None or not (GAMEPLAY_DRAW_START <= pc < GAMEPLAY_DRAW_END):
                continue
            gameplay_rows += 1
            if plausible_play_base(r4):
                r4_counter[r4] += 1
            if GAMEPLAY_DRAW_START <= pc <= GAMEPLAY_DRAW_FOG_BLOCK_END:
                fog_block_rows += 1
                if plausible_play_base(r4):
                    fog_r4_counter[r4] += 1
            if len(sample_rows) < 24:
                sample_rows.append(
                    {
                        "serial": row.get("serial"),
                        "pc": row.get("pc"),
                        "lr": row.get("lr"),
                        "sp": row.get("sp"),
                        "r0": row.get("r0"),
                        "r4": row.get("r4"),
                        "address": row.get("address"),
                        "label": row.get("label"),
                    }
                )

    selected: int | None = None
    source = "missing"
    if fog_r4_counter:
        selected = fog_r4_counter.most_common(1)[0][0]
        source = "gameplay_draw_fog_setup_block_r4"
    elif r4_counter:
        selected = r4_counter.most_common(1)[0][0]
        source = "gameplay_draw_body_r4"

    return {
        "format": "oot3d_title_intro_stack_playbase_probe_v1",
        "policy": "validation_only_not_runtime_replacement_data",
        "writer_trace": str(path),
        "gameplay_draw_range": {
            "start": fmt_u32(GAMEPLAY_DRAW_START),
            "fog_block_end": fmt_u32(GAMEPLAY_DRAW_FOG_BLOCK_END),
            "end": fmt_u32(GAMEPLAY_DRAW_END),
        },
        "row_count": total_rows,
        "gameplay_draw_rows": gameplay_rows,
        "gameplay_draw_fog_block_rows": fog_block_rows,
        "selected_play_base": fmt_u32(selected),
        "selected_owner_base": fmt_u32(selected + 0x3190 if selected is not None else None),
        "selected_source": source,
        "top_r4_in_fog_block": [
            {"value": fmt_u32(value), "count": count}
            for value, count in fog_r4_counter.most_common(12)
        ],
        "top_r4_in_gameplay_draw": [
            {"value": fmt_u32(value), "count": count}
            for value, count in r4_counter.most_common(12)
        ],
        "sample_rows": sample_rows,
    }


def write_markdown(path: Path, report: dict[str, Any]) -> None:
    lines = [
        "# OOT3D Title Intro PlayState Stack Probe",
        "",
        "This report is validation evidence only. The extracted PlayState base is used to aim runtime field watches; it is not engine replacement data.",
        "",
        f"- Writer trace: `{report['writer_trace']}`",
        f"- Selected PlayState base: `{report['selected_play_base']}`",
        f"- Selected runtime env base: `{report['selected_owner_base']}`",
        f"- Source: `{report['selected_source']}`",
        f"- Gameplay_Draw rows: `{report['gameplay_draw_rows']}` / `{report['row_count']}`",
        f"- Fog setup block rows: `{report['gameplay_draw_fog_block_rows']}`",
        "",
        "## Top r4 Values In Fog Setup Block",
        "",
        "| r4 | Count |",
        "|---|---:|",
    ]
    for entry in report["top_r4_in_fog_block"]:
        lines.append(f"| `{entry['value']}` | {entry['count']} |")
    lines.extend(["", "## Top r4 Values In Gameplay_Draw", "", "| r4 | Count |", "|---|---:|"])
    for entry in report["top_r4_in_gameplay_draw"]:
        lines.append(f"| `{entry['value']}` | {entry['count']} |")
    lines.extend(["", "## Sample Rows", "", "| Serial | PC | LR | SP | r0 | r4 | Address |", "|---|---|---|---|---|---|---|"])
    for row in report["sample_rows"]:
        lines.append(
            "| `{}` | `{}` | `{}` | `{}` | `{}` | `{}` | `{}` |".format(
                row.get("serial"),
                row.get("pc"),
                row.get("lr"),
                row.get("sp"),
                row.get("r0"),
                row.get("r4"),
                row.get("address"),
            )
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--writer-trace", type=Path, required=True)
    parser.add_argument("--output-json", type=Path, required=True)
    parser.add_argument("--output-md", type=Path, required=True)
    args = parser.parse_args()

    report = analyze_trace(args.writer_trace)
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(report, indent=2) + "\n", encoding="ascii")
    write_markdown(args.output_md, report)
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
