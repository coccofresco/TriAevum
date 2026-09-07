from __future__ import annotations

import hashlib
import struct
from pathlib import Path

from .binary import ParseError
from .zar import ZarArchive, ZarFile


FORMAT = "oot3d_player_animation_group_native_contract_v1"
CODE_IMAGE_BASE = 0x00100000
ANIMATION_GROUP_POINTER_LITERAL = 0x0034D680
ANIMATION_GROUP_COUNT = 54
ANIMATION_TYPE_COUNT = 6
AGE_PROPERTIES_POINTER_LITERAL = 0x00250AA8
AGE_PROPERTIES_RECORD_STRIDE = 0x134
CHILD_AGE_RECORD_INDEX = 1

SEMANTIC_GROUP_NATIVE_STEMS = {
    "idle": (
        "nml_wait_free", "nml_wait", "nml_wait", "ft_wait_long",
        "nml_wait_free", "nml_wait_free",
    ),
    "walk": (
        "nml_walk_free", "nml_walk", "nml_walk", "ft_walk_long",
        "nml_walk_free", "nml_walk_free",
    ),
    "run": (
        "nml_run_free", "ft_run", "nml_run", "ft_run_long",
        "nml_run_free", "nml_run_free",
    ),
    "walk_end_left": (
        "nml_walk_endl_free", "nml_walk_endl", "nml_walk_endl", "ft_walk_endl_long",
        "nml_walk_endl_free", "nml_walk_endl_free",
    ),
    "walk_end_right": (
        "nml_walk_endr_free", "nml_walk_endr", "nml_walk_endr", "ft_walk_endr_long",
        "nml_walk_endr_free", "nml_walk_endr_free",
    ),
    "landing": (
        "nml_landing_free", "nml_landing", "nml_landing", "nml_landing_free",
        "nml_landing_free", "nml_landing_free",
    ),
    "short_landing": (
        "nml_short_landing_free", "nml_short_landing", "nml_short_landing",
        "nml_short_landing_free", "nml_short_landing_free", "nml_short_landing_free",
    ),
    "landing_roll": (
        "nml_landing_roll_free", "nml_landing_roll", "nml_landing_roll",
        "nml_landing_roll_free", "nml_landing_roll_free", "nml_landing_roll_free",
    ),
    "defense": (
        "nml_defense_free", "nml_defense", "nml_defense", "nml_defense_free",
        "bow_defense", "nml_defense_free",
    ),
    "defense_wait": (
        "nml_defense_wait_free", "nml_defense_wait", "nml_defense_wait",
        "nml_defense_wait_free", "bow_defense_wait", "nml_defense_wait_free",
    ),
    "defense_end": (
        "nml_defense_end_free", "nml_defense_end", "nml_defense_end",
        "nml_defense_end_free", "nml_defense_end_free", "nml_defense_end_free",
    ),
    "jump_climb_hold": (
        "nml_hang_hold_free",
        "nml_hang_hold",
        "nml_hang_hold",
        "nml_hang_hold_free",
        "nml_hang_hold_free",
        "nml_hang_hold_free",
    ),
    "jump_climb_wait": (
        "nml_hang_wait_free",
        "nml_hang_wait",
        "nml_hang_wait",
        "nml_hang_wait_free",
        "nml_hang_wait_free",
        "nml_hang_wait_free",
    ),
    "jump_climb_up": (
        "nml_hang_up_free",
        "nml_hang_up",
        "nml_hang_up",
        "nml_hang_up_free",
        "nml_hang_up_free",
        "nml_hang_up_free",
    ),
}

DIRECT_SEMANTIC_CSAB_STEMS = {
    "auto_jump": "nml_jump",
    "auto_jump_up": "nml_jump_up",
    "fall": "nml_fall",
    "fall_wait": "nml_fall_wait",
    "landing_wait": "nml_landing_wait",
    "run_auto_jump": "nml_run_jump",
    "run_auto_jump_end": "nml_run_jump_end",
}

CHILD_AGE_CLIMB_CLIPS = {
    "regular_climb_start_front": (0x104, "cl_nml_climb_starta"),
    "regular_climb_start_back": (0x108, "cl_nml_climb_startb"),
    "regular_climb_up_left": (0x10C, "cl_nml_climb_upl"),
    "regular_climb_up_right": (0x110, "cl_nml_climb_upr"),
    "free_climb_up_left": (0x114, "nml_fclimb_upl"),
    "free_climb_up_right": (0x118, "nml_fclimb_upr"),
    "free_climb_side_left": (0x11C, "nml_fclimb_sidel"),
    "free_climb_side_right": (0x120, "nml_fclimb_sider"),
    "regular_climb_end_front_left": (0x124, "cl_nml_climb_endal"),
    "regular_climb_end_front_right": (0x128, "cl_nml_climb_endar"),
    "regular_climb_end_back_right": (0x12C, "cl_nml_climb_endbr"),
    "regular_climb_end_back_left": (0x130, "cl_nml_climb_endbl"),
}

