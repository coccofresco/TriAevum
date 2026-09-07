#!/usr/bin/env python3
"""Audit native OOT3D title-intro fog candidates against a validation dump.

The emulator dump is validation evidence only. Candidate colors are derived
from extracted OOT3D scene light records and the code.bin 0x00531EFC transition
table used by FUN_0045DD50.
"""

from __future__ import annotations

import argparse
import json
import struct
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


DEFAULT_SCENE_LIGHT_TABLE = Path("tools/oot3d/decomp_support/analysis/scene_light_source_table.json")
DEFAULT_CODE_BIN = Path("E:/ppssppvr/oot3d_decomp/work/extract/exefs/code.bin")
DEFAULT_DEMO_STATS = Path("build-codex/title_intro_frame600_kankyo_fog_backend_stats.json")
DEFAULT_EMULATOR_SUMMARY = Path(
    "captures/azahar_pica/title_intro_slot6_kankyo_focused_20260707_082324/"
    "derived/title_intro_slot6_pica_dump_summary.json"
)
DEFAULT_OUTPUT = Path("tools/oot3d/decomp_support/analysis/title_intro_fog_runtime_candidates.json")

CODE_BASE = 0x00100000
TRANSITION_TABLE_ADDRESS = 0x00531EFC
TRANSITION_MODE_COUNT = 5
TRANSITION_MODE_STRIDE = 0x36
TRANSITION_ENTRY_COUNT = 9
TRANSITION_ENTRY_SIZE = 6


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def get_path(data: dict[str, Any], dotted: str, default: Any = None) -> Any:
    current: Any = data
    for part in dotted.split("."):
        if not isinstance(current, dict) or part not in current:
            return default
        current = current[part]
    return current


def rgb_from_mapping(value: Any) -> list[int] | None:
    if not isinstance(value, dict):
        return None
    try:
        return [int(value["r"]), int(value["g"]), int(value["b"])]
    except (KeyError, TypeError, ValueError):
        return None


def rgb_key_to_list(key: str | None) -> list[int] | None:
    if not key:
        return None
    parts = key.split(",")
    if len(parts) != 3:
        return None
    try:
        return [int(part) for part in parts]
    except ValueError:
        return None


def dominant_fog_color(emulator_summary: dict[str, Any]) -> list[int] | None:
    colors = get_path(emulator_summary, "frame.draw_summary.fog_color_counts", {}) or {}
    if not isinstance(colors, dict) or not colors:
        return None
    key, _count = max(colors.items(), key=lambda item: int(item[1] or 0))
    return rgb_key_to_list(str(key))


def native_lerp_u8(a: int, b: int, weight: float) -> int:
    return max(0, min(255, int(round(float(a) + (float(b) - float(a)) * weight))))


def transition_weight(start: int, end: int, active_angle: int) -> float:
    span = end - start
    if span <= 0:
        return 0.0
    weight = 1.0 - float(end - active_angle) / float(span)
    return max(0.0, min(1.0, weight))


def parse_transition_table(code_bin: Path) -> list[dict[str, Any]]:
    data = code_bin.read_bytes()
    base_offset = TRANSITION_TABLE_ADDRESS - CODE_BASE
    modes: list[dict[str, Any]] = []
    for mode_index in range(TRANSITION_MODE_COUNT):
        entries = []
        for entry_index in range(TRANSITION_ENTRY_COUNT):
            offset = base_offset + mode_index * TRANSITION_MODE_STRIDE + entry_index * TRANSITION_ENTRY_SIZE
            start, end, from_index, to_index = struct.unpack_from("<HHBB", data, offset)
            entries.append(
                {
                    "entry_index": entry_index,
                    "start_angle": start,
                    "end_angle": end,
                    "from_light_setting_index": from_index,
                    "to_light_setting_index": to_index,
                }
            )
        modes.append({"mode_index": mode_index, "entries": entries})
    return modes


