#!/usr/bin/env python3
"""Map reusable N64 OoT structure layout names onto observed OOT3D offsets."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
DEFAULT_N64_ROOT = ROOT.parent / "external" / "oot"

FIELD_RE = re.compile(
    r"/\*\s*0x(?P<offset>[0-9A-Fa-f]+)\s*\*/\s*(?P<decl>[^;\n]+?)(?:;|//)",
)

OOT3D_PLAYSTATE_OBSERVED = [
    {
        "offset": 0x0000,
        "name": "state",
        "n64_name": "state",
        "role": "game-state base",
        "evidence": "PlayState is passed as the first parameter to the main init/update routines; N64 PlayState also starts with GameState.",
        "confidence": "medium",
    },
    {
        "offset": 0x00A4,
        "name": "sceneId",
        "n64_name": "sceneId",
        "role": "scene identifier",
        "evidence": "FUN_00416608 initializes graphics/resources from PlayState+0xa4; this offset exactly matches N64 PlayState.sceneId.",
        "confidence": "medium",
    },
    {
        "offset": 0x0180,
        "name": "view_or_camera_subsystem",
        "n64_name": "view/mainCamera area",
        "role": "render/camera-adjacent subsystem",
        "evidence": "FUN_00300328 and FUN_00416608 repeatedly initialize/update PlayState+0x180; N64 has View/Camera state near the early PlayState block but not at this exact offset.",
        "confidence": "low",
    },
    {
        "offset": 0x22F0,
        "name": "message_adjacent_22f0",
        "n64_name": None,
        "role": "message-adjacent initialized subsystem",
        "evidence": "Initialized in FUN_00416608 and updated in FUN_00300328 alongside messageCtx.",
        "confidence": "medium",
    },
    {
        "offset": 0x23C8,
        "name": "message_adjacent_23c8",
        "n64_name": None,
        "role": "message-adjacent initialized subsystem",
        "evidence": "Initialized in FUN_00416608 and updated in FUN_00300328 alongside messageCtx.",
        "confidence": "medium",
    },
    {
        "offset": 0x25F0,
        "name": "message_adjacent_25f0",
        "n64_name": None,
        "role": "message-adjacent per-frame subsystem",
        "evidence": "Used as local_30 in FUN_00300328 and initialized in FUN_00416608.",
        "confidence": "medium",
    },
    {
        "offset": 0x32C0,
        "name": "msgCtx",
        "n64_name": "msgCtx",
        "role": "message context",
        "evidence": "FUN_00416608 calls oot3d_load_message_resources(PlayState+0x32c0); FUN_00300328 calls oot3d_message_context_update(PlayState+0x32c0).",
        "confidence": "high",
    },
    {
        "offset": 0x4290,
        "name": "message_adjacent_4290",
        "n64_name": None,
        "role": "message-adjacent update subsystem",
        "evidence": "Initialized in FUN_00416608 and updated in FUN_00300328 near message update.",
        "confidence": "medium",
    },
    {
        "offset": 0x59A0,
        "name": "message_adjacent_59a0",
        "n64_name": None,
        "role": "message-adjacent update subsystem",
        "evidence": "Initialized in FUN_00416608 and updated in FUN_00300328 near message update.",
        "confidence": "medium",
    },
    {
        "offset": 0x72D4,
        "name": "message_resource_handle_72d4",
        "n64_name": None,
        "role": "message-adjacent resource handle",
        "evidence": "Written from a resource lookup in FUN_00416608 and consumed in FUN_00300328.",
        "confidence": "medium",
    },
    {
        "offset": 0x7358,
        "name": "message_adjacent_7358",
        "n64_name": None,
        "role": "message-adjacent initialized subsystem",
        "evidence": "Initialized in FUN_00416608 and reset/updated in FUN_00300328.",
        "confidence": "medium",
    },
    {
        "offset": 0x7440,
        "name": "message_callback_7440",
        "n64_name": None,
        "role": "message-adjacent callback slot",
        "evidence": "FUN_00416608 clears this slot and FUN_00300328 calls it when non-null.",
        "confidence": "medium",
    },
]

OOT3D_MESSAGECTX_OBSERVED = [
    {
        "offset": 0x000C,
        "name": "active_or_initialized_flag_c",
        "n64_name": None,
        "role": "message-context gate flag",
        "evidence": "oot3d_message_context_update checks byte +0xc before reading deeper state.",
        "confidence": "low",
    },
    {
        "offset": 0x000D,
        "name": "secondary_active_flag_d",
        "n64_name": None,
        "role": "message-context secondary gate flag",
        "evidence": "oot3d_message_context_update checks byte +0xd before testing byte +0xe.",
        "confidence": "low",
    },
    {
        "offset": 0x000E,
        "name": "secondary_state_e",
        "n64_name": None,
        "role": "message-context secondary state",
        "evidence": "oot3d_message_context_update requires byte +0xe to be nonzero under the secondary gate.",
        "confidence": "low",
    },
    {
        "offset": 0x0048,
        "name": "resource_array_enabled_48",
        "n64_name": None,
        "role": "message resource update gate",
        "evidence": "When byte +0x48 is set, oot3d_message_context_update iterates 0x100 pointers at +0x568.",
        "confidence": "medium",
    },
    {
        "offset": 0x0568,
        "name": "resource_slots_568",
        "n64_name": None,
        "role": "message resource pointer array",
        "evidence": "oot3d_message_context_update iterates 0x100 words from +0x568 and updates non-null entries.",
        "confidence": "medium",
    },
    {
        "offset": 0x0974,
        "name": "resource_close_arg_974",
        "n64_name": None,
        "role": "message resource close argument",
        "evidence": "Passed to FUN_002f780c when signed word +0x978 is not -1.",
        "confidence": "low",
    },
    {
        "offset": 0x0978,
        "name": "resource_close_index_978",
        "n64_name": None,
        "role": "message resource close index",
        "evidence": "Compared against -1 before final resource close path in oot3d_message_context_update.",
        "confidence": "low",
    },
    {
        "offset": 0x0F38,
        "name": "primary_state_f38",
        "n64_name": None,
        "role": "message-context primary state",
        "evidence": "oot3d_message_context_update excludes states 0, 1, 0xe, and 0xf before resource update work.",
        "confidence": "medium",
    },
]


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        try:
            return str(path.relative_to(ROOT.parent)).replace("\\", "/")
        except ValueError:
            return str(path).replace("\\", "/")


def parse_n64_fields(path: Path, struct_name: str) -> list[dict[str, object]]:
    text = path.read_text(encoding="utf-8", errors="replace")
    marker = f"typedef struct {struct_name}"
    start = text.find(marker)
    if start < 0:
        raise SystemExit(f"{struct_name} not found in {path}")
    end = text.find(f"}} {struct_name}", start)
    if end < 0:
        raise SystemExit(f"{struct_name} end not found in {path}")
    body = text[start:end]
    fields = []
    for match in FIELD_RE.finditer(body):
        decl = " ".join(match.group("decl").split())
        name_match = re.search(r"([A-Za-z_][A-Za-z0-9_]*)(?:\[[^\]]+\])?$", decl)
        fields.append(
            {
                "offset": int(match.group("offset"), 16),
                "decl": decl,
                "name": name_match.group(1) if name_match else decl,
            }
        )
    return fields


def n64_field_by_name(fields: list[dict[str, object]]) -> dict[str, dict[str, object]]:
    return {str(field["name"]): field for field in fields}


def attach_n64_reference(rows: list[dict[str, object]], n64_fields: list[dict[str, object]]) -> list[dict[str, object]]:
    by_name = n64_field_by_name(n64_fields)
    out = []
    for row in rows:
        enriched = dict(row)
        n64_name = row.get("n64_name")
        if n64_name and n64_name in by_name:
            n64_field = by_name[str(n64_name)]
            enriched["n64_offset"] = n64_field["offset"]
            enriched["n64_decl"] = n64_field["decl"]
            enriched["delta_from_n64"] = int(row["offset"]) - int(n64_field["offset"])
        else:
            enriched["n64_offset"] = None
            enriched["n64_decl"] = None
            enriched["delta_from_n64"] = None
        out.append(enriched)
    return out


def macro_name(prefix: str, name: str) -> str:
    return f"OOT3D_{prefix}_{re.sub(r'[^A-Za-z0-9]+', '_', name).upper()}"


def make_header(report: dict[str, object]) -> str:
    lines = [
        "#ifndef OOT3D_STRUCT_LAYOUTS_H",
        "#define OOT3D_STRUCT_LAYOUTS_H",
        "",
        "/* Generated by scripts/map_n64_struct_layouts.py.",
        " * Partial OOT3D layout anchors mapped against zeldaret/oot structure names.",
        " */",
        "",
    ]
    for row in report["oot3d_playstate_offsets"]:
        lines.append(f"#define {macro_name('PLAYSTATE_OFFSET', row['name'])} 0x{int(row['offset']):04X}")
    lines.append("")
    for row in report["oot3d_message_context_offsets"]:
        lines.append(f"#define {macro_name('MESSAGECTX_OFFSET', row['name'])} 0x{int(row['offset']):04X}")
    lines.append("")
    lines.append("#endif")
    lines.append("")
    return "\n".join(lines)


def make_markdown(report: dict[str, object]) -> str:
    lines = [
        "# N64 Structure Layout Port Map",
        "",
        "Generated from `scripts/map_n64_struct_layouts.py`. This report is text-free and only ports names, offsets, and structural roles.",
        "",
        f"- N64 PlayState fields parsed: {report['n64_playstate_field_count']}",
        f"- N64 MessageContext fields parsed: {report['n64_message_context_field_count']}",
        f"- OOT3D PlayState anchors mapped: {len(report['oot3d_playstate_offsets'])}",
        f"- OOT3D MessageContext anchors mapped: {len(report['oot3d_message_context_offsets'])}",
        f"- Generated header: `include/oot3d/struct_layouts.h`",
        "",
        "## OOT3D PlayState Anchors",
        "",
        "| OOT3D offset | Name | N64 field | N64 offset | Delta | Confidence | Role |",
        "| ---: | --- | --- | ---: | ---: | --- | --- |",
    ]
    for row in report["oot3d_playstate_offsets"]:
        n64_offset = "-" if row["n64_offset"] is None else f"0x{int(row['n64_offset']):04x}"
        delta = "-" if row["delta_from_n64"] is None else f"{int(row['delta_from_n64']):+d}"
        n64_name = row["n64_name"] or "-"
        lines.append(
            f"| 0x{int(row['offset']):04x} | `{row['name']}` | `{n64_name}` | {n64_offset} | {delta} | {row['confidence']} | {row['role']} |"
        )

    lines.extend(
        [
            "",
            "## OOT3D MessageContext Anchors",
            "",
            "| OOT3D offset | Name | Confidence | Role | Evidence |",
            "| ---: | --- | --- | --- | --- |",
        ]
    )
    for row in report["oot3d_message_context_offsets"]:
        lines.append(
            f"| 0x{int(row['offset']):04x} | `{row['name']}` | {row['confidence']} | {row['role']} | {row['evidence']} |"
        )
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--n64-root", type=Path, default=DEFAULT_N64_ROOT)
    parser.add_argument("--out-json", type=Path, default=ANALYSIS / "n64_struct_layout_map.json")
    parser.add_argument("--out-md", type=Path, default=ANALYSIS / "n64_struct_layout_map.md")
    parser.add_argument("--out-header", type=Path, default=ROOT / "include" / "oot3d" / "struct_layouts.h")
    args = parser.parse_args()

    play_state_h = args.n64_root / "include" / "play_state.h"
    message_h = args.n64_root / "include" / "message.h"
    n64_playstate_fields = parse_n64_fields(play_state_h, "PlayState")
    n64_message_fields = parse_n64_fields(message_h, "MessageContext")
    report = {
        "n64_sources": [rel(play_state_h), rel(message_h)],
        "n64_playstate_field_count": len(n64_playstate_fields),
        "n64_message_context_field_count": len(n64_message_fields),
        "n64_playstate_fields": n64_playstate_fields,
        "n64_message_context_fields": n64_message_fields,
        "oot3d_playstate_offsets": attach_n64_reference(OOT3D_PLAYSTATE_OBSERVED, n64_playstate_fields),
        "oot3d_message_context_offsets": attach_n64_reference(OOT3D_MESSAGECTX_OBSERVED, n64_message_fields),
    }

    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    args.out_header.parent.mkdir(parents=True, exist_ok=True)
    args.out_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    args.out_md.write_text(make_markdown(report), encoding="utf-8")
    args.out_header.write_text(make_header(report), encoding="utf-8")
    print(f"mapped {len(report['oot3d_playstate_offsets'])} PlayState anchors")
    print(f"mapped {len(report['oot3d_message_context_offsets'])} MessageContext anchors")
    print(f"wrote {args.out_json}")
    print(f"wrote {args.out_md}")
    print(f"wrote {args.out_header}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
