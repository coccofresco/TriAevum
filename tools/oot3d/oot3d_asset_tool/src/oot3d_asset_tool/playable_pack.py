from __future__ import annotations

import argparse
import hashlib
import json
import struct
import zipfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from .native_abi_catalog import load_native_abi_catalog
from .room_compilation_pack import (
    ROOM_COMPILATION_UNIT_CATALOG_ARCHIVE_PATH,
    ROOM_COMPILATION_UNIT_ARCHIVE_ROOT,
    build_room_compilation_unit_catalog,
)


FORMAT = "oot3d_playable_pack_v1"
CATALOG_ARCHIVE_PATH = "oot3d/catalog/oot3d_asset_catalog.json"
SEMANTIC_ROUTE_CATALOG_ARCHIVE_PATH = "oot3d/catalog/oot3d_semantic_route_catalog.json"
PACK_ARCHIVE_PATH = "oot3d/catalog/oot3d_playable_pack.json"
LIGHTING_SEMANTICS_ARCHIVE_PATH = "oot3d/environment/oot3d_pica_lighting_semantics.json"
LIGHT_TRANSITION_TABLE_ARCHIVE_PATH = "oot3d/environment/light_settings_transition_table.bin"
LIGHT_TRANSITION_TABLE_PROVENANCE_ARCHIVE_PATH = "oot3d/environment/light_settings_transition_table.json"
LIGHT_TRANSITION_FALLBACK_ARCHIVE_PATH = "oot3d/environment/light_settings_transition_fallback.bin"
FOG_DEFAULT_SOURCE_ARCHIVE_PATH = "oot3d/environment/fog_default_source.bin"
FOG_VIEW_PROJECTION_DEFAULTS_ARCHIVE_PATH = "oot3d/environment/fog_view_projection_defaults.bin"
FOG_SCENE_PROJECTION_FAR_ARCHIVE_PATH = "oot3d/environment/fog_scene_projection_far.bin"
FOG_DEFAULTS_PROVENANCE_ARCHIVE_PATH = "oot3d/environment/fog_runtime_defaults.json"
KANKYO_SKYBOX_RECORDS_ARCHIVE_PATH = "oot3d/environment/kankyo_skybox_records.bin"
KANKYO_SCHEDULE_TABLE_ARCHIVE_PATH = "oot3d/environment/kankyo_schedule_table.bin"
KANKYO_DRAW_SCALE_ARCHIVE_PATH = "oot3d/environment/kankyo_draw_scale.bin"
KANKYO_TABLES_PROVENANCE_ARCHIVE_PATH = "oot3d/environment/kankyo_runtime_tables.json"
PLAYER_MODEL_RESOURCE_PROFILE_ARCHIVE_PATH = "oot3d/characters/player_model_resource_profile.json"
NATIVE_ABI_CATALOG_ARCHIVE_PATH = "oot3d/catalog/oot3d_native_abi_catalog.json"

CODE_BASE = 0x00100000
PLAYER_MODEL_GROUP_POINTER_LITERAL = 0x0032C3F8
PLAYER_MODEL_RESOURCE_POINTER_LITERAL = 0x0032C3FC
PLAYER_BODY_RESOURCE_POINTER_LITERAL = 0x004C4794
PLAYER_MODEL_GROUP_COUNT = 16
PLAYER_MODEL_TYPE_COUNT = 21
PLAYER_SHIELD_VARIANT_MODEL_TYPES = {10, 18, 19}

SHARDS = (
    "oot3d-core",
    "oot3d-scenes-overworld",
    "oot3d-scenes-indoors",
    "oot3d-scenes-dungeons",
    "oot3d-actors-static",
    "oot3d-characters",
    "oot3d-environment",
    "oot3d-cutscenes",
    "oot3d-material-animations",
    "oot3d-audio",
    "oot3d-ui",
)

ARCHIVES = {
    "oot3d-actors-static": ("static_actor_export/oot3d_static_actor_candidates.o2r",),
    "oot3d-characters": (
        "skinned_bind_pose_batch/oot3d_skinned_bind_pose_candidates.o2r",
        "skinned_animation_batch/oot3d_skinned_animation_candidates.o2r",
    ),
    "oot3d-environment": ("kankyo_environment_export/oot3d_kankyo_environment_candidates.o2r",),
    "oot3d-cutscenes": ("qdb_catalog/oot3d-cutscenes.o2r",),
}


