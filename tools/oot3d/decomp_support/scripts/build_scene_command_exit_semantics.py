"""Build evidence for OOT3D scene command 0x13 exit-list semantics.

The scene command handler only installs the native s16 table at Play+0x5C1C.
This script records the downstream player/collision consumer constants and the
asset-side value distribution while keeping source names conservative when the
surrounding transition state machine has not yet been named.
"""

from __future__ import annotations

import json
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CODE_BIN = Path(r"E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin")
DEFAULT_SCENE_INDEX = ROOT / "analysis" / "oot3d_native_scene_index_all_full.json"
DEFAULT_HELPER_SEMANTICS = ROOT / "analysis" / "transition_request_helper_semantics.json"
DEFAULT_TRANSITION_TABLES = ROOT / "analysis" / "scene_exit_transition_tables.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_command_exit_semantics.json"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_command_exit_semantics.md"

CODE_LOAD_BASE = 0x00100000
EXIT_HANDLER = 0x002A9E2C
EXIT_CONSUMER = 0x003365B0
EXIT_TABLE_PLAY_OFFSET = 0x5C1C
PENDING_TRANSITION_FIELD = 0x5C32
PENDING_TRANSITION_TYPE_FIELD = 0x5C2D
PENDING_TRANSITION_EXTRA_FIELD = 0x5C76
SPECIAL_7FFF = 0x7FFF
HIGH_REMAP_BASE = 0x7FF9
HIGH_REMAP_END_EXCLUSIVE = 0x7FFF
HIGH_REMAP_DELTA_TABLE_LITERAL = 0x00336AB8
HIGH_REMAP_ENTRANCE_TABLE_LITERAL = 0x00336ABC
HIGH_REMAP_LIMIT_LITERAL = 0x00336AB4


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


def unsigned_16(value: int) -> int:
    return value & 0xFFFF


def classify_exit_value(value: int) -> str:
    if value == SPECIAL_7FFF:
        return "special_0x7fff"
    if HIGH_REMAP_BASE <= value < HIGH_REMAP_END_EXCLUSIVE:
        return "high_remap_0x7ff9_to_0x7ffe"
    if value < HIGH_REMAP_BASE:
        return "direct_transition_value"
    return "other_unhandled_signed_value"


def scene_exit_values(path: Path) -> tuple[list[dict[str, Any]], Counter[str]]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    rows: list[dict[str, Any]] = []
    counts: Counter[str] = Counter()
    for record in payload.get("records", []):
        scene_path = str(record.get("scene_path", ""))
        for setup in record.get("setups", []):
            setup_index = int(setup.get("index", -1))
            for command in setup.get("commands", []):
                if int(command.get("command_id", -1)) != 0x13:
                    continue
                decoded = command.get("decoded")
                if not isinstance(decoded, dict):
                    continue
                for exit_record in decoded.get("values", []):
                    if not isinstance(exit_record, dict):
                        continue
                    value = int(exit_record.get("value", 0))
                    category = classify_exit_value(value)
                    counts[category] += 1
                    rows.append(
                        {
                            "scene": scene_path,
                            "setup": setup_index,
                            "index": int(exit_record.get("index", -1)),
                            "offset": exit_record.get("offset", 0),
                            "offset_hex": f"0x{int(exit_record.get('offset', 0)):x}",
                            "value": value,
                            "value_hex": f"0x{unsigned_16(value):04x}",
                            "category": category,
                        }
                    )
    return rows, counts


def transition_helper_summary(path: Path) -> dict[str, Any]:
    if not path.is_file():
        return {
            "report": str(path),
            "status": "missing",
            "helpers": {},
            "field_roles": {},
        }
    payload = json.loads(path.read_text(encoding="utf-8"))
    helpers: dict[str, Any] = {}
    for name, info in payload.get("helpers", {}).items():
        helpers[name] = {
            "entry": f"0x{info.get('entry', '')}",
            "role": info.get("role", ""),
            "call_count": info.get("call_count", 0),
            "caller_count": info.get("caller_count", 0),
            "arg1_counts": info.get("arg1_counts", {}),
            "arg2_counts": info.get("arg2_counts", {}),
            "arg3_counts": info.get("arg3_counts", {}),
        }
    return {
        "report": str(path),
        "status": "loaded",
        "helpers": helpers,
        "field_roles": payload.get("field_roles", {}),
        "total_call_count": payload.get("total_call_count", 0),
    }


