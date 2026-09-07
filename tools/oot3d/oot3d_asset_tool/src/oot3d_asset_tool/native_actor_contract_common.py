from __future__ import annotations

import hashlib
from pathlib import Path

from .binary import BinaryView, ParseError
from .zar import ZarArchive, ZarFile


CODE_IMAGE_BASE = 0x00100000
OBJECT_TABLE = 0x0053CCF4
ACTOR_OVERLAY_TABLE_POINTER_LITERAL = 0x00373B64
ACTOR_OVERLAY_ENTRY_STRIDE = 0x20
ACTOR_OVERLAY_PROFILE_POINTER_OFFSET = 0x14
NATIVE_RANDOM_ZERO_ONE_ADDRESS = 0x003759D0
NATIVE_RANDOM_ZERO_ONE_SIZE = 0x38
NATIVE_RANDOM_STATE_POINTER_LITERAL = 0x00375A08
NATIVE_RANDOM_MULTIPLIER_ADDRESS = 0x00375A0C
NATIVE_RANDOM_INCREMENT_ADDRESS = 0x00375A10


def code_offset(address: int, size: int, view: BinaryView) -> int:
    offset = address - CODE_IMAGE_BASE
    view.require(offset, size)
    return offset


def arm_data_processing_immediate(
    view: BinaryView, address: int, opcode: int, destination_register: int
) -> int:
    word = view.u32(code_offset(address, 4, view))
    if (
        word >> 28 != 0xE
        or (word >> 25) & 1 != 1
        or (word >> 21) & 0xF != opcode
        or (word >> 12) & 0xF != destination_register
    ):
        raise ParseError(
            f"{view.source}: unexpected ARM immediate instruction at 0x{address:08X}"
        )
    immediate = word & 0xFF
    rotation = ((word >> 8) & 0xF) * 2
    if rotation:
        immediate = (
            (immediate >> rotation) | (immediate << (32 - rotation))
        ) & 0xFFFFFFFF
    return immediate


def native_random_zero_one_contract(view: BinaryView) -> dict[str, object]:
    function_offset = code_offset(
        NATIVE_RANDOM_ZERO_ONE_ADDRESS, NATIVE_RANDOM_ZERO_ONE_SIZE, view
    )
    state_address = view.u32(
        code_offset(NATIVE_RANDOM_STATE_POINTER_LITERAL, 4, view)
    )
    return {
        "algorithm": "oot3d_code_bin_rand_zero_one",
        "function": "Rand_ZeroOne",
        "function_address": NATIVE_RANDOM_ZERO_ONE_ADDRESS,
        "function_size": NATIVE_RANDOM_ZERO_ONE_SIZE,
        "function_sha256": hashlib.sha256(
            view.bytes(function_offset, NATIVE_RANDOM_ZERO_ONE_SIZE)
        ).hexdigest(),
        "return_abi": "aapcs_vfp_s0_f32",
        "state_address": state_address,
        "initial_state": view.u32(code_offset(state_address, 4, view)),
        "multiplier": view.u32(
            code_offset(NATIVE_RANDOM_MULTIPLIER_ADDRESS, 4, view)
        ),
        "increment": view.u32(
            code_offset(NATIVE_RANDOM_INCREMENT_ADDRESS, 4, view)
        ),
    }


def actor_profile(view: BinaryView, actor_id: int) -> dict[str, int]:
    table_address = view.u32(
        code_offset(ACTOR_OVERLAY_TABLE_POINTER_LITERAL, 4, view)
    )
    overlay_entry_address = table_address + actor_id * ACTOR_OVERLAY_ENTRY_STRIDE
    profile_address = view.u32(
        code_offset(
            overlay_entry_address + ACTOR_OVERLAY_PROFILE_POINTER_OFFSET,
            4,
            view,
        )
    )
    offset = code_offset(profile_address, 0x20, view)
    profile = {
        "actor_id": view.u16(offset),
        "category": view.u8(offset + 0x02),
        "flags": view.u32(offset + 0x04),
        "object_id": view.u16(offset + 0x08),
        "instance_size": view.u32(offset + 0x0C),
        "init_address": view.u32(offset + 0x10),
        "destroy_address": view.u32(offset + 0x14),
        "update_address": view.u32(offset + 0x18),
        "draw_address": view.u32(offset + 0x1C),
        "overlay_table_address": table_address,
        "overlay_entry_address": overlay_entry_address,
        "profile_address": profile_address,
    }
    if profile["actor_id"] != actor_id:
        raise ParseError(
            f"{view.source}: actor 0x{actor_id:04X} ActorInit profile identity mismatch"
        )
    return profile


def object_path(view: BinaryView, object_id: int) -> str:
    offset = code_offset(OBJECT_TABLE + object_id * 0x44, 0x44, view)
    raw = view.cstr(offset, 0x44).replace("\\", "/")
    if not raw.startswith("rom:/"):
        raise ParseError(
            f"{view.source}: object {object_id} has unsupported path {raw!r}"
        )
    return raw[len("rom:/") :]


def typed_files(archive: ZarArchive, type_name: str) -> list[ZarFile]:
    suffix = f".{type_name}"
    return sorted(
        (
            entry
            for entry in archive.files
            if entry.type_name == type_name or entry.name.lower().endswith(suffix)
        ),
        key=lambda entry: (
            entry.type_local_index is None,
            entry.type_local_index
            if entry.type_local_index is not None
            else entry.index,
        ),
    )


def asset_id(family: str, container: str, member: str) -> str:
    return f"{family}:{container}!{member}"


def resolve_archive(actor_sources: list[Path], path: str) -> ZarArchive:
    expected_name = Path(path).name.lower()
    matches = [source for source in actor_sources if source.name.lower() == expected_name]
    if len(matches) != 1:
        raise ValueError(
            f"native actor object archive selection is not unique: {path}"
        )
    return ZarArchive.from_path(matches[0])
