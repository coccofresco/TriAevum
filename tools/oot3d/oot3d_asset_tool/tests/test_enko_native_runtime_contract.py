from __future__ import annotations

import hashlib
import struct
from pathlib import Path

from oot3d_asset_tool.enko_native_runtime_contract import (
    ANIMATION_SOURCE_SLOT_BY_SEMANTIC,
    ANIMATION_SOURCE_TABLE,
    ANIMATION_TABLE,
    ACTOR_OVERLAY_ENTRY_STRIDE,
    ACTOR_OVERLAY_PROFILE_POINTER_OFFSET,
    ACTOR_OVERLAY_TABLE_POINTER_LITERAL,
    CODE_IMAGE_BASE,
    ENKO_ACTOR_ID,
    ENKO_HEAD_LIMB_COMPARE,
    ENKO_HEAD_STATE_WORD0_LOAD,
    ENKO_HEAD_STATE_WORD1_LOAD,
    ENKO_BLINK_SEQUENCE_POINTER_LITERAL,
    ENKO_BLINK_INDEX_OFFSET_LITERAL,
    ENKO_BLINK_SEQUENCE_LENGTH_COMPARE,
    ENKO_BLINK_TIMER_BASE_MOVE,
    ENKO_BLINK_TIMER_RANGE_COPY,
    ENKO_TORSO_LIMB_COMPARE,
    ENKO_TORSO_STATE_BASE_ADD,
    ENKO_TORSO_STATE_OFFSET_ADD,
    ENKO_TRACKING_ROUTE_WORDS,
    FORMAT,
    HEAD_TABLE,
    MODEL_CLASS_TABLE,
    MODEL_INFO_TABLE,
    MODEL_SCALE_LITERAL,
    OBJECT_TABLE,
    NATIVE_RANDOM_STATE_POINTER_LITERAL,
    NATIVE_RANDOM_MULTIPLIER_ADDRESS,
    NATIVE_RANDOM_INCREMENT_ADDRESS,
    NPC_TRACKING_FACING_THRESHOLD_LITERAL,
    NPC_TRACKING_LAYOUT_WORDS,
    NPC_TRACKING_MODE_WORDS,
    NPC_TRACKING_PRESET_POINTER_LITERAL,
    NPC_TRACKING_PRESET_TABLE_END,
    NPC_TRACKING_PRESET_STRIDE,
    NPC_TRACKING_SIGNED_GUARD_LITERAL,
    NPC_TRACKING_SELECTOR_FORCED_MODE_WORDS,
    NPC_TRACKING_SMOOTH_CALLS,
    NPC_TRACKING_SMOOTH_LANES,
    NPC_TRACKING_TARGET_HEIGHT_COPY_SIZE_INSTRUCTION,
    NPC_TRACKING_TARGET_HEIGHT_FALLBACK_LITERAL,
    NPC_TRACKING_TARGET_HEIGHT_POINTER_LITERAL,
    QUEST_ANIMATION_LOOKUP,
    build_enko_native_runtime_contract,
)
from oot3d_asset_tool.ensa_native_runtime_contract import (
    ANIMATION_PLAYBACK_SPEED_LITERAL as ENSA_ANIMATION_PLAYBACK_SPEED_LITERAL,
    ANIMATION_SELECTOR_TABLE_POINTER_LITERAL as ENSA_ANIMATION_TABLE_POINTER_LITERAL,
    ANIMATION_SELECTOR_TABLE_STRIDE_INSTRUCTION as ENSA_ANIMATION_STRIDE_INSTRUCTION,
    ANIMATION_START_FRAME_LITERAL as ENSA_ANIMATION_START_FRAME_LITERAL,
    BLINK_SEQUENCE_LENGTH_COMPARE as ENSA_BLINK_SEQUENCE_LENGTH_COMPARE,
    BLINK_TIMER_BASE_MOVE as ENSA_BLINK_TIMER_BASE_MOVE,
    BLINK_TIMER_RANGE_COPY as ENSA_BLINK_TIMER_RANGE_COPY,
    ENSA_ACTOR_ID,
    FACE_MOUTH_TABLE_POINTER_LITERAL,
    FORMAT as ENSA_FORMAT,
    INITIAL_KOKIRI_SCENE_COMPARE,
    INITIAL_KOKIRI_SELECTOR_MOVE,
    MODEL_SCALE_LITERAL as ENSA_MODEL_SCALE_LITERAL,
    build_ensa_native_runtime_contract,
)
from oot3d_asset_tool.enmd_native_runtime_contract import (
    ANIMATION_TABLE_POINTER_LITERAL as ENMD_ANIMATION_TABLE_POINTER_LITERAL,
    ANIMATION_TABLE_STRIDE_FIRST as ENMD_ANIMATION_STRIDE_FIRST,
    ANIMATION_TABLE_STRIDE_SECOND as ENMD_ANIMATION_STRIDE_SECOND,
    BLINK_SEQUENCE_LENGTH_COMPARE as ENMD_BLINK_SEQUENCE_LENGTH_COMPARE,
    BLINK_TIMER_BASE_MOVE as ENMD_BLINK_TIMER_BASE_MOVE,
    BLINK_TIMER_RANGE_COPY as ENMD_BLINK_TIMER_RANGE_COPY,
    ENMD_ACTOR_ID,
    FORMAT as ENMD_FORMAT,
    INITIAL_ANIMATION_SELECTOR_MOVE as ENMD_INITIAL_SELECTOR_MOVE,
    INITIAL_KOKIRI_SCENE_COMPARE as ENMD_INITIAL_SCENE_COMPARE,
    MODEL_SCALE_LITERAL as ENMD_MODEL_SCALE_LITERAL,
    build_enmd_native_runtime_contract,
)
from oot3d_asset_tool.native_actor_contract_common import (
    NATIVE_RANDOM_ZERO_ONE_ADDRESS,
    NATIVE_RANDOM_ZERO_ONE_SIZE,
)
from oot3d_asset_tool.player_animation_group_native_contract import (
    ANIMATION_GROUP_POINTER_LITERAL,
    ANIMATION_GROUP_COUNT,
    ANIMATION_TYPE_COUNT,
    build_player_animation_group_native_contract,
)
from oot3d_asset_tool.zar import ZarArchive


