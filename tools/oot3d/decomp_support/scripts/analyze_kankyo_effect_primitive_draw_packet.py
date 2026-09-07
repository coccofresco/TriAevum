#!/usr/bin/env python3
"""Verify the OOT3D kankyo/effect primitive draw packet bridge.

The report ties the effect draw consumer to the primitive PICA packet builder:
runtime draw count at +0x174 is forwarded to FUN_00313444, which emits the
native primitive packet and commits it through FUN_003084DC.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[4]
DECOMPILED = ROOT / "tools" / "oot3d" / "decomp_support" / "ghidra_export" / "decompiled"
ANALYSIS = ROOT / "tools" / "oot3d" / "decomp_support" / "analysis"
DEFAULT_CODE_BIN = Path(r"E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin")
DEFAULT_DISASSEMBLY = (
    ROOT
    / "tools"
    / "oot3d"
    / "decomp_support"
    / "analysis"
    / "actor_vs_packet_writer_ghidra_export"
    / "disassembly_selected.txt"
)
CODE_BASE = 0x00100000


SOURCES = {
    "effect_draw": DECOMPILED / "99103_003fb5ec_FUN_003fb5ec.c",
    "effect_setup": DECOMPILED / "99104_003fb9ac_FUN_003fb9ac.c",
    "primitive_packet": DECOMPILED / "99042_00313444_FUN_00313444.c",
    "attribute_mask": DECOMPILED / "99043_003135ac_FUN_003135ac.c",
    "mode_resolver": DECOMPILED / "99056_0047ff34_FUN_0047ff34.c",
    "light_list_append": DECOMPILED / "99047_00313698_FUN_00313698.c",
    "light_list_default": DECOMPILED / "99046_00313650_FUN_00313650.c",
    "buffer0_resolver": DECOMPILED / "99051_00313ad8_FUN_00313ad8.c",
    "buffer1_resolver": DECOMPILED / "99050_00313ac8_FUN_00313ac8.c",
}


ANCHORS: dict[str, list[tuple[str, str]]] = {
    "effect_draw": [
        ("effect_draw_signature", "void FUN_003fb5ec(int param_1)"),
        ("calls_effect_setup", "FUN_003fb9ac(param_1);"),
        ("light_list_upload", "FUN_004090cc(param_1 + 0x18,param_1 + 0x50);"),
        ("attribute_mask_emit", "FUN_003135ac(param_1 + 0x18,uVar8 | uVar7 | uVar5 | uVar9 | uVar10 << 6);"),
        ("runtime_draw_count_read", "uVar2 = *(undefined4 *)(*(int *)(param_1 + 0x354) + 0x174);"),
        ("descriptor_flag_0x20_draw_mode_5", "FUN_00313444(param_1 + 0x18,5,uVar2,DAT_003fb9a8);"),
        ("descriptor_flag_0x20_draw_mode_4", "FUN_00313444(param_1 + 0x18,4,uVar2,DAT_003fb9a8);"),
    ],
    "effect_setup": [
        ("primary_secondary_color_source", "local_3c = *(undefined4 *)(iVar2 + 0xf0);"),
        ("uv_transform_base", "FUN_0031432c(param_1 + 0x18,iVar2,*(int *)(param_1 + 0x354) + iVar2 * 0x30 + 0x110);"),
        ("six_light_slots", "} while (iVar2 < 6);"),
    ],
    "primitive_packet": [
        ("primitive_packet_signature", "void FUN_00313444(int *param_1,int param_2,int param_3,int param_4,int param_5)"),
        ("stack_index_base_arg", "uVar5 = param_1[9] + param_5;"),
        ("unsigned_short_index_type_gate", "if (param_4 == 0x1403)"),
        ("unsigned_short_high_bit", "uVar5 = uVar5 | 0x80000000;"),
        ("mode_resolver", "iVar4 = FUN_0047ff34(param_2);"),
        ("draw_count_payload", "piVar3[0x10] = param_3;"),
        ("packet_word_count", "*(int **)(*param_1 + 8) = piVar3 + 0x24;"),
    ],
    "attribute_mask": [
        ("attribute_mask_signature", "void FUN_003135ac(int *param_1,uint param_2)"),
        ("attribute_mask_payload", "*puVar1 = param_2 | 0x7fff0000;"),
        ("attribute_mask_header", "puVar1[1] = DAT_003135e4;"),
    ],
    "mode_resolver": [
        ("mode_5_to_1", "if (param_1 == 5) {\n    return 1;"),
        ("mode_6_to_2", "if (param_1 != 6)"),
        ("mode_4_or_6010_to_3", "if (param_1 == 0x6010 || param_1 == 4)"),
    ],
    "light_list_append": [
        ("primary_entry_counter", "*(int *)(param_1 + 0xc) * 0x10 + 0x14"),
        ("primary_entry_increment", "*(int *)(param_1 + 0xc) = *(int *)(param_1 + 0xc) + 1;"),
    ],
    "light_list_default": [
        ("default_entry_base", "* 0x10 + 0xd4"),
        ("default_intensity_word", "0x3f000000"),
    ],
    "buffer0_resolver": [
        ("buffer0_table_offset", "return *(undefined4 *)(param_1 + *(int *)(param_1 + 0x124) * 4 + 0x1a0);"),
    ],
    "buffer1_resolver": [
        ("buffer1_table_offset", "return *(undefined4 *)(param_1 + *(int *)(param_1 + 0x124) * 4 + 0x1a8);"),
    ],
}


DISASSEMBLY_ANCHORS = [
    ("effect_draw_runtime_count_r2", "003fb968: ldr r2,[r0,#0x174]"),
    ("effect_draw_stack_index_base_zero", "003fb96c: str r3,[sp,#0x0]"),
    ("effect_draw_index_type_literal_load", "003fb950: ldr r1,[0x3fb9a8]"),
    ("primitive_packet_stack_arg_read", "00313450: ldr r0,[sp,#0x20]"),
    ("primitive_packet_commit_tail", "00313584: b 0x003084dc"),
    ("attribute_mask_commit_tail", "003135e0: b 0x003084dc"),
]


LITERAL_ADDRESSES = {
    "primitive_packet_header_0x229": 0x00313588,
    "primitive_packet_header_0x25e": 0x0031358C,
    "primitive_packet_header_0x25f": 0x00313590,
    "primitive_packet_header_0x253_single": 0x00313594,
    "primitive_packet_header_0x227": 0x00313598,
    "primitive_packet_header_0x245": 0x0031359C,
    "primitive_packet_header_0x22f": 0x003135A0,
    "attribute_mask_payload_or_mask": 0x003135A4,
    "primitive_packet_header_0x2ba": 0x003135A8,
    "attribute_mask_header_0x2b0": 0x003135E4,
    "element_type_u_byte": 0x00313860,
    "effect_draw_index_type": 0x003FB9A8,
}


def read_texts() -> dict[str, str]:
    texts: dict[str, str] = {}
    missing: list[str] = []
    for key, path in SOURCES.items():
        if not path.is_file():
            missing.append(str(path))
            continue
        texts[key] = path.read_text(encoding="utf-8", errors="replace")
    if missing:
        raise FileNotFoundError("missing Ghidra decompile(s): " + ", ".join(missing))
    return texts


def read_u32(code: bytes, address: int) -> int:
    offset = address - CODE_BASE
    if offset < 0 or offset + 4 > len(code):
        raise ValueError(f"literal address out of code.bin range: 0x{address:08X}")
    return int.from_bytes(code[offset : offset + 4], "little")


def verify_anchors(texts: dict[str, str], disassembly: str) -> list[dict[str, Any]]:
    checks: list[dict[str, Any]] = []
    missing: list[str] = []
    for key, entries in ANCHORS.items():
        for label, needle in entries:
            found = needle in texts[key]
            checks.append({"source": key, "label": label, "found": found, "needle": needle})
            if not found:
                missing.append(f"{key}:{label}")
    for label, needle in DISASSEMBLY_ANCHORS:
        found = needle in disassembly
        checks.append({"source": "disassembly", "label": label, "found": found, "needle": needle})
        if not found:
            missing.append(f"disassembly:{label}")
    if missing:
        raise AssertionError("missing native anchor(s): " + ", ".join(missing))
    return checks


def build_report(checks: list[dict[str, Any]], literal_words: dict[str, int]) -> dict[str, Any]:
    packet_words = [
        literal_words["primitive_packet_header_0x229"],
        literal_words["primitive_packet_header_0x229"] + 0x2A,
        literal_words["primitive_packet_header_0x25e"],
        literal_words["primitive_packet_header_0x25e"] + 0x20000,
        literal_words["primitive_packet_header_0x25f"],
        literal_words["primitive_packet_header_0x253_single"],
        literal_words["primitive_packet_header_0x227"],
        literal_words["primitive_packet_header_0x245"],
        literal_words["primitive_packet_header_0x22f"],
        literal_words["primitive_packet_header_0x22f"] + (literal_words["primitive_packet_header_0x25e"] >> 16),
        literal_words["primitive_packet_header_0x22f"] + (literal_words["primitive_packet_header_0x25e"] >> 16) - 0x120,
        literal_words["attribute_mask_payload_or_mask"],
        literal_words["primitive_packet_header_0x2ba"],
    ]
    return {
        "report": "kankyo_effect_primitive_draw_packet",
        "source": "OOT3D code.bin Ghidra decompile plus literal pool",
        "anchor_check_count": len(checks),
        "all_anchors_found": all(check["found"] for check in checks),
        "checks": checks,
        "literal_words": {key: f"0x{value:08X}" for key, value in literal_words.items()},
        "resolved": {
            "effect_draw_consumer_reads_runtime_draw_count": True,
            "runtime_draw_count_offset": "0x174",
            "primitive_packet_builder": "0x00313444",
            "primitive_packet_commit": "0x003084DC",
            "attribute_mask_packet_builder": "0x003135AC",
            "draw_mode_resolver": "0x0047FF34",
            "primitive_packet_word_count": 0x24,
            "effect_path_index_base_stack_value": 0,
            "effect_path_index_element_type": f"0x{literal_words['effect_draw_index_type']:04X}",
            "unsigned_short_index_base_sets_high_bit": True,
            "backend_opengl_implementation_resolved": False,
        },
        "draw_mode_mapping": {
            "mode_5": 1,
            "mode_6": 2,
            "mode_4": 3,
            "mode_0x6010": 3,
            "other": 0,
        },
        "primitive_packet_literal_words": [f"0x{word:08X}" for word in packet_words],
        "title_intro_day_transition_impact": {
            "quad_batch_runtime_emit": "resolved_in_kankyo_lens_quad_batch_draw",
            "primitive_packet_bridge": "resolved_here",
            "remaining_visible_gap": "implement OpenGL/backend handling of the proven PICA primitive packet and attribute lanes",
        },
    }


def write_markdown(report: dict[str, Any], path: Path) -> None:
    lines = [
        "# Kankyo Effect Primitive Draw Packet",
        "",
        "This report verifies the native bridge from the kankyo/effect draw consumer to the primitive packet builder.",
        "",
        "## Resolved",
        "",
    ]
    resolved = report["resolved"]
    lines.append(f"- Runtime draw count offset: `{resolved['runtime_draw_count_offset']}`.")
    lines.append(f"- Primitive packet builder: `{resolved['primitive_packet_builder']}`.")
    lines.append(f"- Primitive packet commit helper: `{resolved['primitive_packet_commit']}`.")
    lines.append(f"- Attribute mask packet builder: `{resolved['attribute_mask_packet_builder']}`.")
    lines.append(f"- Draw mode resolver: `{resolved['draw_mode_resolver']}`.")
    lines.append(f"- Packet word count: `{resolved['primitive_packet_word_count']}`.")
    lines.append(f"- Effect-path index base stack value: `{resolved['effect_path_index_base_stack_value']}`.")
    lines.append(f"- Effect-path index element type: `{resolved['effect_path_index_element_type']}`.")
    lines.append("")
    lines.append("## Draw Mode Mapping")
    lines.append("")
    for key, value in report["draw_mode_mapping"].items():
        lines.append(f"- `{key}` -> `{value}`.")
    lines.append("")
    lines.append("## Primitive Packet Literal Words")
    lines.append("")
    lines.append("- " + ", ".join(f"`{word}`" for word in report["primitive_packet_literal_words"]) + ".")
    lines.append("")
    lines.append("## Remaining Gap")
    lines.append("")
    lines.append(
        "- The backend/OpenGL implementation is still pending. The next step is to consume the proven PICA primitive "
        "packet and attribute lanes in the renderer, not to add a generic sun/lens fallback."
    )
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--code-bin", type=Path, default=DEFAULT_CODE_BIN)
    parser.add_argument("--disassembly", type=Path, default=DEFAULT_DISASSEMBLY)
    parser.add_argument("--json", type=Path, default=ANALYSIS / "kankyo_effect_primitive_draw_packet.json")
    parser.add_argument("--markdown", type=Path, default=ANALYSIS / "kankyo_effect_primitive_draw_packet.md")
    args = parser.parse_args()

    if not args.code_bin.is_file():
        raise FileNotFoundError(args.code_bin)
    if not args.disassembly.is_file():
        raise FileNotFoundError(args.disassembly)
    texts = read_texts()
    disassembly = args.disassembly.read_text(encoding="utf-8", errors="replace")
    checks = verify_anchors(texts, disassembly)
    code = args.code_bin.read_bytes()
    literal_words = {key: read_u32(code, address) for key, address in LITERAL_ADDRESSES.items()}
    report = build_report(checks, literal_words)
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
