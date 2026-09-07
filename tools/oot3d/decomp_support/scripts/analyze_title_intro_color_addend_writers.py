#!/usr/bin/env python3
"""Classify native OOT3D PlayState color-addend writers for title intro fog.

This is a source-side audit. Emulator/PICA values are used only through the
existing title-intro fog candidate report to describe the validation gap; the
writer classifications come from decompiled code.bin exports.
"""

from __future__ import annotations

import argparse
import json
import re
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


DEFAULT_ROOT = Path("tools/oot3d/decomp_support")
DEFAULT_FOG_CANDIDATES = DEFAULT_ROOT / "analysis/title_intro_fog_runtime_candidates.json"
DEFAULT_OUTPUT = DEFAULT_ROOT / "analysis/title_intro_color_addend_writer_audit.json"

COLOR_ADDEND_OFFSETS = {
    "31fc": "ambient_r_or_overlay_r",
    "31fe": "ambient_g_or_overlay_g",
    "3200": "ambient_b_or_overlay_b",
    "3202": "light_r_or_overlay_r",
    "3204": "light_g_or_overlay_g",
    "3206": "light_b_or_overlay_b",
    "3208": "fog_r",
    "320a": "fog_g",
    "320c": "fog_b",
}
FOG_OFFSETS = {"3208", "320a", "320c"}
AMBIENT_LIGHT_OFFSETS = set(COLOR_ADDEND_OFFSETS) - FOG_OFFSETS

DECOMPILED_DIRS = [
    DEFAULT_ROOT / "ghidra_export/decompiled",
    DEFAULT_ROOT / "analysis/cutscene_camera_blob_helper_ghidra_export/decompiled",
    DEFAULT_ROOT / "analysis/scene_cutscene_context_ghidra_export/decompiled",
    DEFAULT_ROOT / "analysis/title_intro_player_action_consumer_ghidra_export/decompiled",
    DEFAULT_ROOT / "analysis/title_intro_actor_runtime_ghidra_export/decompiled",
    DEFAULT_ROOT / "analysis/shadow_fragop_handle_payload_ghidra_export/decompiled",
    DEFAULT_ROOT / "analysis/shadow_factory_context_helpers_ghidra_export/decompiled",
    DEFAULT_ROOT / "analysis/shadow_runtime_record_helpers_ghidra_export/decompiled",
]

OFFSET_RE = re.compile(r"0x(31fc|31fe|3200|3202|3204|3206|3208|320a|320c)\b", re.IGNORECASE)
FILE_RE = re.compile(r"(?P<rank>\d+)_(?P<address>[0-9a-fA-F]{8})_(?P<name>.+)\.c$")


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def extract_function_info(path: Path) -> dict[str, Any]:
    match = FILE_RE.match(path.name)
    if not match:
        return {"address": "", "name": path.stem}
    return {
        "address": f"0x{match.group('address').lower()}",
        "name": match.group("name"),
    }


def line_window(lines: list[str], line_index: int, before: int = 2, after: int = 2) -> str:
    start = max(0, line_index - before)
    end = min(len(lines), line_index + after + 1)
    return " ".join(line.strip() for line in lines[start:end] if line.strip())