def _at(address: int) -> int:
    return address - CODE_IMAGE_BASE


def _assert_native_random_contract(contract: dict, code: bytes) -> None:
    random = contract["face_runtime"]["random"]
    function_offset = _at(NATIVE_RANDOM_ZERO_ONE_ADDRESS)
    assert random["algorithm"] == "oot3d_code_bin_rand_zero_one"
    assert random["function"] == "Rand_ZeroOne"
    assert random["function_address"] == NATIVE_RANDOM_ZERO_ONE_ADDRESS
    assert random["function_size"] == NATIVE_RANDOM_ZERO_ONE_SIZE
    assert random["function_sha256"] == hashlib.sha256(
        code[function_offset : function_offset + NATIVE_RANDOM_ZERO_ONE_SIZE]
    ).hexdigest()
    assert random["return_abi"] == "aapcs_vfp_s0_f32"
    assert random["state_address"] == 0x0050C0C4


def _write_zar(
    path: Path,
    cmb_name: str,
    csab_count: int,
    csab_names: list[str] | None = None,
    cmab_names: list[str] | None = None,
) -> None:
    files = [(cmb_name, "cmb", b"cmb")]
    names = csab_names or [f"Anim/a{index:02}.csab" for index in range(csab_count)]
    if len(names) != csab_count:
        raise ValueError("CSAB fixture name count does not match csab_count")
    files.extend((name, "csab", b"csab") for name in names)
    files.extend((name, "cmab", b"cmab") for name in (cmab_names or []))
    type_names = ["cmb", "csab"] + (["cmab"] if cmab_names else [])
    type_section = 0x20
    type_size = len(type_names) * 0x10
    meta_section = type_section + type_size
    meta_size = len(files) * 8
    cursor = meta_section + meta_size
    type_lists: dict[str, int] = {}
    for type_name in type_names:
        type_lists[type_name] = cursor
        cursor += sum(1 for _, candidate, _ in files if candidate == type_name) * 4
    type_name_offsets: dict[str, int] = {}
    for type_name in type_names:
        type_name_offsets[type_name] = cursor
        cursor += len(type_name) + 1
    file_name_offsets = []
    for name, _, _ in files:
        file_name_offsets.append(cursor)
        cursor += len(name) + 1
    data_section = (cursor + 3) & ~3
    data_offsets_table_size = len(files) * 4
    cursor = data_section + data_offsets_table_size
    data_offsets = []
    for _, _, payload in files:
        data_offsets.append(cursor)
        cursor += len(payload)
    data = bytearray(cursor)
    data[0:4] = b"ZAR\x01"
    struct.pack_into("<IHHIII", data, 4, len(data), len(type_names), len(files),
                     type_section, meta_section, data_section)
    for type_index, type_name in enumerate(type_names):
        indices = [index for index, (_, candidate, _) in enumerate(files) if candidate == type_name]
        struct.pack_into("<III", data, type_section + type_index * 0x10,
                         len(indices), type_lists[type_name], type_name_offsets[type_name])
        for index, file_index in enumerate(indices):
            struct.pack_into("<I", data, type_lists[type_name] + index * 4, file_index)
        data[type_name_offsets[type_name]:type_name_offsets[type_name] + len(type_name) + 1] = \
            type_name.encode() + b"\0"
    for index, (name, _, payload) in enumerate(files):
        struct.pack_into("<II", data, meta_section + index * 8, len(payload), file_name_offsets[index])
        data[file_name_offsets[index]:file_name_offsets[index] + len(name) + 1] = name.encode() + b"\0"
        struct.pack_into("<I", data, data_section + index * 4, data_offsets[index])
        data[data_offsets[index]:data_offsets[index] + len(payload)] = payload
    path.write_bytes(data)


