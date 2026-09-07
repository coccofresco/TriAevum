#!/usr/bin/env python3
"""Analyze OOT3D pause item UI semantic-table consumers."""

from __future__ import annotations

import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CODE_BIN = ROOT.parent / "work" / "extract" / "exefs" / "code.bin"
BUILD_SUMMARY = ROOT / "metadata" / "build_summary.json"
SEMANTIC_HEADER = ROOT / "include" / "oot3d" / "actor_object_semantics.h"
OUT_MD = ROOT / "analysis" / "pause_item_semantics.md"

DECOMPILED = ROOT / "ghidra_export" / "decompiled"
CONSUMERS = ("002f7b44", "00438984")
HELPERS = (
    {
        "entry": "002e9d78",
        "name": "oot3d_pause_item_slot_resolve_icon_id",
        "role": "resolves pause item/equipment/quest slot ownership and special icon ids",
    },
    {
        "entry": "002eb304",
        "name": "oot3d_pause_item_slot_get_position",
        "role": "computes x/y coordinates for pause item grid slots",
    },
    {
        "entry": "002eb72c",
        "name": "oot3d_pause_item_equip_update_selected_detail",
        "role": "updates or clears the selected-item detail panel during item equip and c-button target selection",
    },
    {
        "entry": "0033c25c",
        "name": "oot3d_pause_item_inventory_sync_and_refresh",
        "role": "syncs pause item inventory/equipment slot state and refreshes 24 item-grid icons",
    },
    {
        "entry": "0045598c",
        "name": "oot3d_pause_ui_init",
        "role": "initializes the main pause UI render object and all pause panel sub-initializers",
    },
    {
        "entry": "0041e968",
        "name": "oot3d_pause_ui_update",
        "role": "runs the per-frame pause UI dispatcher across touch buttons, item/equipment pages, panels, render state, and overlay feedback",
    },
    {
        "entry": "0042df3c",
        "name": "oot3d_pause_touch_button_state_update",
        "role": "updates lower-screen pause touch button availability, hit-test state transitions, item sync, and widget refresh",
    },
    {
        "entry": "00449a38",
        "name": "oot3d_pause_item_inventory_page_init",
        "role": "initializes pause item inventory page render state, per-slot defaults, item sync, and pending slot swap state",
    },
    {
        "entry": "004439c4",
        "name": "oot3d_pause_item_equipment_icon_layout_refresh",
        "role": "refreshes pause item/equipment action icon layouts, visibility, and render entries for active touch-button states",
    },
    {
        "entry": "0046acb8",
        "name": "oot3d_pause_equipment_panel_init",
        "role": "initializes the pause equipment panel render buffers, icon groups, widgets, and default state",
    },
    {
        "entry": "0046b554",
        "name": "oot3d_pause_item_panel_init",
        "role": "initializes the pause item panel render buffers, icon groups, widgets, and item-page helper state",
    },
    {
        "entry": "002eb3d8",
        "name": "oot3d_pause_icon_set_layout_with_offset",
        "role": "sets icon quad layout with x/y offsets for disabled or animated placement",
    },
    {
        "entry": "002f8ee4",
        "name": "oot3d_pause_icon_group_init",
        "role": "initializes a pause icon group with per-slot buffers, default ids, and optional mode-specific icon state",
    },
    {
        "entry": "002f8160",
        "name": "oot3d_pause_icon_refresh_widget_state",
        "role": "refreshes visible pause icon widget draw/highlight/disabled state",
    },
    {
        "entry": "00424324",
        "name": "oot3d_pause_item_page_update",
        "role": "updates the pause item page state machine, input, icon refresh, and selected detail",
    },
    {
        "entry": "00438740",
        "name": "oot3d_pause_item_page_handle_cursor_input",
        "role": "handles item page directional input and confirmed equipment-slot actions",
    },
    {
        "entry": "00438f20",
        "name": "oot3d_pause_item_page_update_touch_slot",
        "role": "hit-tests touch rectangles for item page slots and confirms touched equipment slots",
    },
    {
        "entry": "00433ab4",
        "name": "oot3d_pause_item_equip_page_update",
        "role": "updates the pause item/equipment equip state machine and animated item swaps",
    },
    {
        "entry": "002ec3e4",
        "name": "oot3d_pause_item_equip_grid_update",
        "role": "handles 6x4 item equip grid navigation, selection, cancellation, and selected icon placement",
    },
    {
        "entry": "002eba9c",
        "name": "oot3d_pause_item_equip_grid_update_touch_slot",
        "role": "hit-tests touch rectangles for the item equip grid and updates selected or dragged equip slots",
    },
    {
        "entry": "004456a8",
        "name": "oot3d_pause_item_equip_cbutton_target_update",
        "role": "updates a 2x2 equip target selector for C-button/equipment assignment",
    },
    {
        "entry": "00480eb8",
        "name": "oot3d_pause_dungeon_item_panel_init",
        "role": "initializes a pause dungeon-item panel with map, compass, boss-key, and skull-token icons",
    },
)

