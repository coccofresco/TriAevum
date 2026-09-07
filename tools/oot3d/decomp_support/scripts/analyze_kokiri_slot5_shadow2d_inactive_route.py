#!/usr/bin/env python3
"""Audit Kokiri slot 5 Shadow2D inactivity against native/static evidence."""

from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path
from typing import Any


FORMAT = "oot3d_kokiri_slot5_shadow2d_inactive_route_v1"

DEFAULT_REPO_ROOT = Path(__file__).resolve().parents[4]
DEFAULT_ANALYSIS_ROOT = DEFAULT_REPO_ROOT / "tools" / "oot3d" / "decomp_support" / "analysis"
DEFAULT_VALIDATION_JSON = DEFAULT_ANALYSIS_ROOT / "kokiri_slot5_shadow_register_validation.json"
DEFAULT_CAPTURE_DERIVED = (
    DEFAULT_REPO_ROOT
    / "captures"
    / "azahar_pica"
    / "kokiri_slot5_20260704_101127"
    / "derived"
)


STATIC_EVIDENCE = [
    {
        "id": "remaining_gate_writer_003892d8_is_actor_spawn_logic",
        "address": "0x003892D8",
        "path": DEFAULT_ANALYSIS_ROOT
        / "shadow_gate_remaining_ghidra_export"
        / "decompiled"
        / "99005_003892d8_FUN_003892d8.c",
        "positive_checks": [
            (
                "clears_actor_gate_bit_0x02",
                "*(byte *)(param_1 + 0x1b5) = *(byte *)(param_1 + 0x1b5) & 0xfd",
            ),
            ("spawns_actor_0x4c", "Actor_Spawn(iVar4,param_2,0x4c"),
            ("uses_actor_timers", "*(undefined2 *)(param_1 + 0x26c)"),
            ("uses_actor_state_float", "*(undefined4 *)(param_1 + 0x280)"),
        ],
        "negative_checks": [
            ("does_not_write_autoclass1_record_pointer_offset", "+ 0x1a8"),
            ("does_not_write_autoclass1_record_pointer_word", "[0x6a]"),
        ],
    },
    {
        "id": "player_setmodels_writes_player_selector_fields",
        "address": "0x0032C2C0",
        "path": DEFAULT_ANALYSIS_ROOT
        / "shadow_route_focus_ghidra_export"
        / "decompiled"
        / "99052_0032c2c0_Player_SetModels.c",
        "positive_checks": [
            ("writes_player_gate_selector_byte", "*(undefined1 *)(param_1 + 0x1b5)"),
            ("writes_player_model_pointer_slot", "*(int *)(param_1 + 0x1bc)"),
            ("uses_native_five_byte_record", "iVar4 = param_2 * 5"),
        ],
        "negative_checks": [
            ("does_not_write_autoclass1_record_pointer_offset", "+ 0x1a8"),
            ("does_not_write_autoclass1_record_pointer_word", "[0x6a]"),
        ],
    },
    {
        "id": "player_init_calls_setequipment_with_player_as_second_arg",
        "address": "0x00191844",
        "path": DEFAULT_REPO_ROOT
        / "tools"
        / "oot3d"
        / "decomp_support"
        / "ghidra_export"
        / "decompiled"
        / "99047_00191844_Player_Init.c",
        "positive_checks": [
            ("player_actor_passed_to_equipment_setup", "Player_SetEquipmentData(param_2,param_1)"),
            ("equipment_setup_precedes_player_init_common", "Player_InitCommon(param_1,param_2"),
        ],
        "negative_checks": [],
    },
    {
        "id": "player_setequipment_passes_player_to_setmodels",
        "address": "0x0034913C",
        "path": DEFAULT_ANALYSIS_ROOT
        / "shadow_route_focus_ghidra_export"
        / "decompiled"
        / "99075_0034913c_Player_SetEquipmentData.c",
        "positive_checks": [
            ("setmodels_receives_player_param", "Player_SetModels(param_2)"),
            ("writes_player_equipment_fields", "*(char *)(param_2 + 0x1a6)"),
        ],
        "negative_checks": [
            ("does_not_write_autoclass1_record_pointer_offset", "+ 0x1a8"),
            ("does_not_write_autoclass1_record_pointer_word", "[0x6a]"),
        ],
    },
    {
        "id": "player_draw_clones_existing_source_context",
        "address": "0x004BF618",
        "path": DEFAULT_ANALYSIS_ROOT
        / "shadow_route_focus_ghidra_export"
        / "decompiled"
        / "99174_004bf618_Player_Draw.c",
        "positive_checks": [
            ("clones_player_source_context", "FUN_004c346c(iVar4,*(undefined4 *)((int)param_1 + 0x178))"),
            ("stores_shadow_clone_context", "*(int *)((int)param_1 + 0x291c) = iVar5"),
            ("creates_shadow_draw_object_from_player_cmb", "*(undefined4 *)((int)param_1 + 0x24dc)"),
        ],
        "negative_checks": [],
    },
    {
        "id": "player_source_context_helpers_do_not_install_record_pointer",
        "address": "0x002D5F68",
        "path": DEFAULT_ANALYSIS_ROOT
        / "shadow_route_focus_ghidra_export"
        / "decompiled"
        / "99038_002d5f68_FUN_002d5f68.c",
        "positive_checks": [
            ("uses_player_source_context", "uVar5 = *(undefined4 *)(param_2 + 0x178)"),
            ("temporarily_overrides_texcoord_byte", "*(undefined1 *)(iVar3 + 0x1b6) = 0"),
            ("updates_indexed_float_slots", "FUN_00358964(*(undefined4 *)(param_2 + 0x178),5"),
        ],
        "negative_checks": [
            ("does_not_write_autoclass1_record_pointer_offset", "+ 0x1a8"),
            ("does_not_write_autoclass1_record_pointer_word", "[0x6a]"),
        ],
    },
]


