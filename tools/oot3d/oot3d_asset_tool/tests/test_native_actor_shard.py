from __future__ import annotations

import json
import struct
from pathlib import Path

import pytest

from oot3d_asset_tool.native_actor_shard import (
    build_manifest,
    room_compilation_object_sources,
)


def _single_file_zar(name: str, type_name: str, payload: bytes) -> bytes:
    type_section = 0x20
    type_index_list = 0x30
    type_name_offset = 0x34
    meta_section = 0x40
    file_name_offset = 0x50
    data_section = 0x70
    payload_offset = 0x80
    data = bytearray(payload_offset + len(payload))
    data[:4] = b"ZAR\x01"
    struct.pack_into("<IHHIII", data, 4, len(data), 1, 1, type_section, meta_section, data_section)
    struct.pack_into("<III", data, type_section, 1, type_index_list, type_name_offset)
    struct.pack_into("<I", data, type_index_list, 0)
    data[type_name_offset:type_name_offset + len(type_name) + 1] = type_name.encode("ascii") + b"\0"
    struct.pack_into("<II", data, meta_section, len(payload), file_name_offset)
    data[file_name_offset:file_name_offset + len(name) + 1] = name.encode("ascii") + b"\0"
    struct.pack_into("<I", data, data_section, payload_offset)
    data[payload_offset:] = payload
    return bytes(data)


def _room_unit(path: Path, logical_path: str = "actor/zelda_test.zar") -> None:
    path.write_text(json.dumps({
        "format": "oot3d_room_compilation_unit_v1",
        "identity": {"unit_id": "scene_entry:test:setup-0"},
        "object_dependencies": [
            {
                "object_id": 7,
                "payload_status": "native_zar_materialized",
                "source": {"logical_path": logical_path},
                "required_by": ["room:0"],
            },
            {
                "object_id": 8,
                "payload_status": "native_unmaterialized_object_bank_reference",
                "native_path_record": {"logical_path": "actor/object_os_anime.zar"},
                "required_by": ["room:0"],
            },
        ],
    }), encoding="utf-8")


def test_room_compilation_object_sources_preserve_native_bank_contract(tmp_path: Path) -> None:
    actor_root = tmp_path / "actor"
    actor_root.mkdir()
    source = actor_root / "zelda_test.zar"
    source.write_bytes(_single_file_zar("Model/test.cmb", "cmb", b"cmb-native"))
    unit_path = tmp_path / "room.json"
    _room_unit(unit_path)

    sources, bindings = room_compilation_object_sources([unit_path], actor_root)
    assert sources == [source.resolve()]
    assert bindings == [
        {
            "unit_id": "scene_entry:test:setup-0",
            "object_id": 7,
            "payload_status": "native_zar_materialized",
            "source_container": "actor/zelda_test.zar",
            "resource": "oot3d/native/actor/zelda_test.zar",
            "required_by": ["room:0"],
        },
        {
            "unit_id": "scene_entry:test:setup-0",
            "object_id": 8,
            "payload_status": "native_unmaterialized_object_bank_reference",
            "source_container": "actor/object_os_anime.zar",
            "resource": None,
            "required_by": ["room:0"],
        },
    ]
    manifest = build_manifest(sources, bindings)
    assert manifest["archive_count"] == 1
    assert manifest["object_bank_bindings"] == bindings
    assert manifest["records"][0]["source_container"] == "actor/zelda_test.zar"


def test_room_compilation_object_sources_reject_path_escape(tmp_path: Path) -> None:
    actor_root = tmp_path / "actor"
    actor_root.mkdir()
    unit_path = tmp_path / "room.json"
    _room_unit(unit_path, "actor/../outside.zar")
    with pytest.raises(ValueError, match="invalid actor path"):
        room_compilation_object_sources([unit_path], actor_root)