TABLES = [
    {
        "name": "oot3d_candidate_semantic_u32_run_00504ae0",
        "address": 0x00504AE0,
        "entries": 27,
        "consumer": "002f7b44",
        "role": "initial per-slot item/icon ids for pause item grid",
    },
    {
        "name": "oot3d_candidate_semantic_u32_run_00504e5c",
        "address": 0x00504E5C,
        "entries": 27,
        "consumer": "00438984",
        "role": "selected-slot item/icon ids for pause item detail",
    },
]


def parse_enum_names() -> dict[str, dict[int, str]]:
    text = SEMANTIC_HEADER.read_text(encoding="utf-8")
    domains = {
        "ItemID": {},
        "GetItemID": {},
        "GetItemDrawID": {},
    }
    current: str | None = None
    for line in text.splitlines():
        enum_match = re.match(r"typedef enum Oot3d(\w+) \{", line)
        if enum_match:
            current = enum_match.group(1)
            continue
        if current and line.startswith("} "):
            current = None
            continue
        if current not in domains:
            continue
        value_match = re.match(r"\s*([A-Z0-9_]+)\s*=\s*0x([0-9A-Fa-f]+),", line)
        if value_match:
            domains[current][int(value_match.group(2), 16)] = value_match.group(1)
    return domains


def read_u32_table(data: bytes, base: int, address: int, count: int) -> list[int]:
    offset = address - base
    if offset < 0 or offset + count * 4 > len(data):
        raise ValueError(f"table 0x{address:08x} outside code image")
    return [int.from_bytes(data[offset + i * 4 : offset + i * 4 + 4], "little") for i in range(count)]


def call_lines(path: Path) -> list[str]:
    text = path.read_text(encoding="utf-8")
    patterns = (
        "FUN_002f8d74",
        "FUN_002f8d40",
        "FUN_002e9b00",
        "FUN_002e9a3c",
        "FUN_002e9d78",
        "FUN_002eb304",
        "FUN_002eb72c",
        "FUN_0033c25c",
        "FUN_0045598c",
        "FUN_0041e968",
        "FUN_0042df3c",
        "FUN_004439c4",
        "FUN_00449a38",
        "FUN_0046acb8",
        "FUN_0046b554",
        "FUN_002eb3d8",
        "FUN_002f8ee4",
        "FUN_002f8160",
        "FUN_00424324",
        "FUN_00438740",
        "FUN_00438f20",
        "FUN_00433ab4",
        "FUN_002ec3e4",
        "FUN_002eba9c",
        "FUN_004456a8",
        "FUN_00480eb8",
        "oot3d_pause_icon_set_item_id",
        "oot3d_pause_icon_group_init",
        "oot3d_pause_icon_set_layout",
        "oot3d_pause_icon_set_layout_with_offset",
        "oot3d_pause_icon_refresh_widget_state",
        "oot3d_pause_item_page_update",
        "oot3d_pause_item_page_handle_cursor_input",
        "oot3d_pause_item_page_update_touch_slot",
        "oot3d_pause_item_equip_page_update",
        "oot3d_pause_item_equip_grid_update",
        "oot3d_pause_item_equip_grid_update_touch_slot",
        "oot3d_pause_item_equip_cbutton_target_update",
        "oot3d_pause_dungeon_item_panel_init",
        "oot3d_pause_item_slot_resolve_icon_id",
        "oot3d_pause_item_slot_get_position",
        "oot3d_pause_item_equip_update_selected_detail",
        "oot3d_pause_item_inventory_sync_and_refresh",
        "oot3d_pause_ui_init",
        "oot3d_pause_ui_update",
        "oot3d_pause_touch_button_state_update",
        "oot3d_pause_item_equipment_icon_layout_refresh",
        "oot3d_pause_item_inventory_page_init",
        "oot3d_pause_equipment_panel_init",
        "oot3d_pause_item_panel_init",
        "oot3d_pause_detail_set_item_id",
        "oot3d_pause_detail_load_resource",
        "oot3d_ptr_candidate_semantic_u32_run_00504ae0",
        "oot3d_ptr_candidate_semantic_u32_run_00504e5c",
    )
    rows = []
    for lineno, line in enumerate(text.splitlines(), 1):
        if any(pattern in line for pattern in patterns):
            rows.append(f"{lineno}: {line.strip()}")
    return rows