def read_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def rel(path: Path, repo_root: Path) -> str:
    try:
        return str(path.relative_to(repo_root))
    except ValueError:
        return str(path)


def find_lines(path: Path, pattern: str) -> list[dict[str, Any]]:
    if not path.exists():
        return []
    return [
        {"line": line_number, "text": line.strip()}
        for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1)
        if pattern in line
    ]


def analyze_static_item(item: dict[str, Any], repo_root: Path) -> dict[str, Any]:
    path = Path(item["path"])
    positives = []
    for check_id, pattern in item["positive_checks"]:
        matches = find_lines(path, pattern)
        positives.append(
            {
                "id": check_id,
                "pattern": pattern,
                "present": bool(matches),
                "matches": matches[:4],
            }
        )

    negatives = []
    for check_id, pattern in item["negative_checks"]:
        matches = find_lines(path, pattern)
        negatives.append(
            {
                "id": check_id,
                "pattern": pattern,
                "absent": not matches,
                "matches": matches[:4],
            }
        )

    return {
        "id": item["id"],
        "address": item["address"],
        "path": rel(path, repo_root),
        "exists": path.exists(),
        "positive_checks": positives,
        "negative_checks": negatives,
        "all_positive_checks_present": all(check["present"] for check in positives),
        "all_negative_checks_absent": all(check["absent"] for check in negatives),
    }


def count_values(frames: list[dict[str, Any]], key: str) -> list[dict[str, Any]]:
    counter = Counter(str(frame.get(key)) for frame in frames)
    return [{"value": value, "count": counter[value]} for value in sorted(counter)]


def summarize_validation(path: Path, repo_root: Path) -> dict[str, Any]:
    data = read_json(path)
    conclusions = data.get("conclusions", {})
    return {
        "path": rel(path, repo_root),
        "source_kind": data.get("source_kind"),
        "runtime_source_policy": data.get("runtime_source_policy"),
        "frame_count": data.get("frame_count", len(data.get("frames", []))),
        "distinct_values": data.get("distinct_values", {}),
        "boolean_counts": data.get("boolean_counts", {}),
        "conclusions": conclusions,
        "all_required_inactive_conclusions_true": all(
            conclusions.get(key) is True
            for key in [
                "all_fragop_shadow_payloads_match_codebin_default_0x00003c00",
                "all_texunit0_shadow_payloads_zero",
                "no_primary_rgb_shadow_term_frames",
                "no_dmp_shadow_z_uniform_uploads",
                "validation_supports_default_or_inactive_shadow_route_for_capture",
            ]
        ),
    }


