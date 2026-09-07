import pytest
import struct

from generate_whole_aot_guest_map import (
    attach_native_addresses,
    attach_native_extents,
    build_guest_map,
    native_symbol,
)


def _inputs():
    program = {
        "code_sha256": "code",
        "blocks": [
            {"id": 0, "pc": 0x1000, "end_pc": 0x1008},
            {"id": 1, "pc": 0x1008, "end_pc": 0x1010},
            {"id": 2, "pc": 0x2000, "end_pc": 0x2004},
        ],
        "functions": [
            {"entry": 0x1000, "name": "Actor::Update", "blocks": [0, 1]},
            {"entry": 0x2000, "name": "3d helper", "blocks": [2]},
        ],
    }
    generated = {
        "code_sha256": "code",
        "program_sha256": "program",
        "generator_sha256": "generator",
        "external_functions": [0x3000],
        "functions": [
            {
                "entry": 0x1000,
                "name": "Actor::Update",
                "blocks": 2,
                "instructions": 4,
                "dispatch_entries": [0x1000, 0x1008],
                "resume_entries": [0x1008],
            },
            {
                "entry": 0x2000,
                "name": "3d helper",
                "blocks": 1,
                "instructions": 1,
                "dispatch_entries": [],
                "resume_entries": [],
            },
        ],
        "shards": [
            {"file": "shard_0.cpp", "functions": 1, "entries": [0x2000]},
            {"file": "shard_1.cpp", "functions": 1, "entries": [0x1000]},
        ],
    }
    return program, generated


def test_build_guest_map_joins_program_generated_and_shards():
    program, generated = _inputs()
    result = build_guest_map(program, generated)

    assert result["function_count"] == 2
    assert result["dispatch_entry_count"] == 3
    assert result["shard_count"] == 2
    assert result["functions"][0] == {
        "entry": 0x1000,
        "name": "Actor::Update",
        "native_symbol": (
            "Oot3dNativeGame::GeneratedWholeAot::"
            "Execute_Actor__Update_00001000"
        ),
        "shard": 1,
        "shard_file": "shard_1.cpp",
        "blocks": 2,
        "instructions": 4,
        "ranges": [[0x1000, 0x1010]],
        "dispatch_entries": [0x1000, 0x1008],
        "resume_entries": [0x1008],
    }
    assert result["functions"][1]["dispatch_entries"] == [0x2000]
    assert result["external_entries"] == [0x3000]


def test_native_symbol_matches_generator_sanitization():
    assert native_symbol("3d helper", 0x2000).endswith(
        "Execute_Function_3d_helper_00002000"
    )


def test_build_guest_map_rejects_missing_shard_owner():
    program, generated = _inputs()
    generated["shards"][1]["entries"] = []
    generated["shards"][1]["functions"] = 0

    with pytest.raises(ValueError, match="has no shard"):
        build_guest_map(program, generated)


def test_attach_native_addresses_joins_linker_map_symbols():
    program, generated = _inputs()
    result = build_guest_map(program, generated)
    link_map = """
 Preferred load address is 0000000140000000
 0001:00000100 ?Execute_Actor__Update_00001000@GeneratedWholeAot@Oot3dNativeGame@@YA 0000000140001100 object.obj
 0001:00000200 ?Execute_Function_3d_helper_00002000@GeneratedWholeAot@Oot3dNativeGame@@YA 0000000140001200 object.obj
"""

    attach_native_addresses(result, link_map)

    assert result["native_image_base"] == 0x140000000
    assert result["native_symbol_count"] == 2
    assert result["functions"][0]["native_rva"] == 0x1100
    assert result["functions"][1]["native_va"] == 0x140001200


def _minimal_pe_with_exception_ranges(ranges):
    image = bytearray(0x800)
    image[:2] = b"MZ"
    struct.pack_into("<I", image, 0x3C, 0x80)
    image[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", image, 0x84, 0x8664, 1, 0x12345678,
                     0, 0, 0xF0, 0)
    optional = 0x98
    struct.pack_into("<H", image, optional, 0x20B)
    struct.pack_into("<I", image, optional + 56, 0x4000)
    struct.pack_into("<II", image, optional + 112 + 3 * 8,
                     0x2000, len(ranges) * 12)
    section = optional + 0xF0
    image[section:section + 8] = b".pdata\0\0"
    struct.pack_into("<IIII", image, section + 8, 0x200, 0x2000,
                     0x200, 0x400)
    for index, (begin, end) in enumerate(ranges):
        struct.pack_into("<III", image, 0x400 + index * 12,
                         begin, end, 0x3000)
    return bytes(image)


def test_attach_native_extents_uses_pe_exception_directory():
    program, generated = _inputs()
    result = build_guest_map(program, generated)
    attach_native_addresses(result, """
 Preferred load address is 0000000140000000
 0001:00000100 ?Execute_Actor__Update_00001000@GeneratedWholeAot@Oot3dNativeGame@@YA 0000000140001100 object.obj
 0001:00000200 ?Execute_Function_3d_helper_00002000@GeneratedWholeAot@Oot3dNativeGame@@YA 0000000140001200 object.obj
""")

    attach_native_extents(
        result, _minimal_pe_with_exception_ranges([(0x1100, 0x1180),
                                                    (0x1200, 0x1260)])
    )

    assert result["native_extent_count"] == 2
    assert result["native_machine"] == 0x8664
    assert result["native_image_timestamp"] == 0x12345678
    assert result["native_image_size"] == 0x4000
    assert result["functions"][0]["native_size"] == 0x80
    assert result["functions"][1]["native_end_rva"] == 0x1260