def build_player_model_resource_profile(code_bytes: bytes) -> dict[str, Any]:
    def offset(runtime_address: int, size: int) -> int:
        result = runtime_address - CODE_BASE
        if result < 0 or result + size > len(code_bytes):
            raise ValueError(f"runtime range 0x{runtime_address:08X}+0x{size:X} lies outside code.bin")
        return result

    def u8(runtime_address: int) -> int:
        return code_bytes[offset(runtime_address, 1)]

    def u32(runtime_address: int) -> int:
        return struct.unpack_from("<I", code_bytes, offset(runtime_address, 4))[0]

    model_group_table = u32(PLAYER_MODEL_GROUP_POINTER_LITERAL)
    model_resource_pointer_table = u32(PLAYER_MODEL_RESOURCE_POINTER_LITERAL)
    body_resource_table = u32(PLAYER_BODY_RESOURCE_POINTER_LITERAL)
    model_groups = []
    for index in range(PLAYER_MODEL_GROUP_COUNT):
        row_address = model_group_table + index * 5
        row = [u8(row_address + column) for column in range(5)]
        model_groups.append({
            "index": index,
            "runtime_address": row_address,
            "animation_type": row[0],
            "model_types": row[1:],
        })

    model_types = []
    for index in range(PLAYER_MODEL_TYPE_COUNT):
        table_address = u32(model_resource_pointer_table + index * 4)
        record: dict[str, Any] = {
            "index": index,
            "runtime_address": table_address,
            "selection_kind": "shield_then_age" if index in PLAYER_SHIELD_VARIANT_MODEL_TYPES else "age",
            "resource_ids_by_age": [u32(table_address), u32(table_address + 4)],
            "far_resource_ids_by_age": [u32(table_address + 8), u32(table_address + 12)],
        }
        if index in PLAYER_SHIELD_VARIANT_MODEL_TYPES:
            record["shield_variant_resource_ids_by_age"] = [
                [u32(table_address + shield * 0x10), u32(table_address + shield * 0x10 + 4)]
                for shield in range(4)
            ]
            record["shield_variant_far_resource_ids_by_age"] = [
                [u32(table_address + shield * 0x10 + 8), u32(table_address + shield * 0x10 + 12)]
                for shield in range(4)
            ]
        model_types.append(record)

    body_resource_ids_by_age = [[], []]
    for slot in range(4):
        body_resource_ids_by_age[0].append(u32(body_resource_table + slot * 8))
        body_resource_ids_by_age[1].append(u32(body_resource_table + slot * 8 + 4))

    return {
        "format": "oot3d_player_model_resource_profile_v1",
        "source": "code.bin",
        "code_base": CODE_BASE,
        "code_sha256": hashlib.sha256(code_bytes).hexdigest(),
        "age_order": ["adult", "child"],
        "resource_id_sentinel": 0xFFFFFFFF,
        "body_resource_ids_by_age": body_resource_ids_by_age,
        "model_groups": model_groups,
        "model_types": model_types,
        "provenance": {
            "player_set_models": "OOT3D code.bin 0x0032C2C0",
            "player_draw_visibility": "OOT3D code.bin 0x004C4560",
            "model_group_table": model_group_table,
            "model_resource_pointer_table": model_resource_pointer_table,
            "body_resource_table": body_resource_table,
            "shield_variant_model_types": sorted(PLAYER_SHIELD_VARIANT_MODEL_TYPES),
            "policy": "runtime semantic state selects native OOT3D resource ids; N64 assets are not inputs",
        },
    }


def _shard_for(record: dict[str, Any]) -> tuple[str | None, str | None]:
    family = record.get("family")
    metadata = record.get("metadata", {})
    if family == "actor_model" and metadata.get("model_kind") == "rigid":
        return "oot3d-actors-static", None
    if family == "actor_model" and metadata.get("model_kind") == "skinned":
        return "oot3d-characters", None
    if family == "skeletal_animation":
        return "oot3d-characters", None
    if family == "environment_model":
        return "oot3d-environment", None
    if family == "cutscene_timeline":
        return "oot3d-cutscenes", None
    if family == "scene_profile":
        return str(metadata.get("scene_shard") or "oot3d-core"), None
    if family == "scene_room_source":
        return ((str(metadata["scene_shard"]), None) if metadata.get("scene_shard")
                else (None, "room_source_archive_missing"))
    if family == "scene_collision":
        return None, "scene_shard_classification_missing"
    if family == "texture":
        return None, "standalone_texture_archive_missing"
    if family == "route_binding":
        return None, "route_binding_not_packaged_asset"
    return None, "shard_rule_missing"