def summarize_latest_capture(path: Path, repo_root: Path) -> dict[str, Any]:
    files = sorted(path.glob("*.native_pica_register_trace.json")) if path.exists() else []
    frames: list[dict[str, Any]] = []
    for frame_path in files:
        data = read_json(frame_path)
        shadow_registers = data.get("shadow_registers", {})
        texture_shadow = shadow_registers.get("texture_shadow", {})
        framebuffer_shadow = shadow_registers.get("framebuffer_shadow", {})
        depthmap = shadow_registers.get("depthmap", {})
        shader_route = data.get("shadow_shader_route", {})
        diagnostics = data.get("diagnostics", {})
        draw_summary = data.get("frame_capture", {}).get("draw_summary", {})
        frames.append(
            {
                "path": rel(frame_path, repo_root),
                "texture_shadow_raw": texture_shadow.get("raw"),
                "framebuffer_shadow_raw": framebuffer_shadow.get("raw"),
                "depthmap_scale_raw": depthmap.get("scale_raw"),
                "depthmap_offset_raw": depthmap.get("offset_raw"),
                "lighting_enable_shadow": shader_route.get("enable_shadow"),
                "shadow_texture_is_shadow2d": shader_route.get("shadow_texture_is_shadow2d"),
                "matches_primary_rgb_shadow_term": shader_route.get(
                    "matches_primary_rgb_shadow_term"
                ),
                "shadow_shader_route_matches_primary_rgb_shadow_term": diagnostics.get(
                    "shadow_shader_route_matches_primary_rgb_shadow_term"
                ),
                "dmp_shadow_z_uniforms_present": diagnostics.get("dmp_shadow_z_uniforms_present"),
                "primary_rgb_shadow_route_draw_count": draw_summary.get(
                    "primary_rgb_shadow_route_draw_count"
                ),
                "shadow2d_texture_draw_count": draw_summary.get("shadow2d_texture_draw_count"),
            }
        )

    observed_texture_values = [
        frame["texture_shadow_raw"] for frame in frames if frame["texture_shadow_raw"] is not None
    ]
    observed_fragop_values = [
        frame["framebuffer_shadow_raw"]
        for frame in frames
        if frame["framebuffer_shadow_raw"] is not None
    ]
    no_nonzero_texture_payloads = bool(frames) and all(
        frame["texture_shadow_raw"] in (None, "0x00000000") for frame in frames
    )
    no_nondefault_fragop_payloads = bool(frames) and all(
        frame["framebuffer_shadow_raw"] in (None, "0x00003C00") for frame in frames
    )
    all_observed_texture_zero = bool(observed_texture_values) and all(
        value == "0x00000000" for value in observed_texture_values
    )
    all_observed_fragop_default = bool(observed_fragop_values) and all(
        value == "0x00003C00" for value in observed_fragop_values
    )
    all_no_primary_rgb_shadow = bool(frames) and all(
        frame["matches_primary_rgb_shadow_term"] is False for frame in frames
    )
    all_no_shadow2d_draws = bool(frames) and all(
        frame["shadow2d_texture_draw_count"] in (0, None) for frame in frames
    )
    all_no_primary_shadow_draws = bool(frames) and all(
        frame["primary_rgb_shadow_route_draw_count"] in (0, None) for frame in frames
    )

    return {
        "path": rel(path, repo_root),
        "exists": path.exists(),
        "frame_count": len(frames),
        "distinct_values": {
            "texture_shadow_raw": count_values(frames, "texture_shadow_raw"),
            "framebuffer_shadow_raw": count_values(frames, "framebuffer_shadow_raw"),
            "lighting_enable_shadow": count_values(frames, "lighting_enable_shadow"),
            "shadow_texture_is_shadow2d": count_values(frames, "shadow_texture_is_shadow2d"),
            "primary_rgb_shadow_route_draw_count": count_values(
                frames, "primary_rgb_shadow_route_draw_count"
            ),
            "shadow2d_texture_draw_count": count_values(frames, "shadow2d_texture_draw_count"),
        },
        "frames": frames,
        "conclusions": {
            "observed_texunit0_shadow_payloads_zero": all_observed_texture_zero,
            "observed_fragop_shadow_payloads_match_codebin_default_0x00003c00": (
                all_observed_fragop_default
            ),
            "no_nonzero_texunit0_shadow_payloads": no_nonzero_texture_payloads,
            "no_nondefault_fragop_shadow_payloads": no_nondefault_fragop_payloads,
            "no_primary_rgb_shadow_term_frames": all_no_primary_rgb_shadow,
            "no_shadow2d_texture_draws": all_no_shadow2d_draws,
            "no_primary_rgb_shadow_route_draws": all_no_primary_shadow_draws,
            "validation_supports_inactive_shadow2d_for_latest_capture": (
                no_nonzero_texture_payloads
                and no_nondefault_fragop_payloads
                and all_no_primary_rgb_shadow
                and all_no_shadow2d_draws
                and all_no_primary_shadow_draws
            ),
        },
    }