def decompiled_file_for_entry(entry: str) -> Path:
    matches = sorted(DECOMPILED.glob(f"*_{entry}_*.c"))
    if len(matches) != 1:
        raise FileNotFoundError(f"expected one decompiled file for {entry}, found {len(matches)}")
    return matches[0]


def value_name(domains: dict[str, dict[int, str]], value: int) -> str:
    names = []
    for domain in ("ItemID", "GetItemID", "GetItemDrawID"):
        name = domains[domain].get(value)
        if name:
            names.append(f"{domain}:{name}")
    return ", ".join(names) if names else "-"


def main() -> int:
    build = json.loads(BUILD_SUMMARY.read_text(encoding="utf-8"))
    base = int(build["code_base"], 16)
    data = CODE_BIN.read_bytes()
    domains = parse_enum_names()

    lines = [
        "# Pause Item Semantics",
        "",
        "Generated from `scripts/analyze_pause_item_semantics.py`.",
        "",
        f"- Code image: `{CODE_BIN.relative_to(ROOT.parent).as_posix()}`",
        f"- Code base: `{build['code_base']}`",
        "- N64 anchors: `gItemIcons`, `KaleidoScope_DrawItemSelect`, `KaleidoScope_UpdateItemEquip`, "
        "and pause `cursorItem`/`cursorSlot` semantics from `z_kaleido_scope`.",
        "",
        "## Semantic Tables",
        "",
    ]
    for table in TABLES:
        values = read_u32_table(data, base, table["address"], table["entries"])
        lines.extend(
            [
                f"### `{table['name']}`",
                "",
                f"- Address: `0x{table['address']:08X}`",
                f"- Consumer: `{table['consumer']}`",
                f"- Interpreted role: {table['role']}",
                "",
                "| Slot | Value | N64-derived names |",
                "| --- | --- | --- |",
            ]
        )
        for slot, value in enumerate(values):
            lines.append(f"| `{slot}` | `0x{value:02X}` | {value_name(domains, value)} |")
        lines.append("")

    lines.extend(
        [
            "## Helper Functions",
            "",
            "| Entry | Name | Role |",
            "| --- | --- | --- |",
        ]
    )
    for helper in HELPERS:
        lines.append(f"| `{helper['entry']}` | `{helper['name']}` | {helper['role']} |")
    lines.append("")

    lines.extend(
        [
            "## Consumer Call Evidence",
            "",
            "| Entry | Evidence |",
            "| --- | --- |",
        ]
    )
    for entry in CONSUMERS:
        path = decompiled_file_for_entry(entry)
        evidence = "<br>".join(f"`{line}`" for line in call_lines(path)[:18])
        lines.append(f"| `{entry}` | {evidence} |")
    lines.append("")

    OUT_MD.write_text("\n".join(lines), encoding="utf-8")
    print(json.dumps({"report": str(OUT_MD.relative_to(ROOT)), "tables": len(TABLES)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