FREE_CLIMB_START_BACK_INSTRUCTION = (0x001D0200, 0xE3A030AD, 0xAD)
FREE_CLIMB_START_FRONT_INSTRUCTION = (0x00351780, 0x13A060AE, 0xAE)

N64_SCAFFOLD_BINDING_GROUPS = {
    "jump_climb_hold",
    "jump_climb_wait",
    "jump_climb_up",
}


def _code_offset(address: int, size: int, code: bytes, source: Path) -> int:
    offset = address - CODE_IMAGE_BASE
    if offset < 0 or offset + size > len(code):
        raise ParseError(
            f"{source}: runtime range 0x{address:08X}+0x{size:X} lies outside code.bin"
        )
    return offset


def _u32(code: bytes, address: int, source: Path) -> int:
    return struct.unpack_from("<I", code, _code_offset(address, 4, code, source))[0]


def _typed_files(archive: ZarArchive, type_name: str) -> list[ZarFile]:
    suffix = f".{type_name}"
    return sorted(
        (
            file for file in archive.files
            if file.type_name == type_name or file.name.lower().endswith(suffix)
        ),
        key=lambda file: (
            file.type_local_index is None,
            file.type_local_index if file.type_local_index is not None else file.index,
        ),
    )


def _member_stem(member: str) -> str:
    return Path(member).stem.lower()


