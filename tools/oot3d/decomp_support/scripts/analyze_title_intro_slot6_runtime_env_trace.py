#!/usr/bin/env python3
"""Analyze title-intro slot 6 runtime environment writer traces.

This joins an Azahar memory-writer trace with the native OOT3D PlayState
environment offsets used by FUN_0045DD50 and Gameplay_Draw. It is validation
evidence only, not an engine data source.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from analyze_kokiri_slot5_runtime_light_trace import analyze_trace


RUNTIME_ENV_STATE_OFFSET = 0x3190


def md_triplet(value: list[int] | None) -> str:
    return "--" if value is None else ", ".join(str(item) for item in value)


def write_markdown(path: Path, report: dict[str, Any]) -> None:
    fields = report["field_values"]
    pica = report.get("pica_trace")
    lines = [
        "# OOT3D Title Intro Slot 6 Runtime Environment Trace",
        "",
        "This report is validation/backtrace evidence only. Engine runtime values must come from native OOT3D assets, code.bin-derived state, and decompiled behavior.",
        "",
        "## Inputs",
        "",
        f"- Writer trace: `{report['writer_trace']}`",
        f"- Watchlist: `{report['watchlist']}`",
        f"- Play base: `{report['play_base']}`",
        f"- Runtime environment state base: `{report['owner_base']}`",
        f"- Matching rows: `{report['rows_matching_watch_ranges']}` / `{report['row_count']}`",
        "",
        "## Native Field Reconstruction",
        "",
        "| Field | Raw/pre-addend | Addend i16 | Expected final | Observed final | Match |",
        "|---|---|---|---|---|---|",
    ]
    for name in ("ambient", "light0", "light1", "fog"):
        field = fields.get(name, {})
        final_key = "final_output" if "final_output" in field else "final_compact_payload"
        lines.append(
            "| `{}` | `{}` | `{}` | `{}` | `{}` | `{}` |".format(
                name,
                md_triplet(field.get("raw_preaddend")),
                md_triplet(field.get("addend_i16")),
                md_triplet(field.get("expected_final_from_raw_plus_addend")),
                md_triplet(field.get(final_key)),
                field.get("raw_plus_addend_matches_final"),
            )
        )
    lines.extend(
        [
            "",
            "## Title-Intro Fog Contract Check",
            "",
            "- Native producer formula: `clamp_u8(state+0xc1..0xc3 + state+0x78..0x7c)`.",
            "- Native consumer field: `play+0x0a82..0x0a84`, passed by `Gameplay_Draw` to `FUN_00464B2C`.",
            "- If the aligned PICA `GPUREG_FOG_COLOR` differs, treat it as evidence for the separate material-scalar path feeding `FUN_0047D6AC`, not as a value to copy into runtime.",
        ]
    )
    if pica:
        lines.extend(
            [
                "",
                "## PICA Fog Cross-Check",
                "",
                f"- PICA trace: `{pica.get('path')}`",
                f"- Top PICA fog RGB: `{md_triplet(pica.get('top_fog_color_rgb_u8'))}`",
                f"- Runtime final fog matches PICA: `{pica.get('matches_runtime_final_fog')}`",
            ]
        )
    lines.extend(["", "## Top Writers", ""])
    for label, entries in report.get("top_pc_lrs_by_watch_label", {}).items():
        lines.append(f"### {label}")
        lines.append("")
        lines.append("| PC | LR | Count |")
        lines.append("|---|---|---:|")
        for entry in entries[:8]:
            lines.append(f"| `{entry['pc']}` | `{entry['lr']}` | {entry['count']} |")
        lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--writer-trace", type=Path, required=True)
    parser.add_argument("--watchlist", type=Path, required=True)
    parser.add_argument("--pica-trace", type=Path)
    base_group = parser.add_mutually_exclusive_group()
    base_group.add_argument(
        "--play-base",
        "--global-context",
        dest="play_base",
        type=lambda text: int(text, 0),
        help="OOT3D PlayState/global-context base captured from the title-intro slot 6 run",
    )
    base_group.add_argument(
        "--owner-base",
        type=lambda text: int(text, 0),
        help="runtime environment state base, equal to PlayState + 0x3190",
    )
    parser.add_argument("--output-json", type=Path, required=True)
    parser.add_argument("--output-md", type=Path, required=True)
    args = parser.parse_args()

    owner_base = args.owner_base
    if owner_base is None and args.play_base is not None:
        owner_base = args.play_base + RUNTIME_ENV_STATE_OFFSET

    report = analyze_trace(args.writer_trace, args.watchlist, args.pica_trace, owner_base)
    report["format"] = "oot3d_title_intro_slot6_runtime_env_trace_v1"
    report["policy"] = "validation_only_not_runtime_replacement_data"
    report["scene"] = "title_intro_opening_hyrule_field_slot6"

    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(report, indent=2) + "\n", encoding="ascii")
    write_markdown(args.output_md, report)
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