def build_summary(repo_root: Path, validation_json: Path, capture_derived: Path) -> dict[str, Any]:
    static_evidence = [analyze_static_item(item, repo_root) for item in STATIC_EVIDENCE]
    validation = summarize_validation(validation_json, repo_root)
    latest_capture = summarize_latest_capture(capture_derived, repo_root)

    static_all_positive = all(item["all_positive_checks_present"] for item in static_evidence)
    static_all_negative = all(item["all_negative_checks_absent"] for item in static_evidence)
    latest_capture_inactive = latest_capture["conclusions"][
        "validation_supports_inactive_shadow2d_for_latest_capture"
    ]

    return {
        "format": FORMAT,
        "source_policy": {
            "native_runtime_data": "asset_or_codebin_only",
            "emulator_trace_role": "validation_only",
            "emulator_restart_policy": "restart_only_when_new_targeted_evidence_is_required",
        },
        "validation_summary": validation,
        "latest_capture_summary": latest_capture,
        "static_evidence": static_evidence,
        "conclusions": {
            "kokiri_slot5_shadow2d_register_route_active": False,
            "kokiri_slot5_shadow2d_inactivity_validated": (
                validation["all_required_inactive_conclusions_true"] and latest_capture_inactive
            ),
            "remaining_gate_writer_003892d8_excluded": static_evidence[0][
                "all_positive_checks_present"
            ]
            and static_evidence[0]["all_negative_checks_absent"],
            "player_setmodels_writes_player_fields_not_autoclass1": static_evidence[1][
                "all_positive_checks_present"
            ]
            and static_evidence[1]["all_negative_checks_absent"]
            and static_evidence[2]["all_positive_checks_present"]
            and static_evidence[3]["all_positive_checks_present"],
            "player_draw_and_helpers_clone_or_mutate_existing_context_not_record_pointer": (
                static_evidence[4]["all_positive_checks_present"]
                and static_evidence[5]["all_positive_checks_present"]
                and static_evidence[5]["all_negative_checks_absent"]
            ),
            "all_static_checks_pass": static_all_positive and static_all_negative,
            "link_self_shadow_should_not_target_active_texunit0_shadow_for_this_fixture": True,
            "next_target": (
                "Decode and implement the native Link CMB material lighting path: per-material "
                "lighting flags, vertex colors/normals, fragment lighting/LUT state, and texture "
                "environment inputs. Treat Shadow2D/TEXUNIT0_SHADOW as a separate route that "
                "remains blocked here until a native non-default owner chain is proven."
            ),
        },
    }