def build_player_animation_group_native_contract(
    code_bin: Path,
    actor_zar: Path,
) -> dict[str, object]:
    code = code_bin.read_bytes()
    archive = ZarArchive.from_path(actor_zar)
    csab_files = _typed_files(archive, "csab")
    if not csab_files:
        raise ParseError(f"{actor_zar}: actor archive contains no CSAB files")
    csab_by_type_index = {
        file.type_local_index: file
        for file in csab_files
        if file.type_local_index is not None
    }

    table_address = _u32(code, ANIMATION_GROUP_POINTER_LITERAL, code_bin)
    table_size = ANIMATION_GROUP_COUNT * ANIMATION_TYPE_COUNT * 4
    table_offset = _code_offset(table_address, table_size, code, code_bin)
    rows: list[dict[str, object]] = []
    row_stems: list[tuple[str, ...]] = []
    for group_index in range(ANIMATION_GROUP_COUNT):
        indices = list(
            struct.unpack_from(
                f"<{ANIMATION_TYPE_COUNT}I",
                code,
                table_offset + group_index * ANIMATION_TYPE_COUNT * 4,
            )
        )
        members: list[str] = []
        stems: list[str] = []
        for csab_index in indices:
            if csab_index >= len(csab_files):
                raise ParseError(
                    f"{code_bin}: player animation group {group_index} references "
                    f"CSAB type-local index {csab_index}, but {actor_zar} has {len(csab_files)} CSABs"
                )
            member = csab_files[csab_index].name
            members.append(member)
            stems.append(_member_stem(member))
        row_stems.append(tuple(stems))
        rows.append(
            {
                "group_index": group_index,
                "runtime_address": table_address + group_index * ANIMATION_TYPE_COUNT * 4,
                "csab_type_local_indices": indices,
                "csab_members": members,
            }
        )

    semantic_groups: list[dict[str, object]] = []
    semantic_bindings: list[dict[str, object]] = []
    for semantic_group, expected_stems in SEMANTIC_GROUP_NATIVE_STEMS.items():
        matches = [index for index, stems in enumerate(row_stems) if stems == expected_stems]
        if len(matches) != 1:
            raise ParseError(
                f"{code_bin}: expected exactly one native {semantic_group} row, found {len(matches)}"
            )
        group_index = matches[0]
        row = rows[group_index]
        semantic_groups.append(
            {
                "semantic_group": semantic_group,
                "oot3d_group_index": group_index,
                "runtime_address": row["runtime_address"],
                "csab_type_local_indices": row["csab_type_local_indices"],
                "csab_members": row["csab_members"],
            }
        )
        if semantic_group not in N64_SCAFFOLD_BINDING_GROUPS:
            continue
        bindings_by_name: dict[str, dict[str, object]] = {}
        for animation_type, (stem, csab_index, member) in enumerate(
            zip(
                expected_stems,
                row["csab_type_local_indices"],
                row["csab_members"],
            )
        ):
            free_suffix = "_free" if stem.endswith("_free") else ""
            n64_name = f"gPlayerAnim_link_normal_{semantic_group}{free_suffix}"
            binding = bindings_by_name.setdefault(
                n64_name,
                {
                    "n64_name": n64_name,
                    "csab_name": member,
                    "oot3d_group_index": group_index,
                    "csab_type_local_index": csab_index,
                    "animation_type_indices": [],
                },
            )
            if binding["csab_name"] != member or binding["csab_type_local_index"] != csab_index:
                raise ParseError(
                    f"{code_bin}: native animation types disagree for semantic binding {n64_name}"
                )
            binding["animation_type_indices"].append(animation_type)
        semantic_bindings.extend(bindings_by_name.values())

    direct_semantic_clips: list[dict[str, object]] = []
    for semantic_clip, expected_stem in DIRECT_SEMANTIC_CSAB_STEMS.items():
        matches = [file for file in csab_files if _member_stem(file.name) == expected_stem]
        if len(matches) != 1:
            raise ParseError(
                f"{actor_zar}: expected exactly one native {semantic_clip} CSAB member, "
                f"found {len(matches)}"
            )
        match = matches[0]
        if match.type_local_index is None:
            raise ParseError(f"{actor_zar}: native {semantic_clip} CSAB has no type-local index")
        direct_semantic_clips.append(
            {
                "semantic_clip": semantic_clip,
                "csab_type_local_index": match.type_local_index,
                "csab_member": match.name,
                "resolution": "unique_native_zar_member_stem",
            }
        )

    age_table_address = _u32(code, AGE_PROPERTIES_POINTER_LITERAL, code_bin)
    child_record_address = (
        age_table_address + CHILD_AGE_RECORD_INDEX * AGE_PROPERTIES_RECORD_STRIDE
    )
    _code_offset(
        child_record_address,
        AGE_PROPERTIES_RECORD_STRIDE,
        code,
        code_bin,
    )
    for semantic_clip, (field_offset, expected_stem) in CHILD_AGE_CLIMB_CLIPS.items():
        csab_index = _u32(code, child_record_address + field_offset, code_bin)
        match = csab_by_type_index.get(csab_index)
        if match is None or _member_stem(match.name) != expected_stem:
            actual = "missing" if match is None else match.name
            raise ParseError(
                f"{code_bin}: child age record +0x{field_offset:X} expected native "
                f"{semantic_clip} stem {expected_stem}, found {actual} at CSAB {csab_index}"
            )
        direct_semantic_clips.append(
            {
                "semantic_clip": semantic_clip,
                "csab_type_local_index": csab_index,
                "csab_member": match.name,
                "resolution": "native_child_age_record_index",
                "native_index_source": {
                    "record_runtime_address": child_record_address,
                    "field_offset": field_offset,
                    "field_runtime_address": child_record_address + field_offset,
                },
            }
        )

    for semantic_clip, expected_stem, selector in (
        ("free_climb_start_back", "nml_fclimb_startb", FREE_CLIMB_START_BACK_INSTRUCTION),
        ("free_climb_start_front", "nml_fclimb_starta", FREE_CLIMB_START_FRONT_INSTRUCTION),
    ):
        start_instruction, expected_word, start_index = selector
        actual_word = _u32(code, start_instruction, code_bin)
        if actual_word != expected_word:
            raise ParseError(
                f"{code_bin}: {semantic_clip} selector expected instruction "
                f"0x{expected_word:08X}, found 0x{actual_word:08X}"
            )
        start_match = csab_by_type_index.get(start_index)
        if start_match is None or _member_stem(start_match.name) != expected_stem:
            actual = "missing" if start_match is None else start_match.name
            raise ParseError(
                f"{code_bin}: {semantic_clip} selector index {start_index} resolves to {actual}"
            )
        direct_semantic_clips.append(
            {
                "semantic_clip": semantic_clip,
                "csab_type_local_index": start_index,
                "csab_member": start_match.name,
                "resolution": "native_action_immediate_index",
                "native_index_source": {
                    "instruction_address": start_instruction,
                    "instruction_word": actual_word,
                },
            }
        )

    return {
        "format": FORMAT,
        "status": "ready",
        "source": {
            "code_bin": str(code_bin),
            "code_sha256": hashlib.sha256(code).hexdigest(),
            "actor_zar": str(actor_zar),
            "actor_zar_sha256": hashlib.sha256(actor_zar.read_bytes()).hexdigest(),
        },
        "table": {
            "pointer_literal_address": ANIMATION_GROUP_POINTER_LITERAL,
            "runtime_address": table_address,
            "group_count": ANIMATION_GROUP_COUNT,
            "animation_type_count": ANIMATION_TYPE_COUNT,
        },
        "semantic_policy": {
            "gameplay_scaffold": "N64 Player animation-group identities",
            "asset_and_timing_authority": "OOT3D code.bin group table and native ZAR CSAB members",
            "runtime_rule": "resolve exact semantic bindings offline; do not select CSABs by runtime name heuristics",
        },
        "semantic_groups": semantic_groups,
        "semantic_bindings": sorted(semantic_bindings, key=lambda binding: binding["n64_name"]),
        "direct_semantic_clips": sorted(
            direct_semantic_clips, key=lambda clip: clip["semantic_clip"]
        ),
        "animation_groups": rows,
    }
