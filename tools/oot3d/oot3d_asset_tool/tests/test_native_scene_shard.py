from __future__ import annotations

import json
import struct
import zipfile
from pathlib import Path

from oot3d_asset_tool.native_scene_shard import (
    build_native_scene_shard, shard_manifest_resource, write_native_scene_archive)


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


def test_native_scene_shard_preserves_original_zsi_bytes(tmp_path: Path) -> None:
    scene_root = tmp_path / "scene"
    scene_root.mkdir()
    (scene_root / "spot04_info.zsi").write_bytes(b"ZSI\x01scene")
    (scene_root / "spot04_0_info.zsi").write_bytes(b"ZSI\x01room")
    sidecar = _single_file_zar("ROOM0\\spot04_00.cmab", "cmab", b"cmab-native")
    (scene_root / "spot04.zar").write_bytes(sidecar)
    index = {"records": [{"scene_stem": "spot04", "scene_path": "spot04_info.zsi",
                           "setups": [{"index": 0, "setup_role": "gameplay", "commands": [
                               {"command_name": "skybox_settings", "command_id": 17, "offset": 8,
                                "decoded": {"skybox_id": 29}}]}],
                           "rooms": [{"room_path": "spot04_0_info.zsi"}]}]}
    manifest = build_native_scene_shard(index, scene_root, ["spot04"], "oot3d-scenes-overworld")
    output = tmp_path / "scene.o2r"
    write_native_scene_archive(output, manifest)
    with zipfile.ZipFile(output) as archive:
        assert archive.read("oot3d/native/scene/spot04_info.zsi") == b"ZSI\x01scene"
        assert archive.read("oot3d/native/scene/spot04_0_info.zsi") == b"ZSI\x01room"
        assert archive.read("oot3d/native/scene/spot04.zar") == sidecar
        manifest_path = shard_manifest_resource("oot3d-scenes-overworld")
        assert json.loads(archive.read(manifest_path))["scene_count"] == 1
        shard = json.loads(archive.read(manifest_path))
        assert shard["records"][0]["environment_setups"][0]["commands"][0]["decoded"]["skybox_id"] == 29
        animations = shard["records"][0]["room_material_animations"]
        assert animations == [{
            "room_index": 0,
            "archive_resource": "oot3d/native/scene/spot04.zar",
            "archive_byte_length": len(sidecar),
            "members": [{
                "index": 0,
                "type_local_index": 0,
                "name": "ROOM0\\spot04_00.cmab",
                "normalized_name": "ROOM0/spot04_00.cmab",
                "size": len(b"cmab-native"),
            }],
        }]