def scene_records(scene_light_table: Path, scene_path: str, setup_index: int) -> list[dict[str, Any]]:
    table = load_json(scene_light_table)
    rows = table.get("rows") or []
    records = [
        row
        for row in rows
        if row.get("scene_path") == scene_path and int(row.get("setup_index", -1)) == setup_index
    ]
    return sorted(records, key=lambda row: int(row.get("record_index", -1)))


def all_scene_records(scene_light_table: Path, scene_path: str) -> list[dict[str, Any]]:
    table = load_json(scene_light_table)
    rows = table.get("rows") or []
    return [row for row in rows if row.get("scene_path") == scene_path]


def record_fog_rgb(record: dict[str, Any]) -> list[int]:
    return [
        int(record["fog_or_environment_r"]),
        int(record["fog_or_environment_g"]),
        int(record["fog_or_environment_b"]),
    ]


def rgb_diff(a: list[int], b: list[int]) -> int:
    return sum(abs(int(a[i]) - int(b[i])) for i in range(3))


def transition_candidates(
    modes: list[dict[str, Any]],
    records: list[dict[str, Any]],
    active_angle: int,
    target_rgb: list[int] | None,
) -> list[dict[str, Any]]:
    by_index = {int(record["record_index"]): record for record in records}
    candidates = []
    for mode in modes:
        for entry in mode["entries"]:
            from_record = by_index.get(int(entry["from_light_setting_index"]))
            to_record = by_index.get(int(entry["to_light_setting_index"]))
            if from_record is None or to_record is None:
                continue
            start = int(entry["start_angle"])
            end = int(entry["end_angle"])
            active = start <= active_angle and (active_angle < end or end == 0xFFFF)
            weight = transition_weight(start, end, active_angle)
            from_rgb = record_fog_rgb(from_record)
            to_rgb = record_fog_rgb(to_record)
            rgb = [native_lerp_u8(from_rgb[i], to_rgb[i], weight) for i in range(3)]
            item = {
                "mode_index": int(mode["mode_index"]),
                "entry_index": int(entry["entry_index"]),
                "start_angle": start,
                "end_angle": end,
                "from_light_setting_index": int(entry["from_light_setting_index"]),
                "to_light_setting_index": int(entry["to_light_setting_index"]),
                "active_for_demo_angle": active,
                "angle_weight": weight,
                "from_fog_rgb": from_rgb,
                "to_fog_rgb": to_rgb,
                "candidate_preaddend_rgb": rgb,
            }
            if target_rgb is not None:
                item["rgb_manhattan_diff_to_emulator"] = rgb_diff(rgb, target_rgb)
                item["required_addend_if_same_frame"] = [target_rgb[i] - rgb[i] for i in range(3)]
            candidates.append(item)
    return candidates