def build_playable_pack(catalog: dict[str, Any], work_root: Path,
                        *, now: datetime | None = None) -> dict[str, Any]:
    now = now or datetime.now(timezone.utc)
    if catalog.get("format") != "oot3d_asset_catalog_v1" or catalog.get("status") != "complete":
        raise ValueError("playable pack requires a complete oot3d_asset_catalog_v1")
    assigned: dict[str, list[dict[str, Any]]] = {name: [] for name in SHARDS}
    unassigned: list[dict[str, str]] = []
    for record in catalog.get("records", []):
        shard, blocker = _shard_for(record)
        if shard:
            assigned[shard].append(record)
        else:
            unassigned.append({"asset_id": str(record.get("asset_id")), "blocker": str(blocker)})

    archives_by_shard = dict(ARCHIVES)
    shard_root = work_root / "native_scene_shards"
    for manifest_path in shard_root.glob("*.manifest.json") if shard_root.is_dir() else []:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
        shard_name = str(manifest.get("shard_name", ""))
        archive_path = manifest_path.with_name(manifest_path.name.removesuffix(".manifest.json") + ".o2r")
        if shard_name:
            archives_by_shard[shard_name] = (str(archive_path),)
    shards = []
    for name in SHARDS:
        records = assigned[name]
        archives = []
        for relative in archives_by_shard.get(name, ()):
            path = work_root / relative
            archives.append({
                "path": str(path),
                "exists": path.is_file(),
                "byte_length": path.stat().st_size if path.is_file() else 0,
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None,
            })
        capabilities = sorted({
            capability
            for record in records
            for capability in record.get("required_engine_capabilities", [])
        })
        archive_ready = (name == "oot3d-core") or (bool(archives) and all(archive["exists"] for archive in archives))
        status = "ready" if records and archive_ready else "empty" if not records else "blocked_missing_archive"
        shards.append({
            "name": name,
            "status": status,
            "record_count": len(records),
            "asset_ids": sorted(str(record["asset_id"]) for record in records),
            "required_engine_capabilities": capabilities,
            "archives": archives,
        })
    blocker_counts: dict[str, int] = {}
    for row in unassigned:
        blocker_counts[row["blocker"]] = blocker_counts.get(row["blocker"], 0) + 1
    return {
        "format": FORMAT,
        "generated_utc": now.isoformat(),
        "status": "complete",
        "catalog_record_count": len(catalog.get("records", [])),
        "assigned_record_count": sum(len(records) for records in assigned.values()),
        "unassigned_record_count": len(unassigned),
        "unassigned_blocker_counts": dict(sorted(blocker_counts.items())),
        "unassigned": sorted(unassigned, key=lambda row: row["asset_id"]),
        "shards": shards,
    }