def transition_tables_summary(path: Path) -> dict[str, Any]:
    if not path.is_file():
        return {
            "report": str(path),
            "status": "missing",
            "verified_entrance_table_count": 0,
            "asset_high_remap_use_count": 0,
        }
    payload = json.loads(path.read_text(encoding="utf-8"))
    return {
        "report": str(path),
        "status": "loaded",
        "source": "code.bin tables plus native ZSI high-remap asset coverage",
        "verified_entrance_table_count": int(payload.get("verified_entrance_table_count", 0)),
        "asset_high_remap_use_count": len(payload.get("asset_high_remap_uses", [])),
        "observed_high_remap_value_count": len(payload.get("observed_high_remap_exit_values", [])),
        "unobserved_high_remap_value_count": len(payload.get("unobserved_high_remap_delta_rows", [])),
        "remaining_gaps": payload.get("remaining_gaps", []),
    }


def build_report() -> dict[str, Any]:
    code = DEFAULT_CODE_BIN.read_bytes()
    high_delta_table = read_u32(code, HIGH_REMAP_DELTA_TABLE_LITERAL)
    high_entrance_table = read_u32(code, HIGH_REMAP_ENTRANCE_TABLE_LITERAL)
    high_limit = read_u32(code, HIGH_REMAP_LIMIT_LITERAL)
    high_count = HIGH_REMAP_END_EXCLUSIVE - HIGH_REMAP_BASE
    high_delta_rows = [
        {
            "exit_value": f"0x{HIGH_REMAP_BASE + index:04x}",
            "delta": read_u8(code, high_delta_table + index),
        }
        for index in range(high_count)
    ]
    entrance_rows = [
        {
            "index": index,
            "entrance": read_s16(code, high_entrance_table + index * 2),
        }
        for index in range(16)
    ]
    value_rows, value_counts = scene_exit_values(DEFAULT_SCENE_INDEX)
    high_value_rows = [row for row in value_rows if row["category"] != "direct_transition_value"]
    helper_summary = transition_helper_summary(DEFAULT_HELPER_SEMANTICS)
    tables_summary = transition_tables_summary(DEFAULT_TRANSITION_TABLES)
    return {
        "format": "oot3d_scene_command_exit_semantics_v1",
        "source_policy": {
            "native_code_source": str(DEFAULT_CODE_BIN),
            "native_asset_source": str(DEFAULT_SCENE_INDEX),
            "native_helper_semantics": str(DEFAULT_HELPER_SEMANTICS),
            "native_transition_tables": str(DEFAULT_TRANSITION_TABLES),
            "n64_policy": "N64 names may be used only as secondary hints; this report uses OOT3D code.bin and ZSI evidence.",
        },
        "handler": {
            "command_id": "0x13",
            "handler": f"0x{EXIT_HANDLER:08x}",
            "installs_table_at_play_offset": f"0x{EXIT_TABLE_PLAY_OFFSET:04x}",
        },
        "consumer": {
            "entry": f"0x{EXIT_CONSUMER:08x}",
            "reads": "s16 exit = *(play->exitList + collisionResult * 2 - 2)",
            "pending_transition_fields": {
                "value": f"play+0x{PENDING_TRANSITION_FIELD:04x}",
                "type": f"play+0x{PENDING_TRANSITION_TYPE_FIELD:04x}",
                "extra": f"play+0x{PENDING_TRANSITION_EXTRA_FIELD:04x}",
            },
            "direct_value_gate": f"signed exit < 0x{high_limit:04x}",
            "special_value": f"0x{SPECIAL_7FFF:04x}",
            "high_remap_range": f"0x{HIGH_REMAP_BASE:04x}..0x{HIGH_REMAP_END_EXCLUSIVE - 1:04x}",
            "high_remap_delta_table": f"0x{high_delta_table:08x}",
            "high_remap_entrance_table": f"0x{high_entrance_table:08x}",
        },
        "high_remap_delta_rows": high_delta_rows,
        "high_remap_entrance_table_sample": entrance_rows,
        "transition_helpers": helper_summary,
        "transition_tables": tables_summary,
        "asset_value_counts": dict(sorted(value_counts.items())),
        "asset_value_total": len(value_rows),
        "non_direct_asset_values": high_value_rows,
        "remaining_gaps": [
            "Promote FUN_003348E8 and FUN_003716F0 to final source names only after matching surrounding transition state machine roles.",
            "The high-remap entrance table is now emitted through the asset-proven window; total extent beyond that window is not promoted.",
            "Classify high-remap values not observed in native ZSI exit lists as reserved or runtime-only before promoting per-value names.",
        ],
    }


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_markdown(path: Path, payload: dict[str, Any]) -> None:
    lines = [
        "# Scene Command Exit Semantics",
        "",
        "Generated from OOT3D `code.bin` and native ZSI scene indices.",
        "",
        "## Summary",
        "",
        f"- Handler: `{payload['handler']['handler']}` installs `0x13` table at `{payload['handler']['installs_table_at_play_offset']}`.",
        f"- Consumer: `{payload['consumer']['entry']}` reads `{payload['consumer']['reads']}`.",
        f"- Direct gate: `{payload['consumer']['direct_value_gate']}`",
        f"- Special value: `{payload['consumer']['special_value']}`",
        f"- High remap range: `{payload['consumer']['high_remap_range']}`",
        f"- Asset exit values: {payload['asset_value_total']} `{json.dumps(payload['asset_value_counts'], sort_keys=True)}`",
        f"- Transition helper report: `{payload['transition_helpers']['report']}` (`{payload['transition_helpers']['status']}`)",
        f"- Transition table report: `{payload['transition_tables']['report']}` (`{payload['transition_tables']['status']}`)",
        "",
        "## High Remap Deltas",
        "",
        "| Exit value | Delta |",
        "| --- | ---: |",
    ]
    for row in payload["high_remap_delta_rows"]:
        lines.append(f"| `{row['exit_value']}` | {row['delta']} |")

    lines.extend(
        [
            "",
            "## Entrance Table Sample",
            "",
            "| Index | Entrance |",
            "| ---: | ---: |",
        ]
    )
    for row in payload["high_remap_entrance_table_sample"]:
        lines.append(f"| {row['index']} | {row['entrance']} |")

    lines.extend(
        [
            "",
            "## Extracted Transition Tables",
            "",
            f"- Source: {payload['transition_tables'].get('source', 'unavailable')}",
            f"- Asset high-remap uses: {payload['transition_tables'].get('asset_high_remap_use_count', 0)}",
            f"- Observed high-remap values: {payload['transition_tables'].get('observed_high_remap_value_count', 0)}",
            f"- Unobserved high-remap values: {payload['transition_tables'].get('unobserved_high_remap_value_count', 0)}",
            f"- Verified entrance-table rows emitted: {payload['transition_tables'].get('verified_entrance_table_count', 0)}",
        ]
    )

    lines.extend(
        [
            "",
            "## Transition Request Helpers",
            "",
            "| Helper | Entry | Role | Calls | Key argument evidence |",
            "| --- | --- | --- | ---: | --- |",
        ]
    )
    for name, info in payload["transition_helpers"].get("helpers", {}).items():
        arg2_counts = json.dumps(info.get("arg2_counts", {}), sort_keys=True)
        arg3_counts = json.dumps(info.get("arg3_counts", {}), sort_keys=True)
        lines.append(
            f"| `{name}` | `{info['entry']}` | `{info['role']}` | {info['call_count']} | "
            f"`arg2={arg2_counts}` `arg3={arg3_counts}` |"
        )

    lines.extend(["", "Runtime field roles from helper bodies and transition consumer:", ""])
    for field, info in payload["transition_helpers"].get("field_roles", {}).items():
        lines.append(f"- `{field}`: {info.get('role', '')}")

    lines.extend(["", "## Non-Direct Asset Values", ""])
    if payload["non_direct_asset_values"]:
        lines.extend(["| Scene | Setup | Index | Value | Category |", "| --- | ---: | ---: | --- | --- |"])
        for row in payload["non_direct_asset_values"]:
            lines.append(
                f"| `{row['scene']}` | {row['setup']} | {row['index']} | `{row['value_hex']}` | `{row['category']}` |"
            )
    else:
        lines.append("- None observed in the current ZSI index.")

    lines.extend(["", "## Remaining Gaps", ""])
    for gap in payload["remaining_gaps"]:
        lines.append(f"- {gap}")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")


def main() -> int:
    payload = build_report()
    write_json(DEFAULT_OUT_JSON, payload)
    write_markdown(DEFAULT_OUT_MD, payload)
    print(DEFAULT_OUT_JSON)
    print(DEFAULT_OUT_MD)
    print(json.dumps({"asset_value_counts": payload["asset_value_counts"]}, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
