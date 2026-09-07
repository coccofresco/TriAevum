#!/usr/bin/env python3
"""Verify the native OOT3D play+0x5BB4 view/projection source chain.

The kankyo lens producer projects screen centers through FUN_00368CC0. This
analyzer proves that the matrix read by that helper is built by Gameplay_Draw
from the native play-state matrix blocks instead of from an emulator dump.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[4]
ANALYSIS = ROOT / "tools" / "oot3d" / "decomp_support" / "analysis"
SHADOW_EXPORT = ANALYSIS / "shadow_depth_flush_ghidra_export"
LENS_EXPORT = ANALYSIS / "kankyo_lens_enqueue_ghidra_export"


@dataclass(frozen=True)
class SourceFile:
    label: str
    address: str
    path: Path


SOURCES = {
    "gameplay_draw": SourceFile(
        "Gameplay_Draw",
        "0x002E25F0",
        SHADOW_EXPORT / "decompiled" / "99020_002e25f0_Gameplay_Draw.c",
    ),
    "matrix_compose": SourceFile(
        "FUN_002D9688_matrix_compose",
        "0x002D9688",
        SHADOW_EXPORT / "decompiled" / "99015_002d9688_FUN_002d9688.c",
    ),
    "view_projection_consumer": SourceFile(
        "FUN_00368CC0_play_view_projection_consumer",
        "0x00368CC0",
        LENS_EXPORT / "decompiled" / "99027_00368cc0_FUN_00368cc0.c",
    ),
    "lens_producer": SourceFile(
        "FUN_002D97E4_kankyo_lens_position_producer",
        "0x002D97E4",
        LENS_EXPORT / "decompiled" / "99022_002d97e4_FUN_002d97e4.c",
    ),
    "lens_projection": SourceFile(
        "FUN_00484DB4_lens_projection_helper",
        "0x00484DB4",
        LENS_EXPORT / "decompiled" / "99038_00484db4_FUN_00484db4.c",
    ),
    "gameplay_disassembly": SourceFile(
        "Gameplay_Draw_disassembly",
        "0x002E25F0",
        SHADOW_EXPORT / "disassembly_selected.txt",
    ),
    "lens_disassembly": SourceFile(
        "Lens_projection_disassembly",
        "0x00484DB4",
        LENS_EXPORT / "disassembly_selected.txt",
    ),
}


ANCHORS: dict[str, list[tuple[str, str]]] = {
    "gameplay_draw": [
        ("view_source_producer_writes_play_0x1dc", "FUN_00464b2c(param_1[0x17f2],param_1 + 0x77,"),
        ("main_view_state_prepared_at_play_0x188", "FUN_002de690(param_1 + 0x62);"),
        ("main_view_updated_by_native_view_builder", "FUN_00471ba4(param_1 + 0x62,0xf);"),
        ("destination_is_play_0x5bb4", "puVar5 = param_1 + 0x16ed;"),
        ("source_4x4_copied_from_play_0x1dc", "oot3d_copy_u32x16_if_distinct(puVar5,param_1 + 0x77);"),
        ("compose_matrix_copied_from_play_0x29c", "oot3d_copy_u32x12_if_distinct(&uStack_84,param_1 + 0xa7);"),
        ("destination_composed_in_place", "FUN_002d9688(puVar5,puVar5,&uStack_84);"),
    ],
    "matrix_compose": [
        ("compose_is_4x4_row0", "*param_1 = fVar17 * fVar1 + fVar19 * fVar9 + fVar21 * fVar2 + fVar23 * fVar10;"),
        ("compose_is_4x4_row3", "param_1[0xf] = fVar26 * fVar7 + fVar28 * fVar15 + fVar30 * fVar8 + fVar32 * fVar16;"),
    ],
    "view_projection_consumer": [
        ("consumer_reads_play_0x5bb4_row0", "*(float *)(param_1 + 0x5bb4)"),
        ("consumer_reads_play_0x5bc4_row1", "*(float *)(param_1 + 0x5bc4)"),
        ("consumer_reads_play_0x5bd4_row2", "*(float *)(param_1 + 0x5bd4)"),
        ("consumer_reads_play_0x5be4_w_row", "*(float *)(param_1 + 0x5be4)"),
        ("consumer_applies_reciprocal_w_gate", "if ((int)fVar6 < 0x3f800000)"),
    ],
    "lens_producer": [
        ("lens_producer_projects_through_00484db4", "FUN_00484db4(param_1,auStack_44,&local_90);"),
        ("lens_runtime_enqueue_uses_projected_positions", "FUN_00484f5c(param_2 + 0xec,&local_b4,iVar4);"),
    ],
    "lens_projection": [
        ("projection_helper_calls_00368cc0", "FUN_00368cc0();"),
    ],
    "gameplay_disassembly": [
        ("callsite_view_source_producer", "002e2674: bl 0x00464b2c"),
        ("callsite_view_prep", "002e2688: bl 0x002de690"),
        ("callsite_view_update", "002e2694: bl 0x00471ba4"),
        ("callsite_copy_u32x16_play_0x1dc_to_0x5bb4", "002e26a8: bl 0x00324744"),
        ("callsite_copy_u32x12_play_0x29c_to_stack", "002e26b4: bl 0x00372224"),
        ("identity_row_patch_before_compose", "002e26dc: vstr.32 s0,[sp,#0x1e8]"),
        ("callsite_compose_to_play_0x5bb4", "002e26e0: bl 0x002d9688"),
    ],
    "lens_disassembly": [
        ("00368cc0_adds_0x5800", "00368cc0: add r0,r0,#0x5800"),
        ("00368cc0_adds_0x3b4", "00368cc4: add r0,r0,#0x3b4"),
        ("00484db4_passes_param0_to_00368cc0", "00484dc0: bl 0x00368cc0"),
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
    for key, anchors in ANCHORS.items():
        text = texts[key]
        for label, needle in anchors:
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
    return {
        "report": "kankyo_lens_view_projection_matrix",
        "source": "OOT3D code.bin Ghidra decompile/disassembly",
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
            "gameplay_draw_function_address": "0x002E25F0",
            "view_source_producer_function_address": "0x00464B2C",
            "view_source_producer_callsite_address": "0x002E2674",
            "view_prep_function_address": "0x002DE690",
            "view_prep_callsite_address": "0x002E2688",
            "view_update_function_address": "0x00471BA4",
            "view_update_callsite_address": "0x002E2694",
            "source_copy_u32x16_helper_address": "0x00324744",
            "source_copy_u32x16_callsite_address": "0x002E26A8",
            "compose_copy_u32x12_helper_address": "0x00372224",
            "compose_copy_u32x12_callsite_address": "0x002E26B4",
            "compose_helper_address": "0x002D9688",
            "compose_callsite_address": "0x002E26E0",
            "view_struct_play_offset": "0x0188",
            "view_projection_source_matrix_play_offset": "0x01DC",
            "view_projection_compose_matrix_play_offset": "0x029C",
            "view_projection_destination_matrix_play_offset": "0x5BB4",
            "view_projection_matrix_word_count": 16,
            "view_projection_compose_matrix_native_word_count": 12,
            "identity_row_patched_before_compose": True,
        },
        "native_algorithm": [
            "Gameplay_Draw calls FUN_00464B2C to write a 16-word source matrix at play+0x1DC.",
            "Gameplay_Draw prepares and updates the main View struct at play+0x188 through FUN_002DE690 and FUN_00471BA4.",
            "Gameplay_Draw copies play+0x1DC to play+0x5BB4 with FUN_00324744.",
            "Gameplay_Draw copies the 12-word play+0x29C matrix payload to stack with FUN_00372224.",
            "Gameplay_Draw patches the stack matrix identity row before composition.",
            "FUN_002D9688 composes play+0x5BB4 in place from the copied source and stack matrix.",
            "FUN_00368CC0 consumes the resulting play+0x5BB4 block for screen projection and reciprocal W.",
            "FUN_002D97E4 feeds that projection into the 13-element lens queue before FUN_00484F5C enqueues runtime draw lanes.",
        ],
        "engine_impact": {
            "position_producer_constants_resolved": True,
            "view_projection_source_chain_resolved": True,
            "runtime_matrix_values_materialized_in_host": False,
            "remaining_blocker": "host must materialize the current-frame native camera matrices at play+0x1DC/play+0x29C and compose play+0x5BB4 before emitting exact lens centers",
        },
    }


def write_markdown(report: dict[str, Any], path: Path) -> None:
    lines = [
        "# Kankyo Lens View Projection Matrix",
        "",
        "This report verifies the native source chain for the `play+0x5BB4` matrix consumed by the OOT3D kankyo lens projection helper.",
        "",
        "## Resolved Values",
        "",
    ]
    for key, value in report["resolved_contract_values"].items():
        lines.append(f"- `{key}`: `{value}`.")
    lines.extend(["", "## Native Algorithm", ""])
    for item in report["native_algorithm"]:
        lines.append(f"- {item}")
    lines.extend(["", "## Engine Impact", ""])
    impact = report["engine_impact"]
    lines.append(f"- Position producer constants resolved: `{impact['position_producer_constants_resolved']}`.")
    lines.append(f"- View/projection source chain resolved: `{impact['view_projection_source_chain_resolved']}`.")
    lines.append(f"- Runtime matrix values materialized in host: `{impact['runtime_matrix_values_materialized_in_host']}`.")
    lines.append(f"- Remaining blocker: `{impact['remaining_blocker']}`.")
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", type=Path, default=ANALYSIS / "kankyo_lens_view_projection_matrix.json")
    parser.add_argument("--markdown", type=Path, default=ANALYSIS / "kankyo_lens_view_projection_matrix.md")
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
