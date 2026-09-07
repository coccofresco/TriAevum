from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
IMAGE_BASE = 0x00100000
DEFAULT_CODE_BIN = Path(r"E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin")
OUT_HEADER = ROOT / "include" / "oot3d" / "title_intro_player_resource_visibility.h"
OUT_SOURCE = ROOT / "src" / "code" / "z_title_intro_player_resource_visibility.c"
OUT_JSON = ROOT / "analysis" / "title_intro_player_resource_visibility.json"
OUT_MD = ROOT / "analysis" / "title_intro_player_resource_visibility.md"

DEBUG_SAVE_FUNCTION = 0x003791E0
DEBUG_SAVE_EQUIPMENT_LITERAL = 0x003793A4
PLAYER_INIT_ITEM_ACTION_MOV = 0x00191A1C
PLAYER_INITIAL_ACTION_COMPARE = 0x0034D68C
PLAYER_INITIAL_ACTION_ZERO = 0x0034D6A0
MODEL_GROUP_LOOKUP_POINTER_LITERAL = 0x0033B57C
MODEL_GROUP_TABLE_POINTER_LITERAL = 0x0032C3F8
MODEL_RESOURCE_POINTER_TABLE_LITERAL = 0x0032C3FC
BODY_VISIBILITY_TABLE_POINTER_LITERAL = 0x004C4794
LEFT_HAND_SEMANTIC_TABLE_POINTER_LITERAL = 0x002B7D6C
EQUIPMENT_MASK_TABLE_POINTER_LITERAL = 0x0034925C
EQUIPMENT_SHIFT_TABLE_POINTER_LITERAL = 0x00349260


class CodeBin:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.data = path.read_bytes()

    def offset(self, address: int, size: int) -> int:
        offset = address - IMAGE_BASE
        if offset < 0 or offset + size > len(self.data):
            raise ValueError(f"address 0x{address:08X} is outside code.bin")
        return offset

    def u8(self, address: int) -> int:
        return self.data[self.offset(address, 1)]

    def u16(self, address: int) -> int:
        return struct.unpack_from("<H", self.data, self.offset(address, 2))[0]

    def u32(self, address: int) -> int:
        return struct.unpack_from("<I", self.data, self.offset(address, 4))[0]


def unique(values: list[int]) -> list[int]:
    out: list[int] = []
    for value in values:
        if value != 0xFFFFFFFF and value not in out:
            out.append(value)
    return out