def classify_writer(function_name: str, address: str, offsets: set[str], windows: list[str]) -> dict[str, Any]:
    lower_name = function_name.lower()
    joined = " ".join(windows).lower()
    writes_fog = bool(offsets & FOG_OFFSETS)
    writes_ambient_light = bool(offsets & AMBIENT_LIGHT_OFFSETS)

    classification = "unclassified_color_addend_writer"
    title_intro_relevance = "unknown"
    status = "needs_followup"
    reason = "Offset writes were found but no known semantic owner matched this audit."

    if "cutscene_processcommands" in lower_name or "002c5ba0" in address:
        classification = "cutscene_misc_ambient_light_addend_writer"
        title_intro_relevance = "title_intro_runtime_command_path_but_not_fog_addends"
        status = "excluded_for_fog"
        reason = (
            "Cutscene command handling writes ambient/light addends at 0x31fc/0x3204/0x3206 "
            "but this export does not write fog addends 0x3208..0x320c."
        )
    elif "00458a94" in address or "00458a94" in lower_name:
        classification = "gameplay_draw_global_flash_fog_darken_writer"
        title_intro_relevance = "runtime_gated_shape_mismatch_for_slot6_same_frame_delta"
        status = "not_sufficient"
        reason = (
            "Gameplay_Draw can call this path and it writes all fog addends, but the native shape "
            "subtracts 0x14 from R/G/B and clamps negative; it cannot explain a same-frame "
            "positive red and negative green/blue delta by itself."
        )
    elif "doorwarp" in lower_name or "0020cf6c" in address:
        classification = "door_warp_environment_color_addend_reset"
        title_intro_relevance = "excluded_not_title_intro_opening_scene"
        status = "excluded_for_title_intro"
        reason = "DoorWarp setup/reset writes all addend lanes for a transition actor, not the open-title route."
    elif "boss_va" in lower_name or "zapper" in lower_name or "0039858c" in address:
        classification = "boss_va_attack_effect_color_addend_writer"
        title_intro_relevance = "excluded_actor_not_in_open_title_intro"
        status = "excluded_for_title_intro"
        reason = "Boss Va attack effect writes all addend lanes; actor/effect route is not present in title intro."
    elif "enbombf" in lower_name or "enbom_update" in lower_name:
        classification = "bomb_actor_ambient_light_addend_writer"
        title_intro_relevance = "excluded_actor_not_in_open_title_intro"
        status = "excluded_for_fog"
        reason = "Bomb actor route writes ambient/light addends only, not fog addends."
    elif "003b5ae0" in address:
        classification = "environment_effect_light_addend_writer"
        title_intro_relevance = "not_fog_addends"
        status = "excluded_for_fog"
        reason = "This route writes light addends 0x3202..0x3206, but not 0x3208..0x320c."
    elif writes_fog:
        classification = "fog_addend_writer_candidate"
        title_intro_relevance = "candidate_requires_callsite_or_runtime_trace"
        status = "candidate"
        reason = "Writes at least one fog addend offset; needs callsite ownership before use."
    elif writes_ambient_light:
        classification = "ambient_light_addend_writer"
        title_intro_relevance = "not_fog_addends"
        status = "excluded_for_fog"
        reason = "Only ambient/light addend offsets are written."

    return {
        "classification": classification,
        "title_intro_relevance": title_intro_relevance,
        "status": status,
        "reason": reason,
        "writes_fog_addends": writes_fog,
        "writes_ambient_light_addends": writes_ambient_light,
    }


def collect_writers(search_dirs: list[Path]) -> list[dict[str, Any]]:
    by_function: dict[tuple[str, str], dict[str, Any]] = {}
    for root in search_dirs:
        if not root.is_dir():
            continue
        for path in root.rglob("*.c"):
            try:
                lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
            except OSError:
                continue
            offsets: set[str] = set()
            occurrences: list[dict[str, Any]] = []
            windows: list[str] = []
            for line_index, line in enumerate(lines):
                matches = [match.group(1).lower() for match in OFFSET_RE.finditer(line)]
                if not matches:
                    continue
                window = line_window(lines, line_index)
                windows.append(window)
                for offset in matches:
                    offsets.add(offset)
                    occurrences.append(
                        {
                            "line": line_index + 1,
                            "offset_hex": f"0x{offset}",
                            "offset_role": COLOR_ADDEND_OFFSETS[offset],
                            "text": line.strip(),
                            "window": window,
                        }
                    )
            if not occurrences:
                continue
            info = extract_function_info(path)
            relative = path.as_posix()
            key = (info["address"], info["name"])
            existing = by_function.get(key)
            if existing is None:
                classification = classify_writer(info["name"], info["address"], offsets, windows)
                by_function[key] = {
                    "source_paths": [relative],
                    "address": info["address"],
                    "function": info["name"],
                    "offsets": sorted(f"0x{offset}" for offset in offsets),
                    "offset_roles": [COLOR_ADDEND_OFFSETS[offset] for offset in sorted(offsets)],
                    "occurrence_count": len(occurrences),
                    "occurrences": occurrences[:24],
                    **classification,
                }
            else:
                if relative not in existing["source_paths"]:
                    existing["source_paths"].append(relative)
                known_offsets = {offset[2:] for offset in existing["offsets"]}
                known_offsets.update(offsets)
                existing["offsets"] = sorted(f"0x{offset}" for offset in known_offsets)
                existing["offset_roles"] = [COLOR_ADDEND_OFFSETS[offset] for offset in sorted(known_offsets)]
                existing["occurrence_count"] += len(occurrences)
                existing["occurrences"].extend(occurrences[:24])
    return sorted(
        by_function.values(),
        key=lambda row: (
            0 if row["writes_fog_addends"] else 1,
            row["status"],
            row["address"],
            row["function"],
        ),
    )


def extract_required_addend(fog_candidates_path: Path) -> dict[str, Any]:
    if not fog_candidates_path.is_file():
        return {}
    report = read_json(fog_candidates_path)
    active = report.get("active_transition_candidates_for_demo_angle") or []
    if not active:
        return {}
    candidate = active[0]
    return {
        "candidate_preaddend_rgb": candidate.get("candidate_preaddend_rgb"),
        "demo_final_fog_rgb": (report.get("demo_sample") or {}).get("final_fog_rgb"),
        "emulator_validation_rgb": (report.get("emulator_validation") or {}).get("dominant_fog_rgb"),
        "required_addend_if_same_frame": report.get("same_frame_required_addend_from_demo_preaddend_to_emulator_rgb")
        or candidate.get("required_addend_if_same_frame"),
        "interpretation": report.get("same_frame_addend_interpretation"),
        "same_frame_delta_status": (
            "not_explained_by_known_explicit_fog_addend_writers"
            if candidate.get("required_addend_if_same_frame") else "not_available"
        ),
    }