def test_ensa_contract_resolves_initial_kokiri_state_and_native_face_set(
    tmp_path: Path,
) -> None:
    actor_root = tmp_path / "actor"
    actor_root.mkdir()
    saria = actor_root / "zelda_sa.zar"
    csab_names = [f"Anim/saria_{index:02}.csab" for index in range(32)]
    csab_names[15] = "Anim/sa_matsu.csab"
    _write_zar(
        saria,
        "Model/saria.cmb",
        len(csab_names),
        csab_names,
        ["misc/saria_eye.cmab", "misc/saria_mouth.cmab"],
    )

    code = bytearray(0x450000)
    actor_overlay_table = 0x0050CD84
    actor_profile = 0x0052E720
    struct.pack_into(
        "<I", code, _at(ACTOR_OVERLAY_TABLE_POINTER_LITERAL), actor_overlay_table
    )
    overlay_entry = actor_overlay_table + ENSA_ACTOR_ID * ACTOR_OVERLAY_ENTRY_STRIDE
    struct.pack_into(
        "<I",
        code,
        _at(overlay_entry) + ACTOR_OVERLAY_PROFILE_POINTER_OFFSET,
        actor_profile,
    )
    struct.pack_into(
        "<HBBIHHIIIII",
        code,
        _at(actor_profile),
        ENSA_ACTOR_ID,
        4,
        0,
        0x02000019,
        188,
        0,
        0x0C4C,
        0x00168504,
        0x001688C4,
        0x001B9450,
        0x001B9358,
    )
    object_path = "rom:/actor/zelda_sa.zar"
    object_offset = _at(OBJECT_TABLE + 188 * 0x44)
    code[object_offset : object_offset + len(object_path) + 1] = (
        object_path.encode() + b"\0"
    )

    animation_table = 0x0052E784
    struct.pack_into(
        "<I", code, _at(ENSA_ANIMATION_TABLE_POINTER_LITERAL), animation_table
    )
    struct.pack_into(
        "<I", code, _at(ENSA_ANIMATION_STRIDE_INSTRUCTION), 0xE0804201
    )
    struct.pack_into("<f", code, _at(ENSA_ANIMATION_START_FRAME_LITERAL), 0.0)
    struct.pack_into("<f", code, _at(ENSA_ANIMATION_PLAYBACK_SPEED_LITERAL), 1.0)
    selector_csabs = [15, 2, 4, 3, 15, 5, 16, 17, 14, 18, 19, 16, 29, 30, 10]
    selector_modes = [0, 2, 0, 0, 0, 0, 0, 2, 2, 2, 2, 0, 0, 0, 0]
    for selector, (csab_index, mode) in enumerate(
        zip(selector_csabs, selector_modes, strict=True)
    ):
        struct.pack_into(
            "<IfB3xf",
            code,
            _at(animation_table + selector * 0x10),
            csab_index,
            1.0,
            mode,
            0.0 if selector in (0, 11, 12, 13) else -10.0,
        )

    struct.pack_into("<I", code, _at(INITIAL_KOKIRI_SCENE_COMPARE), 0xE3500055)
    struct.pack_into("<I", code, _at(INITIAL_KOKIRI_SELECTOR_MOVE), 0xE3A01004)
    struct.pack_into("<f", code, _at(ENSA_MODEL_SCALE_LITERAL), 0.01)
    struct.pack_into("<I", code, _at(ENSA_BLINK_SEQUENCE_LENGTH_COMPARE), 0xE3500003)
    struct.pack_into("<I", code, _at(ENSA_BLINK_TIMER_BASE_MOVE), 0xE3A0101E)
    struct.pack_into("<I", code, _at(ENSA_BLINK_TIMER_RANGE_COPY), 0xE1A00001)
    mouth_table = 0x004D9C20
    struct.pack_into("<I", code, _at(FACE_MOUTH_TABLE_POINTER_LITERAL), mouth_table)
    code[_at(mouth_table) : _at(mouth_table) + 5] = bytes([0, 3, 4, 1, 2])
    random_state = 0x0050C0C4
    struct.pack_into("<I", code, _at(NATIVE_RANDOM_STATE_POINTER_LITERAL), random_state)
    struct.pack_into("<I", code, _at(NATIVE_RANDOM_MULTIPLIER_ADDRESS), 0x0019660D)
    struct.pack_into("<I", code, _at(NATIVE_RANDOM_INCREMENT_ADDRESS), 0x3C6EF35F)
    struct.pack_into("<I", code, _at(random_state), 1)
    code_bin = tmp_path / "code.bin"
    code_bin.write_bytes(code)

    contract = build_ensa_native_runtime_contract(code_bin, [saria])

    assert contract["format"] == ENSA_FORMAT
    assert contract["status"] == "initial_route_complete"
    assert contract["actor_profile"]["actor_id"] == ENSA_ACTOR_ID
    assert contract["spawn_state_semantics"][0]["index"] == 4
    assert contract["spawn_state_semantics"][0]["conditions"]["scene.native_id"] == "85"
    assert contract["animations"][4]["csab_member"] == "Anim/sa_matsu.csab"
    assert contract["model"]["model_asset_id"] == \
        "cmb:actor/zelda_sa.zar!Model/saria.cmb"
    assert contract["face_runtime"]["blink_sequence"] == [0, 1, 2]
    assert contract["face_runtime"]["mouth_frame_lookup"] == [0, 3, 4, 1, 2]
    _assert_native_random_contract(contract, code)


