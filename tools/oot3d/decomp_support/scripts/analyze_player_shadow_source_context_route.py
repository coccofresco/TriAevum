#!/usr/bin/env python3
"""Summarize the native player CMB/source-context route for Shadow2D analysis."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


FORMAT = "oot3d_player_shadow_source_context_route_v1"

DEFAULT_REPO_ROOT = Path(__file__).resolve().parents[4]
DEFAULT_ANALYSIS_ROOT = DEFAULT_REPO_ROOT / "tools" / "oot3d" / "decomp_support" / "analysis"
DEFAULT_GHIDRA_EXPORT = DEFAULT_REPO_ROOT / "tools" / "oot3d" / "decomp_support" / "ghidra_export"


FUNCTIONS = [
    {
        "id": "player_init",
        "address": "0x00191844",
        "path": DEFAULT_GHIDRA_EXPORT / "decompiled" / "99047_00191844_Player_Init.c",
        "checks": [
            ("stores_player_cmb", "*(undefined4 *)(param_1 + 0x24dc) = uVar6"),
            ("loads_native_cmb", "ZAR_GetCMBByIndex(psVar12 + 8,0)"),
            (
                "passes_cmb_to_player_init_common",
                "Player_InitCommon(param_1,param_2,*(undefined4 *)(param_1 + 0x24dc))",
            ),
            (
                "iterates_cmb_resource_count",
                "*(int *)(**(int **)(*(int *)(*(int *)(param_1 + 0x24dc) + 4) + 0xc) + 8)",
            ),
        ],
    },
    {
        "id": "player_init_common",
        "address": "0x00250768",
        "path": DEFAULT_ANALYSIS_ROOT
        / "shadow_route_focus_ghidra_export"
        / "decompiled"
        / "99018_00250768_Player_InitCommon.c",
        "checks": [
            ("processes_actor_init_chain", "Actor_ProcessInitChain(param_1,iVar1 + -0x1a8)"),
            (
                "passes_source_context_to_skelanime",
                "*(undefined4 *)(param_1 + 0x178),",
            ),
            ("passes_cmb_param_to_skelanime", "SkelAnime_InitLink(param_1 + 0x254"),
        ],
    },
    {
        "id": "skelanime_init_link",
        "address": "0x003413EC",
        "path": DEFAULT_ANALYSIS_ROOT
        / "shadow_route_focus_ghidra_export"
        / "decompiled"
        / "99071_003413ec_SkelAnime_InitLink.c",
        "checks": [
            ("stores_cmb_in_skelanime", "*param_1 = param_4"),
            ("sets_manager_override_context", "piVar4[2] = param_5"),
            ("calls_manager_factory_with_cmb", "(**(code **)(*piVar4 + 8))(piVar4,*param_1,1)"),
            ("clears_manager_override_context", "piVar4[2] = 0"),
        ],
    },
    {
        "id": "central_draw_object_factory",
        "address": "0x0034897C",
        "path": DEFAULT_GHIDRA_EXPORT / "decompiled" / "99263_0034897c_FUN_0034897c.c",
        "checks": [
            ("allocates_default_context_only_when_missing", "if (param_3 == (undefined4 *)0x0)"),
            ("allocates_autoclass1_default", "param_3 = (undefined4 *)AutoClass1()"),
            ("propagates_context_to_field_1dc", "*(undefined4 **)(iVar1 + 0x1dc) = param_3"),
            ("propagates_context_to_field_358", "*(undefined4 **)(iVar2 + 0x358) = param_3"),
        ],
        "negative_checks": [
            ("does_not_write_autoclass1_record_pointer", "+ 0x1a8"),
            ("does_not_write_autoclass1_record_pointer_word_index", "[0x6a]"),
            ("does_not_write_autoclass1_gate", "+ 0x1b5"),
        ],
    },
    {
        "id": "player_draw_shadow_clone",
        "address": "0x004BF618",
        "path": DEFAULT_ANALYSIS_ROOT
        / "shadow_route_focus_ghidra_export"
        / "decompiled"
        / "99174_004bf618_Player_Draw.c",
        "checks": [
            ("gates_shadow_object_on_player_flag", "& 0x4000000"),
            ("clones_source_context", "FUN_004c346c(iVar4,*(undefined4 *)((int)param_1 + 0x178))"),
            ("stores_cloned_context", "*(int *)((int)param_1 + 0x291c) = iVar5"),
            ("sets_manager_override_context", "piVar10[2] = iVar5"),
            (
                "creates_shadow_draw_object_from_player_cmb",
                "(**(code **)(*piVar10 + 8))(piVar10,*(undefined4 *)((int)param_1 + 0x24dc),1)",
            ),
            ("clears_manager_override_context", "piVar10[2] = 0"),
            ("submits_shadow_draw_object", "FUN_00372170(*(undefined4 *)((int)param_1 + 0x2918),0)"),
        ],
    },
    {
        "id": "player_context_clone_wrapper",
        "address": "0x004C346C",
        "path": DEFAULT_ANALYSIS_ROOT
        / "shadow_route_focus_ghidra_export"
        / "decompiled"
        / "99180_004c346c_FUN_004c346c.c",
        "checks": [
            ("initializes_three_subrecords", "FUN_00350820(param_1 + 0x88"),
            ("copies_source_context", "FUN_00310f7c(iVar1 + -0x88,param_2)"),
        ],
    },
    {
        "id": "render_context_copy",
        "address": "0x00310F7C",
        "path": DEFAULT_ANALYSIS_ROOT
        / "shadow_route_focus_ghidra_export"
        / "decompiled"
        / "99043_00310f7c_FUN_00310f7c.c",
        "checks": [
            ("copies_record_pointer_word", "param_1[0x6a] = param_2[0x6a]"),
            ("copies_shadow_gate_byte", "0x1b5) = *(undefined1 *)((int)param_2 + 0x1b5)"),
            ("copies_texcoord_byte", "0x1b6) = *(undefined1 *)((int)param_2 + 0x1b6)"),
            ("copies_model_route_byte", "0x1b7) = *(undefined1 *)((int)param_2 + 0x1b7)"),
        ],
    },
]


def find_lines(path: Path, pattern: str) -> list[dict[str, Any]]:
    if not path.exists():
        return []
    matches = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        if pattern in line:
            matches.append({"line": line_number, "text": line.strip()})
    return matches


def analyze_function(function: dict[str, Any], repo_root: Path) -> dict[str, Any]:
    path = Path(function["path"])
    relative_path = path
    try:
        relative_path = path.relative_to(repo_root)
    except ValueError:
        pass

    checks = []
    for check_id, pattern in function.get("checks", []):
        matches = find_lines(path, pattern)
        checks.append(
            {
                "id": check_id,
                "pattern": pattern,
                "present": bool(matches),
                "matches": matches,
            }
        )

    negative_checks = []
    for check_id, pattern in function.get("negative_checks", []):
        matches = find_lines(path, pattern)
        negative_checks.append(
            {
                "id": check_id,
                "pattern": pattern,
                "absent": not bool(matches),
                "matches": matches,
            }
        )

    return {
        "id": function["id"],
        "address": function["address"],
        "path": str(relative_path),
        "exists": path.exists(),
        "checks": checks,
        "negative_checks": negative_checks,
        "all_positive_checks_present": all(check["present"] for check in checks),
        "all_negative_checks_absent": all(check["absent"] for check in negative_checks),
    }


def build_summary(repo_root: Path) -> dict[str, Any]:
    functions = [analyze_function(function, repo_root) for function in FUNCTIONS]
    all_positive = all(item["all_positive_checks_present"] for item in functions)
    all_negative = all(item["all_negative_checks_absent"] for item in functions)

    return {
        "format": FORMAT,
        "source_kind": "ghidra_decompiled_static_route_audit",
        "source_policy": "static_code_evidence_only_emulator_traces_are_validation_only",
        "repo_root": str(repo_root),
        "functions": functions,
        "conclusions": {
            "player_cmb_route_to_source_context_resolved": all_positive,
            "central_factory_is_context_propagation_not_record_pointer_producer": (
                functions[3]["all_positive_checks_present"]
                and functions[3]["all_negative_checks_absent"]
            ),
            "player_draw_clone_copies_existing_record_pointer_not_owner": (
                functions[4]["all_positive_checks_present"]
                and functions[5]["all_positive_checks_present"]
                and functions[6]["all_positive_checks_present"]
            ),
            "auto_class1_record_pointer_owner_resolved": False,
            "next_target": (
                "Find the native CMB/resource/material binding or indirect bulk-copy route that "
                "populates source context AutoClass1+0x1A8 before SkelAnime/Player_Draw clone "
                "consumers read it."
            ),
        },
    }


def render_markdown(summary: dict[str, Any]) -> str:
    lines = [
        "# Player Shadow Source Context Route",
        "",
        "This report audits the native player CMB and source-context route from Ghidra decompiled code. It uses static OOT3D code evidence only; emulator traces remain validation-only.",
        "",
        "## Conclusions",
        "",
    ]
    for key, value in summary["conclusions"].items():
        lines.append(f"- `{key}`: `{value}`")
    lines.extend(["", "## Evidence", ""])

    for function in summary["functions"]:
        lines.append(f"### {function['id']} {function['address']}")
        lines.append(f"- Path: `{function['path']}`")
        lines.append(f"- Positive checks present: `{function['all_positive_checks_present']}`")
        if function["negative_checks"]:
            lines.append(f"- Negative checks absent: `{function['all_negative_checks_absent']}`")
        for check in function["checks"]:
            lines.append(f"- `{check['id']}`: `{check['present']}`")
            for match in check["matches"][:3]:
                lines.append(f"  - line `{match['line']}`: `{match['text']}`")
        for check in function["negative_checks"]:
            lines.append(f"- `{check['id']}` absent: `{check['absent']}`")
            for match in check["matches"][:3]:
                lines.append(f"  - line `{match['line']}`: `{match['text']}`")
        lines.append("")

    lines.extend(
        [
            "## Boundary",
            "",
            "The source context route is now bounded: `Player_Init` resolves `player+0x24DC` from the native CMB, `Player_InitCommon` passes that CMB and `player+0x178` to `SkelAnime_InitLink`, the central factory propagates the override context to draw-object fields, and `Player_Draw` later clones the source context before making the Shadow2D draw object from the same CMB. The clone helper copies `AutoClass1+0x1A8/+0x1B5/+0x1B6/+0x1B7`; it does not create those values.",
            "",
            "The remaining owner must therefore be an earlier CMB/resource/material binding or indirect copy that mutates the source `AutoClass1` before these consumers run.",
            "",
        ]
    )
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, default=DEFAULT_REPO_ROOT)
    parser.add_argument("--output-json", type=Path, required=True)
    parser.add_argument("--output-md", type=Path, required=True)
    args = parser.parse_args()

    summary = build_summary(args.repo_root)
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    args.output_md.write_text(render_markdown(summary), encoding="utf-8")
    print(
        json.dumps(
            {
                "status": "ok",
                "output_json": str(args.output_json),
                "output_md": str(args.output_md),
                "player_cmb_route_to_source_context_resolved": summary["conclusions"][
                    "player_cmb_route_to_source_context_resolved"
                ],
                "auto_class1_record_pointer_owner_resolved": summary["conclusions"][
                    "auto_class1_record_pointer_owner_resolved"
                ],
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
