#!/usr/bin/env python3
"""Verify the native OOT3D kankyo/lens runtime enqueue chain.

This analyzer follows the code.bin chain used by the title-intro lens/haze path:
FUN_002D97E4 computes the 13 lens element positions, FUN_00484F5C dispatches
active/target group rows, FUN_002C5314 selects runtime objects, and FUN_00332A14
materializes the queued runtime buffers consumed by the 0x28C quad-batch backend.
"""

from __future__ import annotations

import argparse
import json
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[4]
ANALYSIS = ROOT / "tools" / "oot3d" / "decomp_support" / "analysis"
EXPORT = ANALYSIS / "kankyo_lens_enqueue_ghidra_export" / "decompiled"
OPERATIONAL_INPUTS = ROOT / "tools" / "oot3d" / "operational_inputs.json"
CODE_BASE = 0x00100000


@dataclass(frozen=True)
class SourceFile:
    label: str
    address: str
    path: Path


SOURCES = {
    "consumer": SourceFile("lens_tbd_consumer", "0x002D97E4", EXPORT / "99022_002d97e4_FUN_002d97e4.c"),
    "projection": SourceFile("lens_projection_helper", "0x00484DB4", EXPORT / "99038_00484db4_FUN_00484db4.c"),
    "view_projection": SourceFile("play_view_projection_helper", "0x00368CC0", EXPORT / "99027_00368cc0_FUN_00368cc0.c"),
    "draw": SourceFile("lens_draw_dispatch", "0x00484F5C", EXPORT / "99039_00484f5c_FUN_00484f5c.c"),
    "helper": SourceFile("lens_draw_helper", "0x002C5314", EXPORT / "99019_002c5314_FUN_002c5314.c"),
    "transform": SourceFile("quad_object_transform_enqueue", "0x00371F1C", EXPORT / "99029_00371f1c_FUN_00371f1c.c"),
    "enqueue": SourceFile("runtime_queue_writer", "0x00332A14", EXPORT / "99023_00332a14_FUN_00332a14.c"),
    "submit_queue": SourceFile("runtime_submit_queue", "0x002C517C", EXPORT / "99018_002c517c_FUN_002c517c.c"),
    "submit_record": SourceFile("runtime_submit_record_writer", "0x002C1AE8", EXPORT / "99017_002c1ae8_FUN_002c1ae8.c"),
}