def build_report(args: argparse.Namespace) -> dict[str, Any]:
    writers = collect_writers(args.search_dir)
    fog_writers = [row for row in writers if row["writes_fog_addends"]]
    ambient_light_only = [
        row for row in writers if row["writes_ambient_light_addends"] and not row["writes_fog_addends"]
    ]
    candidates = [row for row in fog_writers if row["status"] == "candidate"]
    report = {
        "format": "oot3d_title_intro_color_addend_writer_audit_v1",
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "inputs": {
            "search_dirs": [path.as_posix() for path in args.search_dir],
            "fog_candidates": args.fog_candidates.as_posix(),
        },
        "summary": {
            "writer_function_count": len(writers),
            "fog_addend_writer_count": len(fog_writers),
            "ambient_light_only_writer_count": len(ambient_light_only),
            "unclassified_fog_candidate_count": len(candidates),
            "title_intro_direct_fog_addend_writer_proven": False,
            "runtime_trace_needed": True,
        },
        "title_intro_delta": extract_required_addend(args.fog_candidates),
        "writers": writers,
        "conclusion": {
            "status": "runtime_source_not_yet_proven",
            "next_step": (
                "Capture or decompile the title-intro playstate fields 0x31fc..0x320c and "
                "0x0a82..0x0a84 for the same slot-6 PICA frame; do not promote any excluded "
                "actor/effect writer into the engine."
            ),
            "runtime_data_rule": (
                "The emulator RGB remains validation evidence only. The engine may accept fog "
                "addends only from a proven OOT3D code.bin/asset/runtime producer."
            ),
        },
    }
    return report


def write_markdown(report: dict[str, Any], path: Path) -> None:
    lines = [
        "# Title Intro Color Addend Writer Audit",
        "",
        "## Summary",
        "",
    ]
    summary = report["summary"]
    lines.extend(
        [
            f"- Writer functions found: `{summary['writer_function_count']}`.",
            f"- Fog-addend writer functions: `{summary['fog_addend_writer_count']}`.",
            f"- Ambient/light-only writer functions: `{summary['ambient_light_only_writer_count']}`.",
            f"- Direct title-intro fog-addend writer proven: `{summary['title_intro_direct_fog_addend_writer_proven']}`.",
        ]
    )
    delta = report.get("title_intro_delta") or {}
    if delta:
        lines.extend(
            [
                "",
                "## Title Intro Delta",
                "",
                f"- Candidate pre-addend RGB: `{delta.get('candidate_preaddend_rgb')}`.",
                f"- Demo final fog RGB: `{delta.get('demo_final_fog_rgb')}`.",
                f"- Emulator validation RGB: `{delta.get('emulator_validation_rgb')}`.",
                f"- Required addend if same frame: `{delta.get('required_addend_if_same_frame')}`.",
                f"- Status: `{delta.get('same_frame_delta_status')}`.",
                f"- Interpretation: {delta.get('interpretation')}",
            ]
        )
    lines.extend(["", "## Writers", ""])
    lines.append("| address | function | offsets | status | title-intro relevance | reason |")
    lines.append("| --- | --- | --- | --- | --- | --- |")
    for row in report["writers"]:
        offsets = ", ".join(row["offsets"])
        reason = str(row["reason"]).replace("|", "\\|")
        lines.append(
            f"| `{row['address']}` | `{row['function']}` | `{offsets}` | "
            f"`{row['status']}` | `{row['title_intro_relevance']}` | {reason} |"
        )
    conclusion = report["conclusion"]
    lines.extend(
        [
            "",
            "## Conclusion",
            "",
            f"- Status: `{conclusion['status']}`.",
            f"- Next: {conclusion['next_step']}",
            f"- Rule: {conclusion['runtime_data_rule']}",
        ]
    )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fog-candidates", type=Path, default=DEFAULT_FOG_CANDIDATES)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--markdown-output", type=Path)
    parser.add_argument("--search-dir", type=Path, action="append", default=None)
    args = parser.parse_args()
    if args.search_dir is None:
        args.search_dir = DECOMPILED_DIRS
    if args.markdown_output is None:
        args.markdown_output = args.output.with_suffix(".md")
    return args


def main() -> int:
    args = parse_args()
    report = build_report(args)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    write_markdown(report, args.markdown_output)
    print(f"Wrote {args.output}")
    print(f"Wrote {args.markdown_output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