def test_enmd_contract_resolves_initial_kokiri_state_and_native_eye_set(
    tmp_path: Path,
) -> None:
    actor_root = tmp_path / "actor"
    actor_root.mkdir()
    mido = actor_root / "zelda_md.zar"
    csab_names = [f"Anim/mido_{index:02}.csab" for index in range(12)]
    csab_names[0] = "Anim/md_matsu_kani.csab"
    _write_zar(
        mido,
        "Model/mido.cmb",
        len(csab_names),
        csab_names,
        ["Misc/mido_eye.cmab"],
    )

    code = bytearray(0x450000)
    actor_overlay_table = 0x0050CD84
    actor_profile = 0x0052C524
    struct.pack_into(
        "<I", code, _at(ACTOR_OVERLAY_TABLE_POINTER_LITERAL), actor_overlay_table
    )
    overlay_entry = actor_overlay_table + ENMD_ACTOR_ID * ACTOR_OVERLAY_ENTRY_STRIDE
    struct.pack_into(
        "<I",
        code,
        _at(overlay_entry) + ACTOR_OVERLAY_PROFILE_POINTER_OFFSET,
        actor_profile,
    )
    struct.pack_into(
        "<HBBIHHIIIII",
        code,
        _at(actor_profile),
        ENMD_ACTOR_ID,
        4,
        0,
        0x02000019,
        251,
        0,
        0x0C24,
        0x0016604C,
        0x00166348,
        0x001B736C,
        0x001B72B4,
    )
    object_path = "rom:/actor/zelda_md.zar"
    object_offset = _at(OBJECT_TABLE + 251 * 0x44)
    code[object_offset : object_offset + len(object_path) + 1] = (
        object_path.encode() + b"\0"
    )

    animation_table = 0x0052C588
    struct.pack_into(
        "<I", code, _at(ENMD_ANIMATION_TABLE_POINTER_LITERAL), animation_table
    )
    struct.pack_into("<I", code, _at(ENMD_ANIMATION_STRIDE_FIRST), 0xE0820082)
    struct.pack_into("<I", code, _at(ENMD_ANIMATION_STRIDE_SECOND), 0xE0814180)
    selector_csabs = [0, 0, 5, 10, 9, 3, 8, 6, 11, 4, 0, 1]
    for selector, csab_index in enumerate(selector_csabs):
        struct.pack_into(
            "<IfffB3xf",
            code,
            _at(animation_table + selector * 0x18),
            csab_index,
            0.0 if selector in (0, 1, 10) else 1.0,
            0.0,
            -1.0,
            2 if selector in (2, 4, 7, 9) else 0,
            0.0 if selector == 0 else -1.0,
        )
    struct.pack_into("<I", code, _at(ENMD_INITIAL_SCENE_COMPARE), 0xE3500055)
    struct.pack_into("<I", code, _at(ENMD_INITIAL_SELECTOR_MOVE), 0xE3A02000)
    struct.pack_into("<f", code, _at(ENMD_MODEL_SCALE_LITERAL), 0.01)
    struct.pack_into("<I", code, _at(ENMD_BLINK_SEQUENCE_LENGTH_COMPARE), 0xE3500003)
    struct.pack_into("<I", code, _at(ENMD_BLINK_TIMER_BASE_MOVE), 0xE3A0101E)
    struct.pack_into("<I", code, _at(ENMD_BLINK_TIMER_RANGE_COPY), 0xE1A00001)
    random_state = 0x0050C0C4
    struct.pack_into("<I", code, _at(NATIVE_RANDOM_STATE_POINTER_LITERAL), random_state)
    struct.pack_into("<I", code, _at(NATIVE_RANDOM_MULTIPLIER_ADDRESS), 0x0019660D)
    struct.pack_into("<I", code, _at(NATIVE_RANDOM_INCREMENT_ADDRESS), 0x3C6EF35F)
    struct.pack_into("<I", code, _at(random_state), 1)
    code_bin = tmp_path / "code.bin"
    code_bin.write_bytes(code)

    contract = build_enmd_native_runtime_contract(code_bin, [mido])

    assert contract["format"] == ENMD_FORMAT
    assert contract["status"] == "initial_route_complete"
    assert contract["actor_profile"]["actor_id"] == ENMD_ACTOR_ID
    assert contract["spawn_state_semantics"][0]["index"] == 0
    assert contract["animations"][0]["csab_member"] == "Anim/md_matsu_kani.csab"
    assert contract["animations"][0]["applied_playback_speed"] == 0.0
    assert contract["face_runtime"]["eye_cmab_member"] == "Misc/mido_eye.cmab"
    _assert_native_random_contract(contract, code)