ANCHORS: dict[str, list[tuple[str, str]]] = {
    "consumer": [
        ("lens_loop_has_thirteen_elements", "while (iVar4 < 0xd);"),
        ("lens_position_x_uses_screen_origin_and_offset", "local_b4 = local_90 + (float)((ulonglong)lVar20 >> 0x20) * fVar12;"),
        ("lens_position_y_uses_screen_origin_and_offset", "local_b0 = local_8c + (float)uVar21 * fVar12;"),
        ("lens_depth_from_lens_depth_table", "local_ac = *(float *)(iVar3 + iVar4 * 4);"),
        ("lens_dispatch_uses_kankyo_list_offset_0ec", "FUN_00484f5c(param_2 + 0xec,&local_b4,iVar4);"),
    ],
    "projection": [
        ("projection_calls_play_view_projection_helper", "FUN_00368cc0();"),
        ("projection_x_uses_screen_center_and_scale", "DAT_00484e24 +\n                                                      *param_3 * param_4 * DAT_00484e24"),
        ("projection_y_uses_base_and_negative_scale", "DAT_00484e2c + param_3[1] * param_4 * fVar2"),
    ],
    "view_projection": [
        ("view_projection_matrix_row0_play_offset_0x5bb4", "*(float *)(param_1 + 0x5bb4)"),
        ("view_projection_matrix_row1_play_offset_0x5bc4", "*(float *)(param_1 + 0x5bc4)"),
        ("view_projection_matrix_row2_play_offset_0x5bd4", "*(float *)(param_1 + 0x5bd4)"),
        ("view_projection_matrix_w_row_play_offset_0x5be4", "*(float *)(param_1 + 0x5be4)"),
        ("view_projection_reciprocal_w_gate", "if ((int)fVar6 < 0x3f800000)"),
    ],
    "draw": [
        ("active_group_slot_loaded_from_0x04", "iVar1 = *(int *)(param_1 + 4);"),
        ("active_and_target_group_compare", "if (iVar1 == *(int *)(param_1 + 8))"),
        ("matching_groups_dispatch_once", "FUN_002c5314(param_1,param_2,iVar1,0,param_3);"),
        ("different_groups_dispatch_active_row", "FUN_002c5314(param_1,param_2,iVar1,0,param_3);"),
        ("different_groups_dispatch_target_row", "FUN_002c5314(param_1,param_2,*(undefined4 *)(param_1 + 8),1,param_3);"),
    ],
    "helper": [
        ("element_0x0c_uses_terminal_object_slot", "if (param_5 == 0xc)"),
        ("element_0x0c_reads_row_terminal_draw_object", "uVar5 = param_1[param_3 + 0x2c];"),
        ("element_0x0c_queues_terminal_base_0x14", "FUN_002c517c(uVar2,param_1[param_3 + 0x2c],param_4 + 0x14);"),
        ("regular_path_reads_row_primary_draw_object", "uVar10 = param_1[param_3 + 0x28];"),
        ("regular_path_enqueues_object_transform", "FUN_00371f1c(uVar10,param_2,0,&local_3c,0,0);"),
        ("element_0x0b_special_queue", "if (param_5 == 0xb)"),
        ("element_0x0b_queues_primary_base_0x12", "FUN_002c517c(uVar2,uVar10,param_4 + 0x12);"),
        ("visible_flag_written_for_special_runtime", "*(uint *)(uVar10 + 0x178) = *(uint *)(uVar10 + 0x178) | 0x10;"),
    ],
    "transform": [
        ("replicates_u32x4_for_four_corners", "while (iVar3 < 4);"),
        ("calls_runtime_queue_writer", "FUN_00332a14(param_1,param_2,param_3,param_4,local_64,param_6);"),
    ],
    "enqueue": [
        ("capacity_gate_offset_0x1f8", "if (*(int *)(param_1 + 0x1f8) <= iVar1)"),
        ("position_array_offset_0x1e4", "*(int *)(param_1 + 0x1e4) + iVar1 * 0xc"),
        ("matrix_array_offset_0x1e8", "iVar4 = *(int *)(param_1 + 0x1e8);"),
        ("matrix_stride_0x30", "iVar4 + iVar1 * 0x30"),
        ("local_vector_array_offset_0x1ec", "*(int *)(param_1 + 0x1ec) + *(int *)(param_1 + 0x1fc) * 0xc"),
        ("optional_u32x4_offset_0x1f0", "*(int *)(param_1 + 0x1f0) + *(int *)(param_1 + 0x1fc) * 0x40"),
        ("optional_float2_offset_0x1f4_decimal_500", "iVar1 = *(int *)(param_1 + 500);"),
        ("batch_count_increment_offset_0x1fc", "*(int *)(param_1 + 0x1fc) = *(int *)(param_1 + 0x1fc) + 1;"),
    ],
    "submit_queue": [
        ("submit_record_stride_8", "FUN_002c1ae8(param_1,param_1 + param_3 * 8 + 4,param_2);"),
        ("queue_index_split_at_0x12", "if (param_3 < 0x12)"),
        ("runtime_vtable_draw_slot_invoked", "(**(code **)(*param_2 + 8))"),
    ],
    "submit_record": [
        ("record_stores_runtime_pointer", "*param_2 = param_3;"),
        ("record_marks_active_byte", "*(undefined1 *)(param_2 + 1) = 1;"),
    ],
}


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


def resolve_operational_path(operational_inputs: dict[str, Any], entry_id: str) -> Path:
    variables = operational_inputs.get("variables", {})
    for entry in operational_inputs.get("entries", []):
        if entry.get("id") != entry_id:
            continue
        path = entry.get("path", "")
        for key, value in variables.items():
            path = path.replace("${" + key + "}", value)
        return Path(path)
    raise KeyError(f"missing operational input entry: {entry_id}")


def runtime_offset(runtime_address: int, code_size: int) -> int:
    offset = runtime_address - CODE_BASE
    if runtime_address < CODE_BASE or offset < 0 or offset + 4 > code_size:
        raise ValueError(f"runtime address out of code.bin range: 0x{runtime_address:08x}")
    return offset


def read_codebin_float_literals() -> dict[str, Any]:
    operational_inputs = json.loads(OPERATIONAL_INPUTS.read_text(encoding="utf-8"))
    code_path = resolve_operational_path(operational_inputs, "oot3d_code_bin")
    code = code_path.read_bytes()
    literal_addresses = {
        "screen_center_x": 0x002D9B9C,
        "screen_center_y": 0x002D9BA0,
        "base_z": 0x002D9BA4,
        "distance_limit_scale": 0x002D9BCC,
        "direction_scale": 0x002D9BD0,
        "projection_scale_x": 0x00484E24,
        "projection_scale_y": 0x00484E28,
        "projection_base_y": 0x00484E2C,
        "view_projection_identity_one": 0x00368D90,
    }
    values = {}
    for name, address in literal_addresses.items():
        offset = runtime_offset(address, len(code))
        raw = code[offset : offset + 4]
        values[name] = {
            "address": f"0x{address:08X}",
            "u32": f"0x{struct.unpack('<I', raw)[0]:08X}",
            "f32": struct.unpack("<f", raw)[0],
        }
    return {
        "code_bin": str(code_path),
        "code_base": f"0x{CODE_BASE:08X}",
        "values": values,
    }