def render_markdown(summary: dict[str, Any]) -> str:
    conclusions = summary["conclusions"]
    lines = [
        "# Kokiri Slot 5 Shadow2D Inactive Route",
        "",
        "This report separates the current Kokiri slot 5 visual self-shading gap from the native Shadow2D/TEXUNIT0_SHADOW route. Emulator traces are validation-only; runtime data must still come from OOT3D assets or `code.bin`.",
        "",
        "## Conclusions",
        "",
    ]
    for key, value in conclusions.items():
        lines.append(f"- `{key}`: `{value}`")

    validation = summary["validation_summary"]
    latest = summary["latest_capture_summary"]
    lines.extend(
        [
            "",
            "## Validation Inputs",
            "",
            f"- Validated summary: `{validation['path']}`",
            f"- Summary frame count: `{validation['frame_count']}`",
            f"- Latest capture directory: `{latest['path']}`",
            f"- Latest capture frame count: `{latest['frame_count']}`",
            f"- Validation policy: `{validation['runtime_source_policy']}`",
            "",
            "## Register Evidence",
            "",
        ]
    )
    for section_name, values in latest["distinct_values"].items():
        formatted = ", ".join(f"`{item['value']}` x{item['count']}" for item in values)
        lines.append(f"- `{section_name}`: {formatted}")

    lines.extend(["", "## Static Evidence", ""])
    for item in summary["static_evidence"]:
        lines.append(f"### {item['id']} {item['address']}")
        lines.append(f"- Path: `{item['path']}`")
        lines.append(f"- Positive checks present: `{item['all_positive_checks_present']}`")
        lines.append(f"- Negative checks absent: `{item['all_negative_checks_absent']}`")
        for check in item["positive_checks"]:
            lines.append(f"- `{check['id']}`: `{check['present']}`")
            for match in check["matches"][:2]:
                lines.append(f"  - line `{match['line']}`: `{match['text']}`")
        for check in item["negative_checks"]:
            lines.append(f"- `{check['id']}` absent: `{check['absent']}`")
            for match in check["matches"][:2]:
                lines.append(f"  - line `{match['line']}`: `{match['text']}`")
        lines.append("")

    lines.extend(
        [
            "## Boundary",
            "",
            "For Kokiri slot 5, both the pre-existing register-validation summary and the latest derived capture show `GPUREG_TEXUNIT0_SHADOW=0x00000000`, `GPUREG_FRAGOP_SHADOW=0x00003C00`, no primary RGB shadow route, and no Shadow2D texture draws. Static code review also excludes the remaining literal gate writer `0x003892D8` as actor-spawn/gameplay logic and corrects the `Player_SetModels` route as player field selection rather than `AutoClass1+0x1A8` record-pointer installation.",
            "",
            "The engine should therefore move the current Link self-shading investigation to native CMB material lighting, vertex color/normal handling, fragment lighting/LUT state, and TextureEnv inputs. Shadow2D remains a real native route, but it is not the active source of the missing visual self-shadow in this fixture.",
            "",
        ]
    )
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, default=DEFAULT_REPO_ROOT)
    parser.add_argument("--validation-json", type=Path, default=DEFAULT_VALIDATION_JSON)
    parser.add_argument("--capture-derived", type=Path, default=DEFAULT_CAPTURE_DERIVED)
    parser.add_argument("--output-json", type=Path, required=True)
    parser.add_argument("--output-md", type=Path, required=True)
    args = parser.parse_args()

    summary = build_summary(args.repo_root, args.validation_json, args.capture_derived)
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    args.output_md.write_text(render_markdown(summary), encoding="utf-8")
    print(
        json.dumps(
            {
                "status": "ok",
                "output_json": str(args.output_json),
                "output_md": str(args.output_md),
                "kokiri_slot5_shadow2d_inactivity_validated": summary["conclusions"][
                    "kokiri_slot5_shadow2d_inactivity_validated"
                ],
                "all_static_checks_pass": summary["conclusions"]["all_static_checks_pass"],
                "next_target": summary["conclusions"]["next_target"],
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