def test_enko_contract_resolves_code_tables_to_native_zar_members(tmp_path: Path) -> None:
    actor_root = tmp_path / "actor"
    actor_root.mkdir()
    km1 = actor_root / "zelda_km1.zar"
    kw1 = actor_root / "zelda_kw1.zar"
    fa = actor_root / "zelda_fa.zar"
    _write_zar(km1, "Model/kokirimaster.cmb", 34)
    _write_zar(kw1, "Model/kokiripeople.cmb", 34)
    _write_zar(fa, "Model/kokiripeople.cmb", 3)

    km1_archive = ZarArchive.from_path(km1)
    km1_csabs = [file for file in km1_archive.files if file.type_name == "csab"]
    assert [file.type_local_index for file in km1_csabs] == list(range(34))

    code = bytearray(0x450000)
    actor_overlay_table = 0x0050CD84
    actor_profile = 0x0052BD90
    struct.pack_into("<I", code, _at(ACTOR_OVERLAY_TABLE_POINTER_LITERAL), actor_overlay_table)
    overlay_entry = actor_overlay_table + ENKO_ACTOR_ID * ACTOR_OVERLAY_ENTRY_STRIDE
    struct.pack_into("<I", code,
                     _at(overlay_entry) + ACTOR_OVERLAY_PROFILE_POINTER_OFFSET,
                     actor_profile)
    struct.pack_into("<HBBIHHIIIII", code, _at(actor_profile),
                     ENKO_ACTOR_ID, 4, 0, 0x19, 1, 0, 0xCA8,
                     0x00165684, 0x00165948, 0x001B5F14, 0x001B5E30)
    source_indices = list(range(29))
    for index, value in enumerate(source_indices):
        struct.pack_into("<I", code, _at(ANIMATION_SOURCE_TABLE) + index * 4, value)
    object_paths = {252: "rom:/actor/zelda_km1.zar", 253: "rom:/actor/zelda_kw1.zar",
                    317: "rom:/actor/zelda_fa.zar"}
    for object_id, value in object_paths.items():
        offset = _at(OBJECT_TABLE + object_id * 0x44)
        code[offset:offset + len(value) + 1] = value.encode() + b"\0"
    struct.pack_into("<IIII", code, _at(MODEL_CLASS_TABLE), 252, 0, 253, 0)
    struct.pack_into("<IIII", code, _at(HEAD_TABLE), 252, 2, 0xFFFFFFFF, 0)
    struct.pack_into("<IIII", code, _at(HEAD_TABLE) + 0x10, 253, 2, 3, 1)
    struct.pack_into("<IIII", code, _at(HEAD_TABLE) + 0x20, 317, 4, 0xFFFFFFFF, 0)
    for semantic_index in range(35):
        csab_index = 1 if semantic_index == 34 else 0
        struct.pack_into("<IfffB3xf", code, _at(ANIMATION_TABLE) + semantic_index * 0x18,
                         csab_index, 1.5 if semantic_index == 20 else 1.0,
                         0.0, -1.0, 0, -8.0 if semantic_index >= 29 else 0.0)
    for subtype in range(13):
        model_class = subtype & 1
        head_class = 2 if subtype == 12 else model_class
        offset = _at(MODEL_INFO_TABLE) + subtype * 11
        code[offset:offset + 11] = bytes(
            [head_class, model_class, 1, 2, 3, 255, model_class, 4, 5, 6, 255]
        )
    code[_at(QUEST_ANIMATION_LOOKUP):_at(QUEST_ANIMATION_LOOKUP) + 65] = bytes(range(5)) * 13
    struct.pack_into("<f", code, _at(MODEL_SCALE_LITERAL), 0.01)
    struct.pack_into("<I", code, _at(ENKO_TORSO_LIMB_COMPARE), 0xE3510009)
    struct.pack_into("<I", code, _at(ENKO_HEAD_LIMB_COMPARE), 0xE351000A)
    struct.pack_into("<I", code, _at(ENKO_TORSO_STATE_BASE_ADD), 0xE2831C02)
    struct.pack_into("<I", code, _at(ENKO_TORSO_STATE_OFFSET_ADD), 0xE281109A)
    struct.pack_into("<I", code, _at(ENKO_HEAD_STATE_WORD0_LOAD), 0xE5930294)
    struct.pack_into("<I", code, _at(ENKO_HEAD_STATE_WORD1_LOAD), 0xE5931298)
    blink_sequence = 0x004D98F4
    struct.pack_into("<I", code, _at(ENKO_BLINK_SEQUENCE_POINTER_LITERAL), blink_sequence)
    struct.pack_into("<I", code, _at(ENKO_BLINK_INDEX_OFFSET_LITERAL), 0x2BA)
    struct.pack_into("<I", code, _at(ENKO_BLINK_SEQUENCE_LENGTH_COMPARE), 0xE3500004)
    struct.pack_into("<I", code, _at(ENKO_BLINK_TIMER_BASE_MOVE), 0xE3A0101E)
    struct.pack_into("<I", code, _at(ENKO_BLINK_TIMER_RANGE_COPY), 0xE1A00001)
    struct.pack_into("<IIII", code, _at(blink_sequence), 0, 1, 2, 1)
    tracking_presets = 0x0050CBA4
    struct.pack_into(
        "<I", code, _at(NPC_TRACKING_PRESET_POINTER_LITERAL), tracking_presets
    )
    struct.pack_into(
        "<I", code, _at(NPC_TRACKING_SIGNED_GUARD_LITERAL), 0xFFFE
    )
    tracking_preset_count = (
        NPC_TRACKING_PRESET_TABLE_END - tracking_presets
    ) // NPC_TRACKING_PRESET_STRIDE
    for index in range(tracking_preset_count):
        struct.pack_into(
            "<6hB3xfh2x",
            code,
            _at(tracking_presets + index * NPC_TRACKING_PRESET_STRIDE),
            1000 + index,
            -200 - index,
            300 + index,
            400 + index,
            -500 - index,
            600 + index,
            1,
            170.0 if index < 6 else 0.0,
            16000 if index < 6 else 0,
        )
    tracking_heights = 0x004D97AC
    struct.pack_into(
        "<I",
        code,
        _at(NPC_TRACKING_TARGET_HEIGHT_POINTER_LITERAL),
        tracking_heights,
    )
    for subtype in range(13):
        for state in range(5):
            struct.pack_into(
                "<f",
                code,
                _at(tracking_heights) + (subtype * 5 + state) * 4,
                float(subtype * 10 + state),
            )
    struct.pack_into(
        "<f", code, _at(NPC_TRACKING_TARGET_HEIGHT_FALLBACK_LITERAL), -20.0
    )
    struct.pack_into(
        "<I", code, _at(NPC_TRACKING_TARGET_HEIGHT_COPY_SIZE_INSTRUCTION), 0xE3A02F41
    )
    struct.pack_into(
        "<I", code, _at(NPC_TRACKING_FACING_THRESHOLD_LITERAL), 0x3FFC
    )
    for _, min_address, max_address, scale_address in NPC_TRACKING_SMOOTH_LANES:
        struct.pack_into("<I", code, _at(min_address), 0xE3A03001)
        struct.pack_into("<I", code, _at(max_address), 0xE3A03E7D)
        struct.pack_into("<I", code, _at(scale_address), 0xE3A02006)
    for address, instruction in ENKO_TRACKING_ROUTE_WORDS.items():
        struct.pack_into("<I", code, _at(address), instruction)
    for address, instruction in {
        **NPC_TRACKING_MODE_WORDS,
        **NPC_TRACKING_SELECTOR_FORCED_MODE_WORDS,
        **NPC_TRACKING_LAYOUT_WORDS,
    }.items():
        struct.pack_into("<I", code, _at(address), instruction)
    random_state = 0x0050C0C4
    struct.pack_into("<I", code, _at(NATIVE_RANDOM_STATE_POINTER_LITERAL), random_state)
    struct.pack_into("<I", code, _at(NATIVE_RANDOM_MULTIPLIER_ADDRESS), 0x0019660D)
    struct.pack_into("<I", code, _at(NATIVE_RANDOM_INCREMENT_ADDRESS), 0x3C6EF35F)
    struct.pack_into("<I", code, _at(random_state), 1)
    code_bin = tmp_path / "code.bin"
    code_bin.write_bytes(code)

    native_abi_catalog = {
        "code_bin_sha256": hashlib.sha256(code).hexdigest(),
        "payload_sha256": "a" * 64,
        "source_snapshot_id": "fixture",
        "functions": [
            {
                "address": "0x002335B4",
                "name": "EnKo_OverrideLimbDraw",
                "closure_kind": "maintained_abi",
                "return_type": "s32",
                "parameter_types": [
                    "Oot3dPlayState*",
                    "s32",
                    "Oot3dMtx3x4*",
                    "void*",
                ],
                "parameter_names": ["play", "limb", "mtx", "state"],
                "source_tranche": 156,
                "evidence_file": "fixture.csv",
                "signature_evidence_file": "fixture_signatures.csv",
            }
        ],
    }
    contract = build_enko_native_runtime_contract(
        code_bin, [km1, kw1, fa], native_abi_catalog
    )

    assert contract["format"] == FORMAT
    assert contract["draw_callback"]["torso_rotation_u16x3_offset"] == 0x29A
    assert contract["draw_callback"]["head_rotation_u16x3_offset"] == 0x294
    assert contract["draw_callback"]["native_abi"]["source_tranche"] == 156
    assert contract["actor_profile"]["actor_id"] == ENKO_ACTOR_ID
    assert contract["actor_profile"]["init_address"] == 0x00165684
    assert contract["quest_state_semantics"][0]["semantic"] == "child_start"
    assert contract["subtypes"][0]["model_asset_id"] == \
        "cmb:actor/zelda_km1.zar!Model/kokirimaster.cmb"
    assert contract["subtypes"][1]["resource_visibility_clear_ids"] == [2, 3]
    assert contract["subtypes"][1]["face_model_asset_id"] == \
        "cmb:actor/zelda_kw1.zar!Model/kokiripeople.cmb"
    assert contract["subtypes"][12]["face_model_asset_id"] == \
        "cmb:actor/zelda_fa.zar!Model/kokiripeople.cmb"
    assert contract["draw_callback"]["torso_limb_index"] == 9
    assert contract["draw_callback"]["head_limb_index"] == 10
    assert contract["draw_callback"]["address"] == 0x002335B4
    assert contract["draw_callback"]["size"] == 0x60
    assert len(contract["draw_callback"]["sha256"]) == 64
    assert contract["face_runtime"]["blink_sequence"] == [0, 1, 2, 1]
    assert contract["face_runtime"]["blink_timer_offset"] == 0x2B8
    assert contract["face_runtime"]["blink_index_offset"] == 0x2BA
    assert contract["face_runtime"]["blink_timer_base"] == 30
    assert contract["face_runtime"]["blink_timer_range"] == 30
    assert contract["face_runtime"]["random"]["initial_state"] == 1
    _assert_native_random_contract(contract, code)
    tracking = contract["tracking_runtime"]
    assert tracking["status"] == "initial_child_start_routes_recovered"
    assert tracking["service"]["parameter_names"][3] == "forced_tracking_mode"
    assert len(tracking["presets"]) == 14
    assert tracking["presets"][2]["head_yaw_limit"] == 1002
    assert tracking["presets"][5]["torso_pitch_max"] == 605
    assert tracking["smoothing"] == {
        "scale": 6,
        "max_step": 2000,
        "min_step": 1,
        "call_sites": list(NPC_TRACKING_SMOOTH_CALLS),
    }
    assert tracking["actor_layout"] == {
        "minimum_size": 0xC0,
        "world_position_f32x3_offset": 0x28,
        "shape_yaw_s16_offset": 0xBE,
    }
    assert tracking["global_context"]["pointer_address"] == 0x0051B2F4
    assert tracking["global_context"]["update_rate_s16_offset"] == 0x110
    assert tracking["global_context"]["update_rate"] == 2
    assert tracking["selector"]["auto_turn_timer_function_address"] == 0x003702C8
    assert tracking["selector"]["auto_turn_timer_function_size"] == 0x80
    assert tracking["selector"]["random_state_address"] == 0x0050C0C4
    assert tracking["interact_info"]["talk_state_s16_offset"] == 0
    assert tracking["interact_info"]["auto_turn_timer_s16_offset"] == 4
    assert tracking["interact_info"]["auto_turn_state_s16_offset"] == 6
    assert tracking["target_heights_by_subtype_and_quest_state"][6][3] == 63.0
    assert tracking["selector"]["facing_threshold"] == 0x3FFC
    assert tracking["selector"]["forced_mode_path"] == \
        "nonzero_argument_returned_unchanged"
    assert tracking["mode_semantics"] == [
        {"mode": 1, "head": False, "torso": False, "rotate_actor": False},
        {"mode": 2, "head": True, "torso": True, "rotate_actor": False},
        {"mode": 3, "head": True, "torso": False, "rotate_actor": False},
        {"mode": 4, "head": True, "torso": True, "rotate_actor": True},
    ]
    routes = {
        route["subtype"]: route for route in tracking["initial_routes"]
    }
    assert routes[0]["preset_index"] == 2
    assert routes[0]["initial_forced_mode"] == 1
    assert routes[1]["initial_forced_mode"] == 4
    assert routes[2]["preset_index"] == 5
    assert routes[2]["engaged_mode_policy"] == "facing_threshold"
    assert routes[2]["facing_mode"] == 2
    assert routes[2]["not_facing_mode"] == 1
    assert routes[6]["mode_policy"] == "facing_threshold"
    assert contract["animations"][0]["csab_type_local_index"] == \
        source_indices[ANIMATION_SOURCE_SLOT_BY_SEMANTIC[0]]
    assert contract["animations"][20]["playback_speed"] == 1.5
    assert contract["animations"][34]["bindings_by_model_class"][1]["csab_member"] == \
        "Anim/a01.csab"