def build_report(checks: list[dict[str, Any]], codebin_literals: dict[str, Any]) -> dict[str, Any]:
    return {
        "report": "kankyo_lens_enqueue_runtime_queue",
        "source": "OOT3D code.bin Ghidra decompile",
        "source_files": {
            key: {
                "label": source.label,
                "function_address": source.address,
                "path": str(source.path.relative_to(ROOT)),
            }
            for key, source in SOURCES.items()
        },
        "anchor_check_count": len(checks),
        "all_anchors_found": all(check["found"] for check in checks),
        "checks": checks,
        "resolved_contract_values": {
            "kankyo_list_offset": "0x0EC",
            "lensflare_runtime_element_count": 13,
            "active_group_word_offset": "0x04",
            "target_group_word_offset": "0x08",
            "special_submit_element_range": "0x0B..0x0C",
            "primary_draw_object_word_index_base": "0x28",
            "terminal_draw_object_word_index_base": "0x2C",
            "primary_submit_queue_index_base": "0x12",
            "terminal_submit_queue_index_base": "0x14",
            "runtime_batch_capacity_offset": "0x1F8",
            "runtime_batch_count_offset": "0x1FC",
            "runtime_position_array_pointer_offset": "0x1E4",
            "runtime_matrix_array_pointer_offset": "0x1E8",
            "runtime_local_vector_array_pointer_offset": "0x1EC",
            "runtime_u32x4_array_pointer_offset": "0x1F0",
            "runtime_float2_array_pointer_offset": "0x1F4",
            "position_view_projection_matrix_play_offset": "0x5BB4",
        },
        "codebin_position_literals": codebin_literals,
        "native_algorithm": [
            "Read LensScaleMin/LensScaleMax/LensOffset/LensDepth/LensHalationColor records from the Lens* TBD set.",
            "For each of 13 lens elements, compute the queued center and depth from current screen/camera state.",
            "Dispatch through the active group row once when active and target group are equal.",
            "Dispatch through both active and target group rows when the rows differ.",
            "For element 11, enqueue through the primary-object slot and queue bases 0x12/0x13 when both rows are active.",
            "For element 12, enqueue through the terminal-object slot and queue bases 0x14/0x15 when both rows are active.",
            "FUN_00371F1C replicates the u32x4 record across four corners before FUN_00332A14 writes the runtime queue.",
            "FUN_00332A14 writes position, matrix, local vector, optional u32x4, optional float2, then increments count at +0x1FC.",
            "FUN_00484DB4 projects the source vector into screen-space with code.bin literal center/scale values.",
            "FUN_00368CC0 consumes the play-state view/projection block at play+0x5BB4 before exact centers can be materialized.",
        ],
        "engine_impact": {
            "contract_fields_added": [
                "NativeKankyoLensflareRuntimeListContract.SpecialSubmitElementStartIndex",
                "NativeKankyoLensflareRuntimeListContract.SpecialSubmitElementEndIndex",
                "NativeKankyoLensflareRuntimeListContract.LensflareRuntimeElementCount",
                "NativeKankyoRuntime28CParticleBatchContract.RuntimeBatchCapacityOffset",
            "NativeKankyoRuntime28CParticleBatchContract.RuntimeLocalVectorArrayPointerOffset",
            "Oot3dNativeKankyoPrimitiveBackendInputState.Runtime28CEnqueueDispatchResolved",
            "Oot3dNativeKankyoPrimitiveBackendInputState.Runtime28CPositionProducerResolved",
        ],
            "visible_backend_render": "requires materialized play+0x5bb4 view/projection input, then the OpenGL consumer of the native queued lanes and primitive packet",
        },
    }


def write_markdown(report: dict[str, Any], path: Path) -> None:
    lines = [
        "# Kankyo Lens Enqueue Runtime Queue",
        "",
        "This report verifies the native queueing chain for the OOT3D kankyo lens/haze runtime path.",
        "",
        "## Resolved Values",
        "",
    ]
    for key, value in report["resolved_contract_values"].items():
        lines.append(f"- `{key}`: `{value}`.")
    lines.extend(["", "## Code.bin Position Literals", ""])
    for key, value in report["codebin_position_literals"]["values"].items():
        lines.append(
            f"- `{key}`: `{value['f32']}` from `{value['address']}` (`{value['u32']}`)."
        )
    lines.extend(["", "## Native Algorithm", ""])
    for item in report["native_algorithm"]:
        lines.append(f"- {item}")
    lines.extend(["", "## Engine Impact", ""])
    impact = report["engine_impact"]
    lines.append("- Added/validated contract fields:")
    for item in impact["contract_fields_added"]:
        lines.append(f"  - `{item}`")
    lines.append(f"- Visible backend render: `{impact['visible_backend_render']}`.")
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", type=Path, default=ANALYSIS / "kankyo_lens_enqueue_runtime_queue.json")
    parser.add_argument("--markdown", type=Path, default=ANALYSIS / "kankyo_lens_enqueue_runtime_queue.md")
    args = parser.parse_args()

    texts = load_sources()
    checks = verify_anchors(texts)
    codebin_literals = read_codebin_float_literals()
    report = build_report(checks, codebin_literals)
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