def summarize(args: argparse.Namespace) -> dict[str, Any]:
    demo = load_json(args.demo_stats)
    emulator = load_json(args.emulator_summary)
    emulator_rgb = dominant_fog_color(emulator)
    active_angle = int(
        get_path(demo, "engine_render_scene.pica_lighting.resolved_runtime_light_setting.active_angle", -1)
    )
    setup_index = int(get_path(demo, "engine_render_scene.pica_lighting.active_setup_index", args.setup_index))
    demo_preaddend = rgb_from_mapping(
        get_path(demo, "engine_render_scene.pica_lighting.resolved_runtime_light_setting.fog_preaddend_color")
    )
    demo_addend = get_path(
        demo,
        "engine_render_scene.pica_lighting.resolved_runtime_light_setting.fog_color_addend_i16",
        [0, 0, 0],
    )
    demo_final = rgb_from_mapping(
        get_path(demo, "engine_render_scene.pica_lighting.resolved_runtime_light_setting.final_fog_color")
    )

    records = scene_records(args.scene_light_table, args.scene_path, setup_index)
    scene_wide_records = all_scene_records(args.scene_light_table, args.scene_path)
    modes = parse_transition_table(args.code_bin)
    candidates = transition_candidates(modes, records, active_angle, emulator_rgb)
    active_candidates = [candidate for candidate in candidates if candidate["active_for_demo_angle"]]

    exact_scene_record_matches = []
    closest_scene_records = []
    if emulator_rgb is not None:
        for record in scene_wide_records:
            rgb = record_fog_rgb(record)
            diff = rgb_diff(rgb, emulator_rgb)
            item = {
                "setup_index": int(record["setup_index"]),
                "record_index": int(record["record_index"]),
                "offset_hex": record.get("offset_hex"),
                "fog_rgb": rgb,
                "rgb_manhattan_diff_to_emulator": diff,
            }
            if diff == 0:
                exact_scene_record_matches.append(item)
            closest_scene_records.append(item)
        closest_scene_records.sort(key=lambda item: item["rgb_manhattan_diff_to_emulator"])

    same_frame_required_addend = None
    if emulator_rgb is not None and demo_preaddend is not None:
        same_frame_required_addend = [emulator_rgb[i] - demo_preaddend[i] for i in range(3)]

    return {
        "format": "oot3d_title_intro_fog_runtime_candidate_audit_v1",
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "inputs": {
            "scene_path": args.scene_path,
            "setup_index": setup_index,
            "scene_light_table": str(args.scene_light_table),
            "code_bin": str(args.code_bin),
            "demo_stats": str(args.demo_stats),
            "emulator_summary": str(args.emulator_summary),
        },
        "demo_sample": {
            "title_intro_frame": get_path(demo, "title_intro_runtime.frame"),
            "initial_scene_cutscene_frame": get_path(demo, "title_intro_runtime.initial_scene_cutscene_frame"),
            "runtime_day_time": get_path(demo, "title_intro_runtime.runtime_environment_rendered_day_time"),
            "runtime_skybox_time": get_path(demo, "title_intro_runtime.runtime_environment_rendered_skybox_time"),
            "active_angle": active_angle,
            "current_mode": get_path(
                demo, "engine_render_scene.pica_lighting.resolved_runtime_light_setting.current_mode"
            ),
            "mode_state_bootstrapped_from_code_bin_fallback": get_path(
                demo,
                "engine_render_scene.pica_lighting.resolved_runtime_light_setting.mode_state_bootstrapped_from_code_bin_fallback",
            ),
            "branch": get_path(demo, "engine_render_scene.pica_lighting.resolved_runtime_light_setting.branch"),
            "fog_preaddend_rgb": demo_preaddend,
            "fog_addend_i16": demo_addend,
            "final_fog_rgb": demo_final,
        },
        "emulator_validation": {
            "dominant_fog_rgb": emulator_rgb,
            "pica_dump_frame_index_available": get_path(emulator, "frame.frame_index") is not None,
            "runtime_timeline_available": get_path(emulator, "frame.runtime") is not None,
            "memory_watch_available": get_path(emulator, "frame.memory_watch") is not None,
        },
        "active_setup_light_records": [
            {
                "record_index": int(record["record_index"]),
                "offset_hex": record.get("offset_hex"),
                "fog_rgb": record_fog_rgb(record),
                "raw_hex": record.get("raw_hex"),
            }
            for record in records
        ],
        "transition_candidates_for_active_setup": candidates,
        "active_transition_candidates_for_demo_angle": active_candidates,
        "same_frame_required_addend_from_demo_preaddend_to_emulator_rgb": same_frame_required_addend,
        "same_frame_addend_interpretation": (
            "This delta is diagnostic only: it is valid only if the emulator PICA frame and the demo "
            "title-intro frame are timeline-aligned, which the current dump does not prove."
        ),
        "scene_wide_exact_record_matches_to_emulator_rgb": exact_scene_record_matches,
        "scene_wide_closest_record_matches_to_emulator_rgb": closest_scene_records[:10],
        "conclusion": {
            "plain_active_setup_record_decode_explains_emulator_rgb": bool(exact_scene_record_matches),
            "current_demo_active_transition_reproduces_emulator_rgb": bool(
                emulator_rgb is not None
                and any(candidate["candidate_preaddend_rgb"] == emulator_rgb for candidate in active_candidates)
            ),
            "next_runtime_source_to_resolve": (
                "Trace/import native writers of play+0x3208..0x320c and capture emulator runtime "
                "timeline/playstate fields for the same PICA frame before using final fog RGB as a parity gate."
            ),
        },
    }


