#!/usr/bin/env python3
"""Decode native OOT3D player model/shadow selector tables from code.bin."""

from __future__ import annotations

import argparse
import json
import struct
from collections import Counter
from pathlib import Path
from typing import Any


FORMAT = "oot3d_player_shadow_model_tables_v1"
IMAGE_BASE = 0x00100000
DEFAULT_CODE_BIN = Path(r"E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin")

GROUP_TABLE_ADDR = 0x0053A558
GROUP_TABLE_ENTRY_SIZE = 5
GROUP_TABLE_COUNT = 16

POINTER_TABLE_ADDR = 0x0053C698
POINTER_TABLE_ENTRY_SIZE = 4
POINTER_TABLE_COUNT = 21


def hex8(value: int) -> str:
    return f"0x{value:08X}"


def hex2(value: int) -> str:
    return f"0x{value:02X}"


def runtime_to_file_offset(address: int) -> int:
    return address - IMAGE_BASE


def read_bytes(data: bytes, runtime_address: int, size: int) -> bytes:
    offset = runtime_to_file_offset(runtime_address)
    if offset < 0 or offset + size > len(data):
        raise ValueError(
            f"runtime address {hex8(runtime_address)} size {size} is outside code.bin"
        )
    return data[offset : offset + size]


def read_u32(data: bytes, runtime_address: int) -> int:
    return struct.unpack_from("<I", read_bytes(data, runtime_address, 4))[0]


def pointer_target_region(target: int, code_size: int) -> str:
    file_offset = runtime_to_file_offset(target)
    if file_offset < 0 or file_offset >= code_size:
        return "outside_code_bin"
    if GROUP_TABLE_ADDR <= target < GROUP_TABLE_ADDR + GROUP_TABLE_ENTRY_SIZE * GROUP_TABLE_COUNT:
        return "player_model_group_table"
    if POINTER_TABLE_ADDR <= target < POINTER_TABLE_ADDR + POINTER_TABLE_ENTRY_SIZE * POINTER_TABLE_COUNT:
        return "player_model_pointer_table"
    if 0x0053A000 <= target < 0x0053D000:
        return "near_player_model_resource_tables"
    return "codebin_data_or_text"


