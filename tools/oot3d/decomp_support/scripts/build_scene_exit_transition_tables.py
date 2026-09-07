"""Extract OOT3D scene-exit transition remap tables from code.bin.

Scene command 0x13 stores native s16 exit lists. The player/collision consumer
at 0x003365B0 interprets high values 0x7FF9..0x7FFE through two native
code.bin tables:

    entrance = entranceTable[play->entranceIndex + deltaTable[exit - 0x7FF9]]

This script emits source-like support data for those tables. The delta table
length is closed by the consumer's high-value range. The entrance table is
emitted only as the window proven necessary by native ZSI assets observed in
the RomFS-wide scene index; bytes beyond that remain unpromoted.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CODE_BIN = Path(r"E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin")
DEFAULT_SCENE_INDEX = ROOT / "analysis" / "oot3d_native_scene_index_all_full.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_exit_transition_tables.json"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_exit_transition_tables.md"
DEFAULT_OUT_HEADER = ROOT / "include" / "oot3d" / "scene_transition.h"
DEFAULT_OUT_SOURCE = ROOT / "src" / "code" / "z_scene_exit_transition_tables.c"

CODE_LOAD_BASE = 0x00100000
EXIT_CONSUMER = 0x003365B0
HIGH_REMAP_BASE = 0x7FF9
HIGH_REMAP_END_EXCLUSIVE = 0x7FFF
SPECIAL_7FFF = 0x7FFF
HIGH_REMAP_DELTA_TABLE_LITERAL = 0x00336AB8
HIGH_REMAP_ENTRANCE_TABLE_LITERAL = 0x00336ABC
HIGH_REMAP_LIMIT_LITERAL = 0x00336AB4
PLAY_ENTRANCE_INDEX_OFFSET = 0x5C02
PLAY_PENDING_TRANSITION_VALUE_OFFSET = 0x5C32
PLAY_PENDING_TRANSITION_STATE_OFFSET = 0x5C2D
PLAY_PENDING_TRANSITION_EFFECT_OFFSET = 0x5C76
TRANSITION_REQUEST_STATE = 0x14


def read_u32(data: bytes, address: int) -> int:
    offset = address - CODE_LOAD_BASE
    if offset < 0 or offset + 4 > len(data):
        raise ValueError(f"address 0x{address:08x} outside code.bin")
    return int.from_bytes(data[offset : offset + 4], "little")


def read_u8(data: bytes, address: int) -> int:
    offset = address - CODE_LOAD_BASE
    if offset < 0 or offset >= len(data):
        raise ValueError(f"address 0x{address:08x} outside code.bin")
    return data[offset]


def read_s16(data: bytes, address: int) -> int:
    offset = address - CODE_LOAD_BASE
    if offset < 0 or offset + 2 > len(data):
        raise ValueError(f"address 0x{address:08x} outside code.bin")
    return int.from_bytes(data[offset : offset + 2], "little", signed=True)


def c_string(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def entrance_count_for_setup(setup: dict[str, Any]) -> int:
    for command in setup.get("commands", []):
        if int(command.get("command_id", -1)) != 0x06:
            continue
        decoded = command.get("decoded")
        if not isinstance(decoded, dict):
            continue
        selected = decoded.get("selected_candidate")
        if isinstance(selected, dict):
            return int(selected.get("entry_count", 0))
    return 0


def high_remap_asset_uses(scene_index_path: Path, delta_by_exit: dict[int, int]) -> list[dict[str, Any]]:
    payload = json.loads(scene_index_path.read_text(encoding="utf-8"))
    rows: list[dict[str, Any]] = []
    for record in payload.get("records", []):
        scene_path = str(record.get("scene_path", ""))
        for setup in record.get("setups", []):
            setup_index = int(setup.get("index", -1))
            entrance_count = entrance_count_for_setup(setup)
            highest_current_entrance = max(0, entrance_count - 1)
            for command in setup.get("commands", []):
                if int(command.get("command_id", -1)) != 0x13:
                    continue
                decoded = command.get("decoded")
                if not isinstance(decoded, dict):
                    continue
                for exit_record in decoded.get("values", []):
                    if not isinstance(exit_record, dict):
                        continue
                    category = exit_record.get("oot3d_exit_category")
                    if category != "high_remap_0x7ff9_to_0x7ffe":
                        continue
                    raw = int(exit_record.get("raw_u16", exit_record.get("value", 0))) & 0xFFFF
                    delta = delta_by_exit[raw]
                    rows.append(
                        {
                            "scene": scene_path,
                            "setup": setup_index,
                            "exit_index": int(exit_record.get("index", -1)),
                            "exit_value": raw,
                            "exit_value_hex": f"0x{raw:04x}",
                            "delta": delta,
                            "entrance_count": entrance_count,
                            "highest_current_entrance_index": highest_current_entrance,
                            "highest_table_index_needed": highest_current_entrance + delta,
                        }
                    )
    return rows


def build_report() -> dict[str, Any]:
    code = DEFAULT_CODE_BIN.read_bytes()
    delta_table = read_u32(code, HIGH_REMAP_DELTA_TABLE_LITERAL)
    entrance_table = read_u32(code, HIGH_REMAP_ENTRANCE_TABLE_LITERAL)
    direct_limit = read_u32(code, HIGH_REMAP_LIMIT_LITERAL)
    delta_count = HIGH_REMAP_END_EXCLUSIVE - HIGH_REMAP_BASE
    delta_rows = [
        {
            "exit_value": HIGH_REMAP_BASE + index,
            "exit_value_hex": f"0x{HIGH_REMAP_BASE + index:04x}",
            "delta": read_u8(code, delta_table + index),
        }
        for index in range(delta_count)
    ]
    delta_by_exit = {int(row["exit_value"]): int(row["delta"]) for row in delta_rows}
    asset_uses = high_remap_asset_uses(DEFAULT_SCENE_INDEX, delta_by_exit)
    observed_exit_values = sorted({int(row["exit_value"]) for row in asset_uses})
    unobserved_delta_rows = [
        row for row in delta_rows if int(row["exit_value"]) not in set(observed_exit_values)
    ]
    verified_entrance_count = (
        max(int(row["highest_table_index_needed"]) for row in asset_uses) + 1 if asset_uses else 0
    )
    entrance_rows = [
        {
            "index": index,
            "entrance": read_s16(code, entrance_table + index * 2),
        }
        for index in range(verified_entrance_count)
    ]
    return {
        "format": "oot3d_scene_exit_transition_tables_v1",
        "source_policy": {
            "native_code_source": str(DEFAULT_CODE_BIN),
            "native_asset_source": str(DEFAULT_SCENE_INDEX),
            "n64_policy": "N64 names are not used by this extraction.",
        },
        "consumer": {
            "entry": f"0x{EXIT_CONSUMER:08x}",
            "formula": "entranceTable[play+0x5C02 + deltaTable[exit - 0x7FF9]]",
            "direct_limit": f"0x{direct_limit:04x}",
            "special_value": f"0x{SPECIAL_7FFF:04x}",
            "high_remap_range": f"0x{HIGH_REMAP_BASE:04x}..0x{HIGH_REMAP_END_EXCLUSIVE - 1:04x}",
            "delta_table": f"0x{delta_table:08x}",
            "entrance_table": f"0x{entrance_table:08x}",
        },
        "play_fields": {
            "entrance_index": f"play+0x{PLAY_ENTRANCE_INDEX_OFFSET:04x}",
            "pending_transition_value": f"play+0x{PLAY_PENDING_TRANSITION_VALUE_OFFSET:04x}",
            "pending_transition_state": f"play+0x{PLAY_PENDING_TRANSITION_STATE_OFFSET:04x}",
            "pending_transition_effect": f"play+0x{PLAY_PENDING_TRANSITION_EFFECT_OFFSET:04x}",
            "transition_request_state": f"0x{TRANSITION_REQUEST_STATE:02x}",
        },
        "delta_rows": delta_rows,
        "observed_high_remap_exit_values": [
            {
                "exit_value": value,
                "exit_value_hex": f"0x{value:04x}",
            }
            for value in observed_exit_values
        ],
        "unobserved_high_remap_delta_rows": unobserved_delta_rows,
        "asset_high_remap_uses": asset_uses,
        "verified_entrance_table_rows": entrance_rows,
        "verified_entrance_table_count": verified_entrance_count,
        "remaining_gaps": [
            "The total extent of the entrance table beyond the asset-proven window is not promoted.",
            "Final source names for transition helper functions still depend on naming the surrounding state machine.",
        ],
    }


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_markdown(path: Path, payload: dict[str, Any]) -> None:
    lines = [
        "# Scene Exit Transition Tables",
        "",
        "Generated from OOT3D `code.bin` and native ZSI scene indices.",
        "",
        "## Summary",
        "",
        f"- Consumer: `{payload['consumer']['entry']}`",
        f"- Formula: `{payload['consumer']['formula']}`",
        f"- Delta table: `{payload['consumer']['delta_table']}`",
        f"- Entrance table: `{payload['consumer']['entrance_table']}`",
        f"- Verified entrance rows: {payload['verified_entrance_table_count']}",
        f"- Observed high-remap values: {len(payload['observed_high_remap_exit_values'])}",
        f"- Unobserved high-remap values: {len(payload['unobserved_high_remap_delta_rows'])}",
        "",
        "## Delta Table",
        "",
        "| Exit value | Delta |",
        "| --- | ---: |",
    ]
    for row in payload["delta_rows"]:
        lines.append(f"| `{row['exit_value_hex']}` | {row['delta']} |")

    lines.extend(["", "Observed in native ZSI exit lists:", ""])
    for row in payload["observed_high_remap_exit_values"]:
        lines.append(f"- `{row['exit_value_hex']}`")
    lines.extend(["", "Not observed in current native ZSI exit lists:", ""])
    for row in payload["unobserved_high_remap_delta_rows"]:
        lines.append(f"- `{row['exit_value_hex']}` delta {row['delta']}")

    lines.extend(
        [
            "",
            "## Verified Entrance Table Window",
            "",
            "| Index | Entrance |",
            "| ---: | ---: |",
        ]
    )
    for row in payload["verified_entrance_table_rows"]:
        lines.append(f"| {row['index']} | {row['entrance']} |")

    lines.extend(
        [
            "",
            "## Asset Coverage",
            "",
            "| Scene | Setup | Exit index | Exit value | Delta | Entrances | Highest table index |",
            "| --- | ---: | ---: | --- | ---: | ---: | ---: |",
        ]
    )
    for row in payload["asset_high_remap_uses"]:
        lines.append(
            f"| `{row['scene']}` | {row['setup']} | {row['exit_index']} | `{row['exit_value_hex']}` | "
            f"{row['delta']} | {row['entrance_count']} | {row['highest_table_index_needed']} |"
        )

    lines.extend(["", "## Remaining Gaps", ""])
    for gap in payload["remaining_gaps"]:
        lines.append(f"- {gap}")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")


def write_header(path: Path, payload: dict[str, Any]) -> None:
    count = int(payload["verified_entrance_table_count"])
    lines = [
        "#ifndef OOT3D_SCENE_TRANSITION_H",
        "#define OOT3D_SCENE_TRANSITION_H",
        "",
        '#include "oot3d/types.h"',
        "",
        "enum {",
        f"    OOT3D_SCENE_EXIT_DIRECT_LIMIT = 0x{HIGH_REMAP_BASE:04X},",
        f"    OOT3D_SCENE_EXIT_HIGH_REMAP_BASE = 0x{HIGH_REMAP_BASE:04X},",
        f"    OOT3D_SCENE_EXIT_HIGH_REMAP_COUNT = {HIGH_REMAP_END_EXCLUSIVE - HIGH_REMAP_BASE},",
        f"    OOT3D_SCENE_EXIT_SPECIAL_7FFF = 0x{SPECIAL_7FFF:04X},",
        f"    OOT3D_SCENE_EXIT_HIGH_REMAP_VERIFIED_ENTRANCE_COUNT = {count},",
        f"    OOT3D_SCENE_TRANSITION_REQUEST_STATE = 0x{TRANSITION_REQUEST_STATE:02X},",
        f"    OOT3D_PLAY_ENTRANCE_INDEX_OFFSET = 0x{PLAY_ENTRANCE_INDEX_OFFSET:04X},",
        f"    OOT3D_PLAY_PENDING_TRANSITION_VALUE_OFFSET = 0x{PLAY_PENDING_TRANSITION_VALUE_OFFSET:04X},",
        f"    OOT3D_PLAY_PENDING_TRANSITION_STATE_OFFSET = 0x{PLAY_PENDING_TRANSITION_STATE_OFFSET:04X},",
        f"    OOT3D_PLAY_PENDING_TRANSITION_EFFECT_OFFSET = 0x{PLAY_PENDING_TRANSITION_EFFECT_OFFSET:04X},",
        "};",
        "",
        "typedef struct {",
        "    u16 exitValue;",
        "    u8 entranceIndexDelta;",
        "} Oot3dSceneExitHighRemapDelta;",
        "",
        "typedef struct {",
        "    u8 tableIndex;",
        "    s16 entrance;",
        "} Oot3dSceneExitHighRemapEntrance;",
        "",
        "typedef struct {",
        "    const Oot3dSceneExitHighRemapDelta* deltas;",
        "    u32 deltaCount;",
        "    const Oot3dSceneExitHighRemapEntrance* verifiedEntrances;",
        "    u32 verifiedEntranceCount;",
        "} Oot3dSceneExitHighRemapTables;",
        "",
        "extern const Oot3dSceneExitHighRemapDelta oot3d_scene_exit_high_remap_deltas[];",
        "extern const Oot3dSceneExitHighRemapEntrance oot3d_scene_exit_high_remap_verified_entrances[];",
        "extern const Oot3dSceneExitHighRemapTables oot3d_scene_exit_high_remap_tables;",
        "",
        "#endif",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def write_source(path: Path, payload: dict[str, Any]) -> None:
    lines = [
        "/*",
        " * Generated from OOT3D code.bin scene-exit transition tables.",
        " * Entrance rows are the asset-proven table window, not a claimed full extent.",
        " */",
        '#include "oot3d/scene_transition.h"',
        "",
        "const Oot3dSceneExitHighRemapDelta oot3d_scene_exit_high_remap_deltas[] = {",
    ]
    for row in payload["delta_rows"]:
        lines.append(f"    {{ 0x{int(row['exit_value']):04X}u, {int(row['delta'])}u }},")
    lines.extend(
        [
            "};",
            "",
            "const Oot3dSceneExitHighRemapEntrance oot3d_scene_exit_high_remap_verified_entrances[] = {",
        ]
    )
    for row in payload["verified_entrance_table_rows"]:
        lines.append(f"    {{ {int(row['index'])}u, {int(row['entrance'])} }},")
    lines.extend(
        [
            "};",
            "",
            "const Oot3dSceneExitHighRemapTables oot3d_scene_exit_high_remap_tables = {",
            "    oot3d_scene_exit_high_remap_deltas,",
            "    OOT3D_SCENE_EXIT_HIGH_REMAP_COUNT,",
            "    oot3d_scene_exit_high_remap_verified_entrances,",
            "    OOT3D_SCENE_EXIT_HIGH_REMAP_VERIFIED_ENTRANCE_COUNT,",
            "};",
            "",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    payload = build_report()
    write_json(DEFAULT_OUT_JSON, payload)
    write_markdown(DEFAULT_OUT_MD, payload)
    write_header(DEFAULT_OUT_HEADER, payload)
    write_source(DEFAULT_OUT_SOURCE, payload)
    print(DEFAULT_OUT_JSON)
    print(DEFAULT_OUT_MD)
    print(DEFAULT_OUT_HEADER)
    print(DEFAULT_OUT_SOURCE)
    print(
        json.dumps(
            {
                "delta_count": len(payload["delta_rows"]),
                "asset_high_remap_use_count": len(payload["asset_high_remap_uses"]),
                "verified_entrance_table_count": payload["verified_entrance_table_count"],
            },
            indent=2,
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