def write_markdown(path: Path, report: dict[str, Any]) -> None:
    demo = report["demo_sample"]
    emulator = report["emulator_validation"]
    lines = [
        "# Title Intro Fog Runtime Candidates",
        "",
        "## Summary",
        "",
        f"- Demo frame/cutscene: `{demo.get('title_intro_frame')}` / `{demo.get('initial_scene_cutscene_frame')}`.",
        f"- Demo active angle/mode: `{demo.get('active_angle')}` / `{demo.get('current_mode')}`.",
        f"- Demo fog pre-addend/addend/final: `{demo.get('fog_preaddend_rgb')}` + "
        f"`{demo.get('fog_addend_i16')}` -> `{demo.get('final_fog_rgb')}`.",
        f"- Emulator dominant PICA fog RGB: `{emulator.get('dominant_fog_rgb')}`.",
        f"- Emulator runtime timeline available: `{emulator.get('runtime_timeline_available')}`.",
        f"- Emulator memory watch available: `{emulator.get('memory_watch_available')}`.",
        "",
        "## Active Setup Records",
        "",
    ]
    for record in report["active_setup_light_records"]:
        lines.append(
            f"- record `{record['record_index']}` at `{record['offset_hex']}` fog `{record['fog_rgb']}`."
        )
    lines.extend(["", "## Active Transition Candidates", ""])
    for candidate in report["active_transition_candidates_for_demo_angle"]:
        lines.append(
            f"- mode `{candidate['mode_index']}` entry `{candidate['entry_index']}` "
            f"{candidate['from_light_setting_index']} -> {candidate['to_light_setting_index']} "
            f"weight `{candidate['angle_weight']:.6f}` gives `{candidate['candidate_preaddend_rgb']}`; "
            f"same-frame required addend `{candidate.get('required_addend_if_same_frame')}`."
        )
    lines.extend(
        [
            "",
            "## Scene-Wide Closest Records",
            "",
        ]
    )
    for record in report["scene_wide_closest_record_matches_to_emulator_rgb"][:5]:
        lines.append(
            f"- setup `{record['setup_index']}` record `{record['record_index']}` "
            f"fog `{record['fog_rgb']}` diff `{record['rgb_manhattan_diff_to_emulator']}`."
        )
    lines.extend(
        [
            "",
            "## Conclusion",
            "",
            f"- Active transition reproduces emulator RGB: "
            f"`{report['conclusion']['current_demo_active_transition_reproduces_emulator_rgb']}`.",
            f"- Plain scene record exact match exists: "
            f"`{report['conclusion']['plain_active_setup_record_decode_explains_emulator_rgb']}`.",
            f"- Next: {report['conclusion']['next_runtime_source_to_resolve']}",
            "",
            "The emulator RGB is validation evidence only; it is not a runtime data source.",
        ]
    )
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scene-light-table", type=Path, default=DEFAULT_SCENE_LIGHT_TABLE)
    parser.add_argument("--code-bin", type=Path, default=DEFAULT_CODE_BIN)
    parser.add_argument("--demo-stats", type=Path, default=DEFAULT_DEMO_STATS)
    parser.add_argument("--emulator-summary", type=Path, default=DEFAULT_EMULATOR_SUMMARY)
    parser.add_argument("--scene-path", default="spot00_info.zsi")
    parser.add_argument("--setup-index", type=int, default=6)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--markdown-output", type=Path)
    args = parser.parse_args()

    report = summarize(args)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding="ascii")
    markdown_output = args.markdown_output or args.output.with_suffix(".md")
    write_markdown(markdown_output, report)
    print(json.dumps({"output": str(args.output), "markdown_output": str(markdown_output)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