def decode(code: CodeBin) -> dict[str, object]:
    signatures = {
        DEBUG_SAVE_FUNCTION + 0x04: 0xE3A00000,
        DEBUG_SAVE_FUNCTION + 0x20: 0xE5860004,
        DEBUG_SAVE_FUNCTION + 0x3C: 0xE59F1180,
        DEBUG_SAVE_FUNCTION + 0x54: 0xE1C618BA,
        PLAYER_INIT_ITEM_ACTION_MOV: 0xE3A010FF,
        PLAYER_INITIAL_ACTION_COMPARE: 0xE35200FE,
        PLAYER_INITIAL_ACTION_ZERO: 0xA3A07000,
    }
    for address, expected in signatures.items():
        actual = code.u32(address)
        if actual != expected:
            raise ValueError(
                f"code signature mismatch at 0x{address:08X}: "
                f"expected 0x{expected:08X}, got 0x{actual:08X}"
            )

    equipment_word = code.u32(DEBUG_SAVE_EQUIPMENT_LITERAL)
    if equipment_word != 0x00001122:
        raise ValueError(f"unexpected title debug-save equipment word 0x{equipment_word:08X}")

    age_index = 0
    initial_action_index = 0
    model_group_lookup = code.u32(MODEL_GROUP_LOOKUP_POINTER_LITERAL)
    model_group = code.u8(model_group_lookup + initial_action_index)

    mask_table = code.u32(EQUIPMENT_MASK_TABLE_POINTER_LITERAL)
    shift_table = code.u32(EQUIPMENT_SHIFT_TABLE_POINTER_LITERAL)
    sword_mask = code.u16(mask_table)
    shield_mask = code.u16(mask_table + 2)
    sword_shift = code.u8(shift_table)
    shield_shift = code.u8(shift_table + 1)
    sword_value = (equipment_word & sword_mask) >> sword_shift
    shield_value = (equipment_word & shield_mask) >> shield_shift

    body_table = code.u32(BODY_VISIBILITY_TABLE_POINTER_LITERAL)
    body_ids = unique(
        [code.u32(body_table + slot * 8 + age_index * 4) for slot in range(4)]
    )

    model_group_table = code.u32(MODEL_GROUP_TABLE_POINTER_LITERAL)
    model_group_row_address = model_group_table + model_group * 5
    model_group_row = [code.u8(model_group_row_address + index) for index in range(5)]
    model_types = model_group_row[1:5]

    resource_pointer_table = code.u32(MODEL_RESOURCE_POINTER_TABLE_LITERAL)
    resource_records = [code.u32(resource_pointer_table + model_type * 4) for model_type in model_types]
    model_resource_ids = [code.u32(record + age_index * 4) for record in resource_records]

    left_semantic_table = code.u32(LEFT_HAND_SEMANTIC_TABLE_POINTER_LITERAL)
    left_semantic_ids = [code.u32(left_semantic_table + index * 4) for index in range(8)]
    left_hand_id = model_resource_ids[0]
    if left_hand_id not in left_semantic_ids:
        raise ValueError(f"left-hand resource id {left_hand_id} is absent from semantic table")
    right_hand_id = model_resource_ids[1]

    sheath_record = resource_records[2]
    if sword_value == 3:
        raise ValueError("title visibility decode reached the unimplemented giant-sword override branch")
    sheath_variant_address = sheath_record + shield_value * 0x10
    sheath_id = code.u32(sheath_variant_address + age_index * 4)
    waist_id = model_resource_ids[3]

    active_ids = unique(body_ids + [left_hand_id, right_hand_id, sheath_id, waist_id])
    if any(value > 0xFF for value in active_ids):
        raise ValueError(f"resource visibility id exceeds CMB u8 range: {active_ids}")

    return {
        "format": "oot3d_title_intro_player_resource_visibility_v1",
        "code_bin": {
            "path": str(code.path),
            "size": len(code.data),
            "sha256": hashlib.sha256(code.data).hexdigest(),
            "image_base": f"0x{IMAGE_BASE:08X}",
        },
        "state": {
            "age_index": age_index,
            "initial_action_index": initial_action_index,
            "model_group": model_group,
            "equipment_word": equipment_word,
            "sword_value": sword_value,
            "shield_value": shield_value,
        },
        "tables": {
            "body_visibility_table": {
                "pointer_literal": f"0x{BODY_VISIBILITY_TABLE_POINTER_LITERAL:08X}",
                "address": f"0x{body_table:08X}",
                "selected_ids": body_ids,
            },
            "model_group_table": {
                "pointer_literal": f"0x{MODEL_GROUP_TABLE_POINTER_LITERAL:08X}",
                "address": f"0x{model_group_table:08X}",
                "row_address": f"0x{model_group_row_address:08X}",
                "row": model_group_row,
                "model_types": model_types,
            },
            "model_resource_pointer_table": {
                "pointer_literal": f"0x{MODEL_RESOURCE_POINTER_TABLE_LITERAL:08X}",
                "address": f"0x{resource_pointer_table:08X}",
                "record_addresses": [f"0x{value:08X}" for value in resource_records],
                "base_resource_ids": model_resource_ids,
            },
            "left_hand_semantic_table": {
                "pointer_literal": f"0x{LEFT_HAND_SEMANTIC_TABLE_POINTER_LITERAL:08X}",
                "address": f"0x{left_semantic_table:08X}",
                "ids": left_semantic_ids,
            },
            "sheath_variant": {
                "record_address": f"0x{sheath_record:08X}",
                "selected_address": f"0x{sheath_variant_address:08X}",
                "selected_id": sheath_id,
            },
        },
        "active_resource_ids": active_ids,
        "active_resource_ids_sorted": sorted(active_ids),
        "validation": {
            "azahar_slot6_active_resource_ids": [0, 13, 20, 45, 46, 47],
            "matches_validation": sorted(active_ids) == [0, 13, 20, 45, 46, 47],
            "validation_is_not_runtime_input": True,
        },
        "source": (
            "OOT3D code.bin Sram_InitDebugSave 0x003791E0, Player_Init/FUN_0034D688 initial "
            "action route, Player model-group/resource tables, and Player draw visibility consumers "
            "0x004C4560/0x002B7CF4/0x004C70C4"
        ),
    }


def write_header(data: dict[str, object]) -> None:
    active_ids = data["active_resource_ids"]
    header = f"""#ifndef OOT3D_TITLE_INTRO_PLAYER_RESOURCE_VISIBILITY_H
#define OOT3D_TITLE_INTRO_PLAYER_RESOURCE_VISIBILITY_H

#include "oot3d/types.h"

#define OOT3D_TITLE_INTRO_PLAYER_RESOURCE_VISIBILITY_MAX_IDS 8u

typedef struct Oot3dTitleIntroPlayerResourceVisibilityRow {{
    u8 valid;
    u8 ageIndex;
    u8 initialActionIndex;
    u8 modelGroup;
    u8 swordValue;
    u8 shieldValue;
    u8 activeResourceIdCount;
    u8 reserved;
    u16 debugSaveEquipmentWord;
    u8 activeResourceIds[OOT3D_TITLE_INTRO_PLAYER_RESOURCE_VISIBILITY_MAX_IDS];
    const char* source;
}} Oot3dTitleIntroPlayerResourceVisibilityRow;

extern const Oot3dTitleIntroPlayerResourceVisibilityRow gOot3dTitleIntroPlayerResourceVisibilityRow;

u32 Oot3d_TitleIntroPlayerBuildResourceVisibility(
    const Oot3dTitleIntroPlayerResourceVisibilityRow* row,
    u8* resourceVisibility,
    u32 resourceCount);

#endif
"""
    OUT_HEADER.write_text(header, encoding="ascii", newline="\n")


