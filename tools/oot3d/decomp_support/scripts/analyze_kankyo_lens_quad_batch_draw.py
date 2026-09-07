#!/usr/bin/env python3
"""Promote the OOT3D kankyo/lens quad-batch draw path into a checkable report.

This analyzer is intentionally narrow: it reads the current Ghidra decompile of
FUN_003FC2F8 and the directly called draw-buffer helpers, verifies the native
anchors we rely on, and emits JSON/Markdown evidence. It does not mark the final
backend submit as resolved; that still requires the draw-handle material/packet
consumer.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[4]
DECOMPILED = ROOT / "tools" / "oot3d" / "decomp_support" / "ghidra_export" / "decompiled"
ANALYSIS = ROOT / "tools" / "oot3d" / "decomp_support" / "analysis"


@dataclass(frozen=True)
class SourceFile:
    label: str
    address: str
    path: Path


SOURCES = {
    "draw": SourceFile("quad_batch_draw", "0x003FC2F8", DECOMPILED / "99105_003fc2f8_FUN_003fc2f8.c"),
    "primary": SourceFile("primary_draw_buffer", "0x00333270", DECOMPILED / "99073_00333270_FUN_00333270.c"),
    "u32x4": SourceFile("optional_u32x4_lane_buffer", "0x003331EC", DECOMPILED / "99072_003331ec_FUN_003331ec.c"),
    "float2": SourceFile("optional_float2_lane_buffer", "0x00333070", DECOMPILED / "99071_00333070_FUN_00333070.c"),
    "draw_count": SourceFile("draw_count_writer", "0x00333294", DECOMPILED / "99074_00333294_FUN_00333294.c"),
}


ANCHORS: dict[str, list[tuple[str, str]]] = {
    "draw": [
        ("draw_method_signature", "void FUN_003fc2f8(int param_1,undefined4 param_2,undefined4 *param_3,float *param_4)"),
        ("initial_draw_count_reset", "FUN_00333294(param_1,0);"),
        ("queued_element_count_gate", "if (*(int *)(param_1 + 0x1fc) != 0)"),
        ("matrix_source_mode_descriptor_field_0x18", "iVar2 = *(int *)(**(int **)(param_1 + 4) + 0x18);"),
        ("runtime_matrix_offset_0x84", "oot3d_copy_u32x12_if_distinct(param_1 + 0x84"),
        ("runtime_depth_average_offset_0x0e4", "*(int *)(param_1 + 0xe4) = (int)uVar38;"),
        ("runtime_flags_offset_0x178", "*(uint *)(param_1 + 0x178)"),
        ("input_centers_offset_0x1e4", "pfVar12 = *(float **)(param_1 + 0x1e4);"),
        ("input_matrices_offset_0x1e8", "iVar2 = *(int *)(param_1 + 0x1e8);"),
        ("input_local_vectors_offset_0x1ec", "pfVar21 = *(float **)(param_1 + 0x1ec);"),
        ("average_position_offsets_0x280_0x284_0x288", "*(float *)(param_1 + 0x280) = fVar37;"),
        ("sort_enable_descriptor_field_0x20", "if (*(short *)(**(int **)(param_1 + 4) + 0x20) == 1)"),
        ("primary_buffer_helper", "FUN_00333270(*(undefined4 *)(param_1 + 4))"),
        ("descriptor_flag_0x80_gate", "(uVar28 & 0x80) == 0"),
        ("secondary_float3_disable_flag_0x20000", "(uVar28 & 0x20000) == 0"),
        ("secondary_float3_helper", "FUN_00408c80()"),
        ("optional_u32x4_source_offset_0x1f0", "*(int *)(param_1 + 0x1f0)"),
        ("optional_u32x4_disable_flag_0x40000", "(*(uint *)(param_1 + 0x178) & 0x40000) == 0"),
        ("optional_u32x4_buffer_helper", "FUN_003331ec(*(undefined4 *)(param_1 + 4))"),
        ("optional_float2_source_offset_0x1f4_decimal_500", "*(int *)(param_1 + 500)"),
        ("optional_float2_disable_flag_0x80000", "(*(uint *)(param_1 + 0x178) & 0x80000) == 0"),
        ("optional_float2_buffer_helper", "FUN_00333070(*(undefined4 *)(param_1 + 4))"),
        ("scratch_transformed_quad_offset_0x200", "iVar2 = param_1 + 0x200;"),
        ("scratch_secondary_quad_offset_0x230", "iVar2 = param_1 + 0x230;"),
        ("float2_base_offsets_0x260_0x264", "local_68 = *(float *)(iVar13 + 0x260) + *pfVar3;"),
        ("primary_writes_four_float3_corners", "do {\n              fVar36 = local_a4[iVar2 * 3 + 1];"),
        ("secondary_writes_four_float3_corners", "if (local_6c != (undefined4 *)0x0) {\n                uVar7 = local_d4[iVar2 * 3 + 1];"),
        ("u32x4_writes_four_words_per_corner", "puVar6 = (undefined4 *)\n                         (*(int *)(param_1 + 0x1f0) + (iVar2 + (int)fVar37 * 4) * 0x10);"),
        ("float2_writes_two_floats_per_corner", "*pfVar19 = local_68;\n                pfVar19[1] = local_64;"),
        ("final_draw_count_visible_elements_times_six", "FUN_00333294(param_1,local_50 * 6);"),
        ("queued_element_count_clear", "*(undefined4 *)(param_1 + 0x1fc) = 0;"),
        ("draw_handle_page_advance", "FUN_00368d94(*(int *)(iVar2 + 0x124) + 1,*(undefined4 *)(iVar2 + 0x128));"),
    ],
    "primary": [
        ("primary_helper_signature", "undefined4 FUN_00333270(int param_1)"),
        ("primary_draw_handle_page", "FUN_00368d94(*(int *)(param_1 + 0x124) + 1,*(undefined4 *)(param_1 + 0x128));"),
        ("primary_slot_base_0x1a0", "return *(undefined4 *)(param_1 + extraout_r1 * 4 + 0x1a0);"),
    ],
    "u32x4": [
        ("u32x4_helper_signature", "int FUN_003331ec(int *param_1)"),
        ("descriptor_vertex_slot_count_field_0x0c", "iVar1 = *(int *)(*param_1 + 0xc);"),
        ("descriptor_flag_field_0x1c", "uVar4 = *(uint *)(*param_1 + 0x1c);"),
        ("descriptor_flag_0x80_primary_stride_gate", "if ((uVar4 & 0x80) == 0)"),
        ("descriptor_flag_0x10_float2_stride_gate", "if ((uVar4 & 0x10) == 0)"),
        ("returns_after_primary_and_float2_lanes", "return iVar1 + iVar2 + param_1[extraout_r1 + 0x68] + iVar3 * 0xc;"),
    ],
    "float2": [
        ("float2_helper_signature", "int FUN_00333070(int *param_1)"),
        ("descriptor_vertex_slot_count_field_0x0c", "iVar1 = *(int *)(*param_1 + 0xc);"),
        ("descriptor_flag_0x80_primary_stride_gate", "if ((*(uint *)(*param_1 + 0x1c) & 0x80) == 0)"),
        ("returns_after_primary_lane", "return iVar1 + param_1[extraout_r1 + 0x68] + iVar2 * 0xc;"),
    ],
    "draw_count": [
        ("draw_count_helper_signature", "void FUN_00333294(int param_1,int param_2)"),
        ("max_draw_count_descriptor_field_0x14", "iVar1 = *(int *)(**(int **)(param_1 + 4) + 0x14);"),
        ("default_max_draw_count_four", "if (iVar1 == 0) {\n    iVar1 = 4;"),
        ("draw_count_offset_0x174", "*(int *)(param_1 + 0x174) = param_2;"),
    ],
}


def fmt_hex(value: int) -> str:
    return f"0x{value:03X}" if value < 0x1000 else f"0x{value:08X}"


def load_sources() -> dict[str, str]:
    loaded: dict[str, str] = {}
    missing: list[str] = []
    for key, source in SOURCES.items():
        if not source.path.is_file():
            missing.append(str(source.path))
            continue
        loaded[key] = source.path.read_text(encoding="utf-8", errors="replace")
    if missing:
        raise FileNotFoundError("missing required Ghidra export(s): " + ", ".join(missing))
    return loaded


def verify_anchors(texts: dict[str, str]) -> list[dict[str, Any]]:
    checks: list[dict[str, Any]] = []
    missing: list[str] = []
    for key, entries in ANCHORS.items():
        text = texts[key]
        for label, needle in entries:
            found = needle in text
            checks.append(
                {
                    "source": SOURCES[key].label,
                    "function_address": SOURCES[key].address,
                    "label": label,
                    "found": found,
                    "needle": needle,
                }
            )
            if not found:
                missing.append(f"{SOURCES[key].label}:{label}")
    if missing:
        raise AssertionError("missing native anchor(s): " + ", ".join(missing))
    return checks


def build_report(checks: list[dict[str, Any]]) -> dict[str, Any]:
    source_files = {
        key: {
            "label": source.label,
            "function_address": source.address,
            "path": str(source.path.relative_to(ROOT)),
        }
        for key, source in SOURCES.items()
    }
    runtime_offsets = [
        {"offset": fmt_hex(0x084), "name": "runtime_object_matrix_3x4", "status": "verified_in_FUN_003FC2F8"},
        {"offset": fmt_hex(0x0E4), "name": "average_depth", "status": "verified_in_FUN_003FC2F8"},
        {"offset": fmt_hex(0x174), "name": "draw_count", "status": "verified_in_FUN_00333294"},
        {"offset": fmt_hex(0x178), "name": "runtime_flags", "status": "verified_in_FUN_003FC2F8"},
        {"offset": fmt_hex(0x1E4), "name": "queued_element_center_float3_array", "status": "verified_in_FUN_003FC2F8"},
        {"offset": fmt_hex(0x1E8), "name": "queued_element_matrix_3x4_array", "status": "verified_in_FUN_003FC2F8"},
        {"offset": fmt_hex(0x1EC), "name": "queued_element_local_vector_float3_array", "status": "verified_in_FUN_003FC2F8"},
        {"offset": fmt_hex(0x1F0), "name": "optional_u32x4_source_pointer", "status": "verified_in_FUN_003FC2F8"},
        {"offset": fmt_hex(0x1F4), "name": "optional_float2_source_pointer", "status": "verified_in_FUN_003FC2F8_as_decimal_500"},
        {"offset": fmt_hex(0x1FC), "name": "queued_element_count", "status": "verified_in_FUN_003FC2F8"},
        {"offset": fmt_hex(0x200), "name": "scratch_primary_quad_float3x4", "status": "verified_in_FUN_003FC2F8"},
        {"offset": fmt_hex(0x230), "name": "scratch_secondary_quad_float3x4", "status": "verified_in_FUN_003FC2F8"},
        {"offset": fmt_hex(0x260), "name": "corner_float2_base_u0", "status": "verified_in_FUN_003FC2F8"},
        {"offset": fmt_hex(0x264), "name": "corner_float2_base_v0", "status": "verified_in_FUN_003FC2F8"},
        {"offset": fmt_hex(0x280), "name": "average_position_x", "status": "verified_in_FUN_003FC2F8"},
        {"offset": fmt_hex(0x284), "name": "average_position_y", "status": "verified_in_FUN_003FC2F8"},
        {"offset": fmt_hex(0x288), "name": "average_position_z", "status": "verified_in_FUN_003FC2F8"},
    ]
    output_lanes = [
        {
            "name": "primary_float3_quad_lane",
            "helper": "FUN_00333270",
            "helper_address": "0x00333270",
            "per_visible_element": "4 float3 corners",
            "source": "runtime scratch at object+0x200 after local/matrix transforms",
            "gate": "always when visible element count is nonzero",
            "native_status": "verified",
        },
        {
            "name": "secondary_float3_quad_lane",
            "helper": "FUN_00408C80",
            "helper_address": "0x00408C80",
            "per_visible_element": "4 float3 corners",
            "source": "runtime scratch at object+0x230 after secondary transform",
            "gate": "descriptor flag 0x80 clear and runtime flag 0x20000 clear",
            "native_status": "helper_call_verified_semantic_consumer_pending",
        },
        {
            "name": "optional_u32x4_quad_lane",
            "helper": "FUN_003331EC",
            "helper_address": "0x003331EC",
            "per_visible_element": "4 u32x4 records",
            "source": "runtime pointer object+0x1F0, indexed by corner + source_index*4",
            "gate": "source pointer non-null and runtime flag 0x40000 clear",
            "native_status": "verified",
        },
        {
            "name": "optional_float2_quad_lane",
            "helper": "FUN_00333070",
            "helper_address": "0x00333070",
            "per_visible_element": "4 float2 records",
            "source": "runtime pointer object+0x1F4 plus corner bases object+0x260/0x264",
            "gate": "source pointer non-null and runtime flag 0x80000 clear",
            "native_status": "verified",
        },
    ]
    descriptor_fields = [
        {"offset": fmt_hex(0x00C), "name": "descriptor_vertex_slot_count", "default": 4},
        {"offset": fmt_hex(0x014), "name": "max_draw_count", "default": 4},
        {"offset": fmt_hex(0x018), "name": "matrix_source_mode", "observed_modes": [0, 1]},
        {"offset": fmt_hex(0x01C), "name": "descriptor_flags", "observed_bits": ["0x80", "0x10"]},
        {"offset": fmt_hex(0x020), "name": "depth_sort_enable", "enabled_when": 1},
    ]
    runtime_flags = [
        {"bit": "0x00000001", "effect": "skip rebuilding runtime object matrix before queuing"},
        {"bit": "0x00000004", "effect": "keeps otherwise rejected depth entries in visible list"},
        {"bit": "0x00020000", "effect": "disable secondary float3 lane"},
        {"bit": "0x00040000", "effect": "disable optional u32x4 lane"},
        {"bit": "0x00080000", "effect": "disable optional float2 lane"},
    ]
    return {
        "report": "kankyo_lens_quad_batch_draw",
        "source": "OOT3D code.bin Ghidra decompile",
        "source_files": source_files,
        "anchor_check_count": len(checks),
        "all_anchors_found": all(check["found"] for check in checks),
        "checks": checks,
        "resolved": {
            "quad_batch_draw_method": True,
            "draw_buffer_helpers": True,
            "runtime_object_offsets": True,
            "visible_element_to_native_draw_count_formula": "visible_count * 6",
            "backend_submit_resolved": False,
            "renderer_behavior_enabled_from_this_report": False,
        },
        "runtime_offsets": runtime_offsets,
        "descriptor_fields": descriptor_fields,
        "runtime_flags": runtime_flags,
        "output_lanes": output_lanes,
        "native_algorithm": [
            "Reset draw count through FUN_00333294(object, 0).",
            "If queued element count object+0x1FC is nonzero, rebuild object matrix unless runtime flag bit 0 is set.",
            "Transform queued local vectors through each element matrix and object matrix, then compute visibility/depth and average position/depth.",
            "Sort visible elements by depth when descriptor field +0x20 is 1.",
            "Write primary and optional per-corner lanes for each visible element.",
            "Finalize native draw count as visible_count * 6, clear object+0x1FC, and advance the draw handle page.",
        ],
        "title_day_transition_impact": {
            "clock_and_schedule_side": "already_resolved",
            "visible_sun_lens_haze_side": "narrowed_to_proven_primitive_packet_backend_implementation",
            "effect_primitive_draw_packet_report": "kankyo_effect_primitive_draw_packet",
            "do_not_use_generic_light_fallback": True,
        },
        "next_required_proof": [
            "Consume the proven FUN_00313444 primitive packet and attribute lanes in the backend/OpenGL renderer.",
            "Preserve the verified runtime draw count, index type, draw mode, color, texcoord, and position lane semantics.",
            "Only after that, enable visible sun/lens/haze rendering from decoded Lens* TBD and CTXB resources.",
        ],
    }


def write_markdown(report: dict[str, Any], path: Path) -> None:
    lines = [
        "# Kankyo Lens Quad-Batch Draw",
        "",
        "This report is generated from the current OOT3D `code.bin` Ghidra decompile. It promotes only verified native anchors.",
        "",
        "## Resolution State",
        "",
        f"- Quad-batch draw method: `{report['source_files']['draw']['function_address']}`.",
        "- Runtime buffer helpers: `FUN_00333270`, `FUN_003331EC`, `FUN_00333070`, plus secondary helper `FUN_00408C80`.",
        f"- Visible element draw count formula: `{report['resolved']['visible_element_to_native_draw_count_formula']}`.",
        f"- Backend submit resolved: `{str(report['resolved']['backend_submit_resolved']).lower()}`.",
        "",
        "## Output Lanes",
        "",
    ]
    for lane in report["output_lanes"]:
        lines.append(
            f"- `{lane['name']}` via `{lane['helper']}`: {lane['per_visible_element']}; "
            f"source {lane['source']}; gate {lane['gate']}; status `{lane['native_status']}`."
        )
    lines.extend(["", "## Runtime Offsets", ""])
    for item in report["runtime_offsets"]:
        lines.append(f"- `{item['offset']}`: `{item['name']}` ({item['status']}).")
    lines.extend(["", "## Descriptor Fields", ""])
    for item in report["descriptor_fields"]:
        details = ", ".join(f"{key}={value!r}" for key, value in item.items() if key not in {"offset", "name"})
        lines.append(f"- `{item['offset']}`: `{item['name']}`" + (f" ({details})." if details else "."))
    lines.extend(["", "## Runtime Flags", ""])
    for item in report["runtime_flags"]:
        lines.append(f"- `{item['bit']}`: {item['effect']}.")
    lines.extend(["", "## Title Intro Day Transition Impact", ""])
    impact = report["title_day_transition_impact"]
    lines.append(f"- Clock/schedule side: `{impact['clock_and_schedule_side']}`.")
    lines.append(f"- Visible sun/lens/haze side: `{impact['visible_sun_lens_haze_side']}`.")
    if "effect_primitive_draw_packet_report" in impact:
        lines.append(f"- Primitive packet bridge report: `{impact['effect_primitive_draw_packet_report']}`.")
    lines.append("- Do not replace this with a generic light fallback.")
    lines.extend(["", "## Next Required Proof", ""])
    for item in report["next_required_proof"]:
        lines.append(f"- {item}")
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", type=Path, default=ANALYSIS / "kankyo_lens_quad_batch_draw.json")
    parser.add_argument("--markdown", type=Path, default=ANALYSIS / "kankyo_lens_quad_batch_draw.md")
    args = parser.parse_args()

    texts = load_sources()
    checks = verify_anchors(texts)
    report = build_report(checks)
    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.markdown.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    write_markdown(report, args.markdown)
    print(f"wrote {args.json}")
    print(f"wrote {args.markdown}")
    print(f"verified {len(checks)} native anchors")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