def write_core_archive(path: Path, catalog: dict[str, Any], pack: dict[str, Any],
                       lighting_semantics: dict[str, Any], transition_table: bytes = b"",
                       transition_provenance: dict[str, Any] | None = None,
                       transition_fallback: bytes = b"",
                       fog_default_source: bytes = b"",
                       fog_view_projection_defaults: bytes = b"",
                       fog_scene_projection_far: bytes = b"",
                       fog_defaults_provenance: dict[str, Any] | None = None,
                       kankyo_skybox_records: bytes = b"",
                       kankyo_schedule_table: bytes = b"",
                       kankyo_draw_scale: bytes = b"",
                       kankyo_tables_provenance: dict[str, Any] | None = None,
                        semantic_routes: dict[str, Any] | None = None,
                        player_model_resource_profile: dict[str, Any] | None = None,
                        native_abi_catalog: dict[str, Any] | None = None,
                        room_compilation_unit_catalog: dict[str, Any] | None = None,
                       room_compilation_units: dict[str, dict[str, Any]] | None = None) -> None:
    manifest = {
        "name": "OOT3D Native Asset Core",
        "author": "local",
        "version": "0.1.0",
        "description": "Generated OOT3D native asset catalog and shard orchestration metadata.",
        "license": "Generated from user-provided local assets; do not redistribute without rights.",
        "code_version": 1,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, payload in (("manifest.json", manifest), (CATALOG_ARCHIVE_PATH, catalog),
                              (PACK_ARCHIVE_PATH, pack),
                              (LIGHTING_SEMANTICS_ARCHIVE_PATH, lighting_semantics)):
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, json.dumps(payload, indent=2) + "\n")
        if semantic_routes is not None:
            info = zipfile.ZipInfo(SEMANTIC_ROUTE_CATALOG_ARCHIVE_PATH,
                                   date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, json.dumps(semantic_routes, indent=2) + "\n")
        if player_model_resource_profile is not None:
            info = zipfile.ZipInfo(PLAYER_MODEL_RESOURCE_PROFILE_ARCHIVE_PATH,
                                   date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, json.dumps(player_model_resource_profile, indent=2) + "\n")
        if native_abi_catalog is not None:
            info = zipfile.ZipInfo(NATIVE_ABI_CATALOG_ARCHIVE_PATH,
                                   date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, json.dumps(native_abi_catalog, indent=2) + "\n")
        if room_compilation_unit_catalog is not None:
            info = zipfile.ZipInfo(ROOM_COMPILATION_UNIT_CATALOG_ARCHIVE_PATH,
                                   date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(
                info, json.dumps(room_compilation_unit_catalog, indent=2, sort_keys=True) + "\n"
            )
            for resource_path, unit in sorted((room_compilation_units or {}).items()):
                info = zipfile.ZipInfo(resource_path, date_time=(1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o644 << 16
                archive.writestr(info, json.dumps(unit, indent=2, sort_keys=True) + "\n")
        if transition_table:
            for name, payload in (
                (LIGHT_TRANSITION_TABLE_ARCHIVE_PATH, transition_table),
                (LIGHT_TRANSITION_TABLE_PROVENANCE_ARCHIVE_PATH,
                 (json.dumps(transition_provenance or {}, indent=2) + "\n").encode("utf-8")),
            ):
                info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o644 << 16
                archive.writestr(info, payload)
        if kankyo_skybox_records:
            for name, payload in (
                (KANKYO_SKYBOX_RECORDS_ARCHIVE_PATH, kankyo_skybox_records),
                (KANKYO_SCHEDULE_TABLE_ARCHIVE_PATH, kankyo_schedule_table),
                (KANKYO_DRAW_SCALE_ARCHIVE_PATH, kankyo_draw_scale),
                (KANKYO_TABLES_PROVENANCE_ARCHIVE_PATH,
                 (json.dumps(kankyo_tables_provenance or {}, indent=2) + "\n").encode("utf-8")),
            ):
                info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o644 << 16
                archive.writestr(info, payload)
        if transition_fallback:
            info = zipfile.ZipInfo(LIGHT_TRANSITION_FALLBACK_ARCHIVE_PATH,
                                   date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, transition_fallback)
        if fog_default_source:
            for name, payload in (
                (FOG_DEFAULT_SOURCE_ARCHIVE_PATH, fog_default_source),
                (FOG_VIEW_PROJECTION_DEFAULTS_ARCHIVE_PATH, fog_view_projection_defaults),
                (FOG_SCENE_PROJECTION_FAR_ARCHIVE_PATH, fog_scene_projection_far),
                (FOG_DEFAULTS_PROVENANCE_ARCHIVE_PATH,
                 (json.dumps(fog_defaults_provenance or {}, indent=2) + "\n").encode("utf-8")),
            ):
                info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o644 << 16
                archive.writestr(info, payload)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Build OOT3D playable archive shard manifests.")
    parser.add_argument("--catalog", type=Path, required=True)
    parser.add_argument("--work-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--core-archive", type=Path)
    parser.add_argument("--lighting-semantics", type=Path)
    parser.add_argument("--semantic-routes", type=Path)
    parser.add_argument("--room-unit-root", type=Path)
    parser.add_argument("--code-bin", type=Path)
    parser.add_argument("--native-abi-catalog", type=Path)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args(argv)
    catalog = json.loads(args.catalog.read_text(encoding="utf-8-sig"))
    result = build_playable_pack(catalog, args.work_root)
    room_compilation_unit_catalog = None
    room_compilation_units = None
    if args.room_unit_root is not None:
        try:
            room_compilation_unit_catalog, room_compilation_units = (
                build_room_compilation_unit_catalog(args.room_unit_root)
            )
        except (OSError, ValueError, json.JSONDecodeError) as error:
            parser.error(str(error))
        result["room_compilation_units"] = {
            "catalog_archive_path": ROOM_COMPILATION_UNIT_CATALOG_ARCHIVE_PATH,
            "status": room_compilation_unit_catalog["status"],
            "unit_count": room_compilation_unit_catalog["unit_count"],
            "excluded_unit_count": room_compilation_unit_catalog["excluded_unit_count"],
        }
    if args.core_archive:
        if not args.lighting_semantics or not args.lighting_semantics.is_file():
            parser.error("--lighting-semantics must name the native PICA semantics JSON when writing the core archive")
        lighting_semantics = json.loads(args.lighting_semantics.read_text(encoding="utf-8-sig"))
        if lighting_semantics.get("format") != "oot3d_pica_lighting_semantics_v1":
            parser.error("unsupported native PICA lighting semantics format")
        semantic_routes = None
        if args.semantic_routes is not None:
            semantic_routes = json.loads(args.semantic_routes.read_text(encoding="utf-8-sig"))
            if (semantic_routes.get("format") != "oot3d_semantic_route_catalog_v1" or
                    semantic_routes.get("status") != "complete"):
                parser.error("unsupported or incomplete OOT3D semantic route catalog")
        if not args.code_bin or not args.code_bin.is_file():
            parser.error("--code-bin must name the original OOT3D executable when writing the core archive")
        if not args.native_abi_catalog or not args.native_abi_catalog.is_file():
            parser.error(
                "--native-abi-catalog must name the reviewed Zelda3drecomp ABI catalog "
                "when writing the core archive"
            )
        try:
            native_abi_catalog = load_native_abi_catalog(args.native_abi_catalog)
        except (OSError, ValueError, json.JSONDecodeError) as error:
            parser.error(str(error))
        code_base = CODE_BASE
        table_address = 0x00531EFC
        table_size = 5 * 0x36
        code_bytes = args.code_bin.read_bytes()
        if hashlib.sha256(code_bytes).hexdigest() != native_abi_catalog.get(
            "code_bin_sha256"
        ):
            parser.error("native ABI catalog targets a different OOT3D code.bin")
        result["native_abi_catalog"] = {
            "archive_path": NATIVE_ABI_CATALOG_ARCHIVE_PATH,
            "source_snapshot_id": native_abi_catalog["source_snapshot_id"],
            "source_base_revision": native_abi_catalog["source_base_revision"],
            "function_count": native_abi_catalog["function_count"],
            "payload_sha256": native_abi_catalog["payload_sha256"],
        }
        result["core_archive"] = {
            "status": "ready",
            "path": str(args.core_archive.resolve()),
            "native_abi_catalog_resource": NATIVE_ABI_CATALOG_ARCHIVE_PATH,
        }
        table_offset = table_address - code_base
        transition_table = code_bytes[table_offset:table_offset + table_size]
        if len(transition_table) != table_size:
            parser.error("OOT3D light transition table lies outside code.bin")
        transition_provenance = {
            "format": "oot3d_light_settings_transition_table_v1",
            "source": "code.bin",
            "code_base": code_base,
            "runtime_address": table_address,
            "size": table_size,
            "mode_count": 5,
            "mode_stride": 0x36,
            "entry_count": 9,
            "entry_size": 6,
            "sha256": hashlib.sha256(transition_table).hexdigest(),
        }
        fallback_address = 0x00531EB4
        fallback_size = 0x2C
        fallback_offset = fallback_address - code_base
        transition_fallback = code_bytes[fallback_offset:fallback_offset + fallback_size]
        if len(transition_fallback) != fallback_size:
            parser.error("OOT3D light transition fallback state lies outside code.bin")
        transition_provenance["fallback_runtime_address"] = fallback_address
        transition_provenance["fallback_size"] = fallback_size
        transition_provenance["fallback_sha256"] = hashlib.sha256(transition_fallback).hexdigest()
        fog_source_address = 0x004FA8B8
        fog_source_size = 0x43
        fog_view_defaults_address = 0x002E5B00
        fog_view_defaults_size = 8
        fog_scene_far_address = 0x00479290
        fog_scene_far_size = 4
        def code_slice(runtime_address: int, size: int, label: str) -> bytes:
            offset = runtime_address - code_base
            payload = code_bytes[offset:offset + size]
            if len(payload) != size:
                parser.error(f"OOT3D {label} lies outside code.bin")
            return payload
        fog_default_source = code_slice(fog_source_address, fog_source_size, "fog default source")
        fog_view_projection_defaults = code_slice(
            fog_view_defaults_address, fog_view_defaults_size, "fog view projection defaults")
        fog_scene_projection_far = code_slice(
            fog_scene_far_address, fog_scene_far_size, "fog scene projection far")
        fog_defaults_provenance = {
            "format": "oot3d_fog_runtime_defaults_v1",
            "source": "code.bin",
            "code_base": code_base,
            "resources": [
                {"archive_path": FOG_DEFAULT_SOURCE_ARCHIVE_PATH,
                 "runtime_address": fog_source_address, "size": fog_source_size,
                 "sha256": hashlib.sha256(fog_default_source).hexdigest()},
                {"archive_path": FOG_VIEW_PROJECTION_DEFAULTS_ARCHIVE_PATH,
                 "runtime_address": fog_view_defaults_address, "size": fog_view_defaults_size,
                 "sha256": hashlib.sha256(fog_view_projection_defaults).hexdigest()},
                {"archive_path": FOG_SCENE_PROJECTION_FAR_ARCHIVE_PATH,
                 "runtime_address": fog_scene_far_address, "size": fog_scene_far_size,
                 "sha256": hashlib.sha256(fog_scene_projection_far).hexdigest()},
            ],
        }
        kankyo_skybox_address = 0x0054984C
        kankyo_skybox_record_count = 30
        kankyo_skybox_record_size = 0x50
        kankyo_schedule_address = 0x0053200A
        kankyo_schedule_mode_count = 5
        kankyo_schedule_mode_size = 0x48
        kankyo_draw_scale_address = 0x0047D1D0
        kankyo_skybox_records = code_slice(
            kankyo_skybox_address, kankyo_skybox_record_count * kankyo_skybox_record_size,
            "kankyo skybox record table")
        kankyo_schedule_table = code_slice(
            kankyo_schedule_address, kankyo_schedule_mode_count * kankyo_schedule_mode_size,
            "kankyo schedule table")
        kankyo_draw_scale = code_slice(kankyo_draw_scale_address, 4, "kankyo draw scale")
        kankyo_tables_provenance = {
            "format": "oot3d_kankyo_runtime_tables_v1", "source": "code.bin",
            "code_base": code_base,
            "skybox_record_count": kankyo_skybox_record_count,
            "skybox_record_size": kankyo_skybox_record_size,
            "schedule_mode_count": kankyo_schedule_mode_count,
            "schedule_mode_size": kankyo_schedule_mode_size,
            "resources": [
                {"archive_path": KANKYO_SKYBOX_RECORDS_ARCHIVE_PATH,
                 "runtime_address": kankyo_skybox_address, "size": len(kankyo_skybox_records),
                 "sha256": hashlib.sha256(kankyo_skybox_records).hexdigest()},
                {"archive_path": KANKYO_SCHEDULE_TABLE_ARCHIVE_PATH,
                 "runtime_address": kankyo_schedule_address, "size": len(kankyo_schedule_table),
                 "sha256": hashlib.sha256(kankyo_schedule_table).hexdigest()},
                {"archive_path": KANKYO_DRAW_SCALE_ARCHIVE_PATH,
                 "runtime_address": kankyo_draw_scale_address, "size": len(kankyo_draw_scale),
                 "sha256": hashlib.sha256(kankyo_draw_scale).hexdigest()},
            ],
        }
        player_model_resource_profile = build_player_model_resource_profile(code_bytes)
        write_core_archive(args.core_archive, catalog, result, lighting_semantics,
                           transition_table, transition_provenance, transition_fallback,
                           fog_default_source, fog_view_projection_defaults,
                           fog_scene_projection_far, fog_defaults_provenance,
                           kankyo_skybox_records, kankyo_schedule_table,
                           kankyo_draw_scale, kankyo_tables_provenance,
                           semantic_routes, player_model_resource_profile,
                           native_abi_catalog,
                           room_compilation_unit_catalog, room_compilation_units)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(result, indent=2) + "\n", encoding="utf-8", newline="\n"
    )
    print(args.output)
    if args.core_archive:
        print(args.core_archive)
    if args.verify and any(shard["status"] == "blocked_missing_archive" for shard in result["shards"]):
        return 1
    if (args.verify and args.room_unit_root is not None and
            room_compilation_unit_catalog is not None and
            room_compilation_unit_catalog["unit_count"] == 0):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
