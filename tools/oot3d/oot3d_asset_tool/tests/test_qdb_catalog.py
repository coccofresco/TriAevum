from __future__ import annotations

import json
import struct
import zipfile
from datetime import datetime, timezone
from pathlib import Path

from oot3d_asset_tool.qdb_catalog import (
    CATALOG_ARCHIVE_PATH,
    build_qdb_catalog,
    write_qdb_archive,
)


def _align(value: int, alignment: int = 4) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def _qdb() -> bytes:
    payload = bytearray()
    payload += b" BDQ"
    payload += struct.pack("<III", 3, 1, 42)
    payload += struct.pack("<I", 0x2D)
    payload += bytes(range(12))
    payload += b"\xff\xff\xff\xff"
    payload += bytes(12)
    return bytes(payload)


def _zar(member: str, payload: bytes) -> bytes:
    type_section = 0x18
    type_list = 0x28
    strings = 0x2C
    type_name = strings
    member_name = type_name + 4
    encoded_member = member.encode("ascii") + b"\0"
    meta_section = _align(member_name + len(encoded_member))
    data_section = meta_section + 8
    payload_offset = _align(data_section + 4)
    archive_size = payload_offset + len(payload)
    data = bytearray(archive_size)
    struct.pack_into("<4sIHHIII", data, 0, b"ZAR\x01", archive_size, 1, 1,
                     type_section, meta_section, data_section)
    struct.pack_into("<III", data, type_section, 1, type_list, type_name)
    struct.pack_into("<I", data, type_list, 0)
    data[type_name:type_name + 4] = b"qdb\0"
    data[member_name:member_name + len(encoded_member)] = encoded_member
    struct.pack_into("<II", data, meta_section, len(payload), member_name)
    struct.pack_into("<I", data, data_section, payload_offset)
    data[payload_offset:] = payload
    return bytes(data)


def test_catalogs_and_packages_native_qdb_bytes(tmp_path: Path) -> None:
    scene_root = tmp_path / "scene"
    scene_root.mkdir()
    member = r"..\..\scene\Spot00\demo\spot00_demo_epona_00.qdb"
    payload = _qdb()
    (scene_root / "spot00.zar").write_bytes(_zar(member, payload))

    catalog = build_qdb_catalog(
        {"scene": scene_root}, now=datetime(2026, 7, 13, tzinfo=timezone.utc)
    )

    assert catalog["status"] == "complete"
    assert catalog["summary"]["qdb_count"] == 1
    record = catalog["records"][0]
    assert record["asset_id"] == f"qdb:scene/spot00.zar!{member}"
    assert record["header"]["command_count"] == 1
    assert record["header"]["end_frame"] == 42
    assert record["header"]["trailer_status"] == "native_terminator_and_alignment"
    assert record["commands"][0]["category"] == "fixed16"

    archive_path = tmp_path / "oot3d-cutscenes.o2r"
    write_qdb_archive(archive_path, catalog)
    with zipfile.ZipFile(archive_path) as archive:
        assert json.loads(archive.read(CATALOG_ARCHIVE_PATH))["format"] == "oot3d_qdb_catalog_v1"
        assert archive.read(record["canonical_resource"]) == payload


def test_rejects_qdb_without_native_terminator(tmp_path: Path) -> None:
    actor_root = tmp_path / "actor"
    actor_root.mkdir()
    payload = _qdb()[:-16]
    (actor_root / "test.zar").write_bytes(_zar("demo/test.qdb", payload))

    catalog = build_qdb_catalog({"actor": actor_root})

    assert catalog["status"] == "invalid_qdb_payloads"
    assert catalog["issues"][0]["reason"] == "missing_native_terminator"