def write_source(data: dict[str, object]) -> None:
    state = data["state"]
    active_ids = list(data["active_resource_ids"])
    padded_ids = active_ids + [0xFF] * (8 - len(active_ids))
    id_text = ", ".join(f"{value}u" for value in padded_ids)
    source = data["source"].replace('"', '\\"')
    text = f"""/* Generated by build_title_intro_player_resource_visibility.py from OOT3D code.bin. */

#include "oot3d/title_intro_player_resource_visibility.h"

const Oot3dTitleIntroPlayerResourceVisibilityRow gOot3dTitleIntroPlayerResourceVisibilityRow = {{
    1u,
    {state['age_index']}u,
    {state['initial_action_index']}u,
    {state['model_group']}u,
    {state['sword_value']}u,
    {state['shield_value']}u,
    {len(active_ids)}u,
    0u,
    0x{state['equipment_word']:04X}u,
    {{ {id_text} }},
    "{source}",
}};

u32 Oot3d_TitleIntroPlayerBuildResourceVisibility(
    const Oot3dTitleIntroPlayerResourceVisibilityRow* row,
    u8* resourceVisibility,
    u32 resourceCount) {{
    u32 index;
    u32 applied = 0u;
    if (row == 0 || row->valid == 0u || resourceVisibility == 0) {{
        return 0u;
    }}
    for (index = 0u; index < resourceCount; ++index) {{
        resourceVisibility[index] = 0u;
    }}
    for (index = 0u; index < row->activeResourceIdCount; ++index) {{
        const u32 resourceId = row->activeResourceIds[index];
        if (resourceId < resourceCount && resourceVisibility[resourceId] == 0u) {{
            resourceVisibility[resourceId] = 1u;
            ++applied;
        }}
    }}
    return applied;
}}
"""
    OUT_SOURCE.write_text(text, encoding="ascii", newline="\n")


def write_markdown(data: dict[str, object]) -> None:
    state = data["state"]
    tables = data["tables"]
    lines = [
        "# OOT3D Title Intro Player Resource Visibility",
        "",
        f"- `code.bin` SHA-256: `{data['code_bin']['sha256']}`",
        f"- Native title equipment word: `0x{state['equipment_word']:04X}`",
        f"- Age/action/model group: `{state['age_index']}/{state['initial_action_index']}/{state['model_group']}`",
        f"- Sword/shield selectors: `{state['sword_value']}/{state['shield_value']}`",
        f"- Active CMB resource IDs: `{', '.join(str(value) for value in sorted(data['active_resource_ids']))}`",
        f"- Azahar validation matches: `{str(data['validation']['matches_validation']).lower()}`",
        "",
        "## Native derivation",
        "",
        "- `Sram_InitDebugSave` at `0x003791E0` writes code.bin literal `0x1122` to `SaveContext+0x8A` and zero to `SaveContext+0x04` (adult age index).",
        "- `Player_Init` passes item selector `0xFF`; the native initial-action route normalizes it to action index `0`, whose table entry selects model group `3`.",
        f"- Body resource table `{tables['body_visibility_table']['address']}` selects `{tables['body_visibility_table']['selected_ids']}` after duplicate removal.",
        f"- Model-group row `{tables['model_group_table']['row_address']}` is `{tables['model_group_table']['row']}` and resolves left/right/sheath/waist through the native resource pointer table.",
        f"- Shield selector `{state['shield_value']}` chooses sheath record `{tables['sheath_variant']['selected_address']}`, resource ID `{tables['sheath_variant']['selected_id']}`.",
        "- The emulator table is used only to validate the derived result; generated runtime values come from code.bin.",
    ]
    OUT_MD.write_text("\n".join(lines) + "\n", encoding="ascii", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--code-bin", type=Path, default=DEFAULT_CODE_BIN)
    args = parser.parse_args()
    code = CodeBin(args.code_bin)
    data = decode(code)
    OUT_JSON.write_text(json.dumps(data, indent=2) + "\n", encoding="ascii", newline="\n")
    write_header(data)
    write_source(data)
    write_markdown(data)
    print(json.dumps({
        "active_resource_ids": data["active_resource_ids_sorted"],
        "matches_validation": data["validation"]["matches_validation"],
    }))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