def test_player_animation_group_contract_resolves_native_jump_climb_rows(tmp_path: Path) -> None:
    actor_zar = tmp_path / "zelda_link_child_new.zar"
    csab_names = [f"boy/anim/test_{index:03}.csab" for index in range(180)]
    csab_names[110:126] = [
        "boy/anim/nml_wait_free.csab",
        "boy/anim/nml_wait.csab",
        "child/anim/ft_wait_long.csab",
        "child/anim/nml_walk_free.csab",
        "child/anim/nml_walk.csab",
        "child/anim/ft_walk_long.csab",
        "child/anim/nml_run_free.csab",
        "child/anim/ft_run.csab",
        "child/anim/nml_run.csab",
        "child/anim/ft_run_long.csab",
        "boy/anim/nml_walk_endL_free.csab",
        "boy/anim/nml_walk_endL.csab",
        "child/anim/ft_walk_endL_long.csab",
        "boy/anim/nml_walk_endR_free.csab",
        "boy/anim/nml_walk_endR.csab",
        "child/anim/ft_walk_endR_long.csab",
    ]
    csab_names[143:149] = [
        "boy/anim/nml_hang_wait_free.csab",
        "boy/anim/nml_hang_wait.csab",
        "boy/anim/nml_hang_up_free.csab",
        "boy/anim/nml_hang_up.csab",
        "boy/anim/nml_hang_hold_free.csab",
        "boy/anim/nml_hang_hold.csab",
    ]
    csab_names[126:133] = [
        "boy/anim/nml_landing_free.csab",
        "boy/anim/nml_landing.csab",
        "boy/anim/nml_short_landing_free.csab",
        "boy/anim/nml_short_landing.csab",
        "boy/anim/nml_fall.csab",
        "boy/anim/nml_fall_wait.csab",
        "boy/anim/nml_landing_wait.csab",
    ]
    for index, name in {
        133: "boy/anim/nml_landing_roll_free.csab",
        134: "boy/anim/nml_landing_roll.csab",
        135: "boy/anim/nml_defense_free.csab",
        136: "boy/anim/nml_defense.csab",
        137: "boy/anim/bow_defense.csab",
        138: "boy/anim/nml_defense_wait_free.csab",
        139: "boy/anim/nml_defense_wait.csab",
        140: "boy/anim/bow_defense_wait.csab",
        141: "boy/anim/nml_defense_end_free.csab",
        142: "boy/anim/nml_defense_end.csab",
        150: "child/anim/cl_nml_climb_startA.csab",
        151: "child/anim/cl_nml_climb_startB.csab",
        152: "child/anim/cl_nml_climb_upL.csab",
        153: "child/anim/cl_nml_climb_upR.csab",
        154: "child/anim/cl_nml_climb_endAL.csab",
        155: "child/anim/cl_nml_climb_endAR.csab",
        156: "child/anim/cl_nml_climb_endBR.csab",
        157: "child/anim/cl_nml_climb_endBL.csab",
        158: "boy/anim/nml_jump.csab",
        159: "boy/anim/nml_jump_up.csab",
        160: "boy/anim/nml_run_jump.csab",
        161: "boy/anim/nml_run_jump_end.csab",
        171: "boy/anim/nml_Fclimb_upR.csab",
        172: "boy/anim/nml_Fclimb_upL.csab",
        173: "boy/anim/nml_Fclimb_startB.csab",
        174: "boy/anim/nml_Fclimb_startA.csab",
        175: "boy/anim/nml_Fclimb_sideR.csab",
        176: "boy/anim/nml_Fclimb_sideL.csab",
    }.items():
        csab_names[index] = name
    _write_zar(actor_zar, "child/model/childlink_v2.cmb", len(csab_names), csab_names)

    table_address = 0x0053A5F8
    table_size = ANIMATION_GROUP_COUNT * ANIMATION_TYPE_COUNT * 4
    code = bytearray(table_address - CODE_IMAGE_BASE + table_size)
    struct.pack_into("<I", code, _at(ANIMATION_GROUP_POINTER_LITERAL), table_address)
    age_table_address = 0x0053A2F0
    child_record_address = age_table_address + 0x134
    struct.pack_into("<I", code, _at(0x00250AA8), age_table_address)
    for field_offset, csab_index in {
        0x104: 150,
        0x108: 151,
        0x10C: 152,
        0x110: 153,
        0x114: 172,
        0x118: 171,
        0x11C: 176,
        0x120: 175,
        0x124: 154,
        0x128: 155,
        0x12C: 156,
        0x130: 157,
    }.items():
        struct.pack_into("<I", code, _at(child_record_address + field_offset), csab_index)
    struct.pack_into("<I", code, _at(0x001D0200), 0xE3A030AD)
    struct.pack_into("<I", code, _at(0x00351780), 0x13A060AE)
    table_offset = _at(table_address)
    for group_index in range(ANIMATION_GROUP_COUNT):
        struct.pack_into(
            f"<{ANIMATION_TYPE_COUNT}I",
            code,
            table_offset + group_index * ANIMATION_TYPE_COUNT * 4,
            *([0] * ANIMATION_TYPE_COUNT),
        )
    for group_index, indices in {
        0: [110, 111, 111, 112, 110, 110],
        1: [113, 114, 114, 115, 113, 113],
        2: [116, 117, 118, 119, 116, 116],
        17: [126, 127, 127, 126, 126, 126],
        18: [128, 129, 129, 128, 128, 128],
        19: [133, 134, 134, 133, 133, 133],
        21: [120, 121, 121, 122, 120, 120],
        22: [123, 124, 124, 125, 123, 123],
        25: [135, 136, 136, 135, 137, 135],
        26: [138, 139, 139, 138, 140, 138],
        27: [141, 142, 142, 141, 141, 141],
        47: [147, 148, 148, 147, 147, 147],
        48: [143, 144, 144, 143, 143, 143],
        49: [145, 146, 146, 145, 145, 145],
    }.items():
        struct.pack_into(
            f"<{ANIMATION_TYPE_COUNT}I",
            code,
            table_offset + group_index * ANIMATION_TYPE_COUNT * 4,
            *indices,
        )
    code_bin = tmp_path / "code.bin"
    code_bin.write_bytes(code)

    contract = build_player_animation_group_native_contract(code_bin, actor_zar)

    semantic_groups = {
        group["semantic_group"]: group["oot3d_group_index"]
        for group in contract["semantic_groups"]
    }
    assert semantic_groups == {
        "idle": 0,
        "walk": 1,
        "run": 2,
        "walk_end_left": 21,
        "walk_end_right": 22,
        "landing": 17,
        "short_landing": 18,
        "landing_roll": 19,
        "defense": 25,
        "defense_wait": 26,
        "defense_end": 27,
        "jump_climb_hold": 47,
        "jump_climb_wait": 48,
        "jump_climb_up": 49,
    }
    bindings = {binding["n64_name"]: binding for binding in contract["semantic_bindings"]}
    assert bindings["gPlayerAnim_link_normal_jump_climb_up"]["csab_name"] == \
        "boy/anim/nml_hang_up.csab"
    assert bindings["gPlayerAnim_link_normal_jump_climb_up_free"]["animation_type_indices"] == \
        [0, 3, 4, 5]
    direct_clips = {
        clip["semantic_clip"]: clip for clip in contract["direct_semantic_clips"]
    }
    assert direct_clips["fall"]["csab_member"] == "boy/anim/nml_fall.csab"
    assert direct_clips["fall_wait"]["csab_type_local_index"] == 131
    assert direct_clips["landing_wait"]["resolution"] == "unique_native_zar_member_stem"
    assert direct_clips["auto_jump"]["csab_type_local_index"] == 158
    assert direct_clips["auto_jump_up"]["csab_member"] == "boy/anim/nml_jump_up.csab"
    assert direct_clips["run_auto_jump"]["csab_type_local_index"] == 160
    assert direct_clips["run_auto_jump_end"]["csab_member"] == \
        "boy/anim/nml_run_jump_end.csab"
    assert direct_clips["free_climb_start_back"]["csab_type_local_index"] == 173
    assert direct_clips["free_climb_start_front"]["csab_type_local_index"] == 174
    assert direct_clips["free_climb_up_right"]["csab_member"] == \
        "boy/anim/nml_Fclimb_upR.csab"
    assert direct_clips["regular_climb_start_back"]["resolution"] == \
        "native_child_age_record_index"