def decode_preview_words(data: bytes, target: int, max_words: int) -> list[dict[str, Any]]:
    offset = runtime_to_file_offset(target)
    if offset < 0 or offset >= len(data):
        return []
    available_words = max(0, min(max_words, (len(data) - offset) // 4))
    result = []
    for index in range(available_words):
        value = struct.unpack_from("<I", data, offset + index * 4)[0]
        result.append({"word_index": index, "hex": hex8(value), "decimal": value})
    return result


def decode_group_records(data: bytes, pointer_entries: list[dict[str, Any]]) -> list[dict[str, Any]]:
    records = []
    for index in range(GROUP_TABLE_COUNT):
        runtime_address = GROUP_TABLE_ADDR + index * GROUP_TABLE_ENTRY_SIZE
        raw = read_bytes(data, runtime_address, GROUP_TABLE_ENTRY_SIZE)
        render_mode, model0, shadow_gate, texcoord, model3 = raw
        selectors = {
            "model0_selector": model0,
            "shadow_gate_selector": shadow_gate,
            "model3_selector": model3,
        }
        selector_targets: dict[str, dict[str, Any] | None] = {}
        for name, selector in selectors.items():
            selector_targets[name] = (
                {
                    "pointer_table_index": selector,
                    "target_runtime_address": pointer_entries[selector]["target_runtime_address"],
                    "target_file_offset": pointer_entries[selector]["target_file_offset"],
                    "target_region": pointer_entries[selector]["target_region"],
                }
                if selector < len(pointer_entries)
                else None
            )
        records.append(
            {
                "index": index,
                "runtime_address": hex8(runtime_address),
                "file_offset": hex8(runtime_to_file_offset(runtime_address)),
                "raw_bytes": " ".join(f"{byte:02X}" for byte in raw),
                "render_mode_byte": render_mode,
                "render_mode_hex": hex2(render_mode),
                "model0_selector": model0,
                "model0_selector_hex": hex2(model0),
                "shadow_gate_selector": shadow_gate,
                "shadow_gate_selector_hex": hex2(shadow_gate),
                "texcoord_byte": texcoord,
                "texcoord_hex": hex2(texcoord),
                "model3_selector": model3,
                "model3_selector_hex": hex2(model3),
                "selector_targets": selector_targets,
            }
        )
    return records


def decode_pointer_table(data: bytes) -> list[dict[str, Any]]:
    raw_targets = [read_u32(data, POINTER_TABLE_ADDR + index * 4) for index in range(POINTER_TABLE_COUNT)]
    sorted_targets = sorted(set(raw_targets))
    entries = []
    for index, target in enumerate(raw_targets):
        runtime_address = POINTER_TABLE_ADDR + index * POINTER_TABLE_ENTRY_SIZE
        next_targets = [candidate for candidate in sorted_targets if candidate > target]
        next_target = next_targets[0] if next_targets else None
        bytes_until_next = next_target - target if next_target is not None else None
        preview_words = decode_preview_words(data, target, 12)
        entries.append(
            {
                "index": index,
                "runtime_address": hex8(runtime_address),
                "file_offset": hex8(runtime_to_file_offset(runtime_address)),
                "target_runtime_address": hex8(target),
                "target_file_offset": hex8(runtime_to_file_offset(target)),
                "target_within_code_bin": 0 <= runtime_to_file_offset(target) < len(data),
                "target_region": pointer_target_region(target, len(data)),
                "next_distinct_target_runtime_address": hex8(next_target) if next_target else None,
                "bytes_until_next_distinct_target": bytes_until_next,
                "preview_u32_words": preview_words,
            }
        )
    return entries


def build_summary(data: bytes, code_bin: Path) -> dict[str, Any]:
    pointer_entries = decode_pointer_table(data)
    group_records = decode_group_records(data, pointer_entries)

    gate_counts = Counter(record["shadow_gate_selector"] for record in group_records)
    texcoord_counts = Counter(record["texcoord_byte"] for record in group_records)
    render_mode_counts = Counter(record["render_mode_byte"] for record in group_records)

    gate_usage = []
    for gate, count in sorted(gate_counts.items()):
        pointer_entry = pointer_entries[gate] if gate < len(pointer_entries) else None
        gate_usage.append(
            {
                "shadow_gate_selector": gate,
                "shadow_gate_selector_hex": hex2(gate),
                "group_record_count": count,
                "group_record_indices": [
                    record["index"] for record in group_records if record["shadow_gate_selector"] == gate
                ],
                "pointer_table_target_runtime_address": (
                    pointer_entry["target_runtime_address"] if pointer_entry else None
                ),
                "pointer_table_target_file_offset": (
                    pointer_entry["target_file_offset"] if pointer_entry else None
                ),
                "pointer_table_target_region": pointer_entry["target_region"] if pointer_entry else None,
            }
        )

    default_group = group_records[0]
    default_gate = default_group["shadow_gate_selector"]
    default_gate_pointer = pointer_entries[default_gate] if default_gate < len(pointer_entries) else None

    return {
        "format": FORMAT,
        "source_kind": "oot3d_codebin_static_table_decode",
        "source_policy": "code_bin_values_are_native_evidence_trace_values_are_validation_only",
        "code_bin": str(code_bin),
        "image_base": hex8(IMAGE_BASE),
        "tables": {
            "player_model_group_table": {
                "runtime_address": hex8(GROUP_TABLE_ADDR),
                "file_offset": hex8(runtime_to_file_offset(GROUP_TABLE_ADDR)),
                "entry_size": GROUP_TABLE_ENTRY_SIZE,
                "entry_count": GROUP_TABLE_COUNT,
                "records": group_records,
            },
            "player_model_pointer_table": {
                "runtime_address": hex8(POINTER_TABLE_ADDR),
                "file_offset": hex8(runtime_to_file_offset(POINTER_TABLE_ADDR)),
                "entry_size": POINTER_TABLE_ENTRY_SIZE,
                "entry_count": POINTER_TABLE_COUNT,
                "entries": pointer_entries,
            },
        },
        "summary": {
            "shadow_gate_selector_counts": [
                {"selector": gate, "selector_hex": hex2(gate), "count": count}
                for gate, count in sorted(gate_counts.items())
            ],
            "texcoord_byte_counts": [
                {"value": value, "value_hex": hex2(value), "count": count}
                for value, count in sorted(texcoord_counts.items())
            ],
            "render_mode_byte_counts": [
                {"value": value, "value_hex": hex2(value), "count": count}
                for value, count in sorted(render_mode_counts.items())
            ],
            "shadow_gate_usage": gate_usage,
            "default_group_index": 0,
            "default_group_shadow_gate_selector": default_gate,
            "default_group_shadow_gate_selector_hex": hex2(default_gate),
            "default_group_shadow_gate_pointer_target": (
                default_gate_pointer["target_runtime_address"] if default_gate_pointer else None
            ),
            "native_interpretation": (
                "The five-byte player model-group records choose selector bytes, including the "
                "Shadow2D special-route gate byte. The pointer table maps those selector ids to "
                "native code.bin resource-id tables; it does not by itself contain the active "
                "AutoClass1+0x1A8 Shadow2D projection-record pointer."
            ),
        },
        "unresolved_boundary": {
            "auto_class1_record_pointer_owner_resolved": False,
            "missing_chain": (
                "native producer or alias/bulk-copy route that installs a nonzero projection-record "
                "pointer into the source AutoClass1+0x1A8 before Player_Draw clones it"
            ),
        },
    }


def render_markdown(summary: dict[str, Any]) -> str:
    gate_rows = summary["summary"]["shadow_gate_usage"]
    pointer_entries = summary["tables"]["player_model_pointer_table"]["entries"]
    lines = [
        "# Player Shadow Model Tables",
        "",
        "This report decodes the native OOT3D player model/shadow selector tables directly from `code.bin`. It is static code/data evidence, not an emulator trace promotion.",
        "",
        "## Tables",
        "",
        f"- Image base: `{summary['image_base']}`",
        f"- Group table: `{summary['tables']['player_model_group_table']['runtime_address']}` / file offset `{summary['tables']['player_model_group_table']['file_offset']}`, `{summary['tables']['player_model_group_table']['entry_count']}` records x `{summary['tables']['player_model_group_table']['entry_size']}` bytes",
        f"- Pointer table: `{summary['tables']['player_model_pointer_table']['runtime_address']}` / file offset `{summary['tables']['player_model_pointer_table']['file_offset']}`, `{summary['tables']['player_model_pointer_table']['entry_count']}` records x `{summary['tables']['player_model_pointer_table']['entry_size']}` bytes",
        "",
        "## Shadow Gate Usage",
        "",
        "| gate | group records | pointer target | target region |",
        "| --- | --- | --- | --- |",
    ]
    for row in gate_rows:
        indices = ", ".join(str(index) for index in row["group_record_indices"])
        lines.append(
            f"| `{row['shadow_gate_selector_hex']}` | `{indices}` | "
            f"`{row['pointer_table_target_runtime_address']}` | `{row['pointer_table_target_region']}` |"
        )
    lines.extend(
        [
            "",
            "## Pointer Table Preview",
            "",
            "| index | target | region | first words |",
            "| --- | --- | --- | --- |",
        ]
    )
    for entry in pointer_entries:
        first_words = ", ".join(word["hex"] for word in entry["preview_u32_words"][:4])
        lines.append(
            f"| `{entry['index']}` | `{entry['target_runtime_address']}` | "
            f"`{entry['target_region']}` | `{first_words}` |"
        )
    lines.extend(
        [
            "",
            "## Interpretation",
            "",
            summary["summary"]["native_interpretation"],
            "",
            "The default decoded group is group `0`, whose Shadow2D gate selector is "
            f"`{summary['summary']['default_group_shadow_gate_selector_hex']}` and maps through the pointer table to "
            f"`{summary['summary']['default_group_shadow_gate_pointer_target']}`.",
            "",
            "This closes the static selector-table side of the player route. The unresolved boundary remains the owner/alias chain that writes the active nonzero projection-record pointer into `AutoClass1+0x1A8` before the player draw clone.",
            "",
        ]
    )
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--code-bin", type=Path, default=DEFAULT_CODE_BIN)
    parser.add_argument("--output-json", type=Path, required=True)
    parser.add_argument("--output-md", type=Path, required=True)
    args = parser.parse_args()

    data = args.code_bin.read_bytes()
    summary = build_summary(data, args.code_bin)

    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    args.output_md.write_text(render_markdown(summary), encoding="utf-8")

    print(
        json.dumps(
            {
                "status": "ok",
                "output_json": str(args.output_json),
                "output_md": str(args.output_md),
                "group_record_count": GROUP_TABLE_COUNT,
                "pointer_entry_count": POINTER_TABLE_COUNT,
                "default_shadow_gate": summary["summary"]["default_group_shadow_gate_selector_hex"],
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
