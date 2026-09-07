from __future__ import annotations

import argparse
import hashlib
import json
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from typing import Any

from .native_scene_shard import shard_manifest_resource


FORMAT = "oot3d_asset_catalog_v1"

MANIFESTS = {
    "ctxb": "ctxb_texture_export/ctxb_texture_export_manifest.json",
    "static_actor": "static_actor_export/converted/batch_manifest.json",
    "skinned_model": "skinned_bind_pose_batch/skinned_bind_pose_batch_manifest.json",
    "csab": "skinned_animation_batch/csab_skeleton_track_batch_manifest.json",
    "collision": "collision_activation/collision_activation_full_visual_manifest.json",
    "kankyo": "kankyo_environment_export/kankyo_environment_export_manifest.json",
    "route": "kokiri_runtime_route/oot3d_kokiri_runtime_manifest_enabled.json",
}


def _load(path: Path) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(data, dict):
        raise ValueError(f"{path}: expected JSON object")
    return data


def _native_path(value: str) -> str:
    normalized = value.replace("\\", "/")
    marker = "/romfs/"
    lower = normalized.lower()
    if marker in lower:
        normalized = normalized[lower.index(marker) + len(marker):]
    return str(PurePosixPath(normalized)).lstrip("./")


def _source_identity(family: str, container: str, member: str | None = None) -> str:
    identity = f"{family}:{_native_path(container)}"
    if member:
        identity += f"!{str(PurePosixPath(member))}"
    return identity


def _resource_paths(record: dict[str, Any], *, entry_only: bool = False) -> list[str]:
    resources = record.get("resources", [])
    if not isinstance(resources, list):
        return []
    paths = [str(item.get("path")) for item in resources if isinstance(item, dict) and item.get("path")]
    if entry_only:
        symbol = str(record.get("symbol", ""))
        selected = [path for path in paths if symbol and path.rsplit("/", 1)[-1] == symbol]
        return selected
    return paths


def _record(asset_id: str, family: str, source: str, resources: list[str], provenance: str,
            *, dependencies: list[str] | None = None, metadata: dict[str, Any] | None = None,
            required_engine_capabilities: list[str] | None = None,
            ownership: dict[str, Any] | None = None,
            runtime_state: str = "packaged_not_bound") -> dict[str, Any]:
    tier_by_state = {"decoded_not_packaged": 1, "packaged_not_bound": 2, "runtime_candidate": 3}
    return {
        "asset_id": asset_id,
        "family": family,
        "source_identity": source,
        "canonical_resources": sorted(set(resources)),
        "dependencies": sorted(set(dependencies or [])),
        "required_engine_capabilities": sorted(set(required_engine_capabilities or [])),
        "support_tier": tier_by_state[runtime_state],
        "runtime_state": runtime_state,
        "provenance_manifest": provenance,
        "metadata": metadata or {},
        "ownership": ownership or {},
    }


def build_asset_catalog(work_root: Path, *, scene_index_path: Path | None = None,
                        scene_resource_table_path: Path | None = None,
                        scene_shard_manifest_paths: list[Path] | None = None,
                        qdb_catalog_path: Path | None = None,
                        now: datetime | None = None) -> dict[str, Any]:
    now = now or datetime.now(timezone.utc)
    paths = {key: work_root / relative for key, relative in MANIFESTS.items()}
    missing = [key for key, path in paths.items() if not path.is_file()]
    if missing:
        return {
            "format": FORMAT,
            "generated_utc": now.isoformat(),
            "work_root": str(work_root),
            "status": "blocked_missing_manifests",
            "missing_manifests": missing,
            "records": [],
        }
    manifests = {key: _load(path) for key, path in paths.items()}
    records: list[dict[str, Any]] = []
    scene_ids_by_stem: dict[str, int] = {}
    room_source_records: dict[str, dict[str, Any]] = {}
    scene_shard_resources: dict[str, tuple[str, str, dict[str, Any]]] = {}
    for shard_path in scene_shard_manifest_paths or []:
        shard = _load(shard_path)
        if shard.get("format") != "oot3d_native_scene_shard_v1" or shard.get("status") != "complete":
            continue
        shard_name = str(shard["shard_name"])
        for source in shard.get("sources", []):
            scene_shard_resources[str(source["name"])] = (str(source["resource"]), shard_name, source)

    if scene_index_path is not None and scene_resource_table_path is not None:
        scene_index = _load(scene_index_path)
        resource_table = _load(scene_resource_table_path)
        scene_ids = {str(row.get("zsi_path")): int(row["scene_id"])
                     for row in resource_table.get("rows", []) if row.get("zsi_path") and "scene_id" in row}
        for row in list(resource_table.get("rows", [])) + list(resource_table.get("variant_rows", [])):
            stem = str(row.get("native_scene_index_stem") or "")
            if stem and "scene_id" in row:
                scene_ids_by_stem[stem] = int(row["scene_id"])
        for scene in scene_index.get("records", []):
            scene_path = str(scene["scene_path"])
            scene_stem = str(scene["scene_stem"])
            scene_id = scene_ids.get(scene_path, -1)
            scene_asset_id = _source_identity("scene", scene_path)
            scene_shard = scene_shard_resources.get(scene_path)
            setup_indices = [int(setup["index"]) for setup in scene.get("setups", [])]
            command_ids = sorted({command_id for setup in scene.get("setups", [])
                                  for command_id in setup.get("command_ids", [])})
            light_setting_count = sum(
                len(command.get("decoded", {}).get("settings", []))
                for setup in scene.get("setups", []) for command in setup.get("commands", [])
                if command.get("command_name") == "light_settings_list"
            )
            records.append(_record(
                scene_asset_id, "scene_profile", scene_asset_id,
                [scene_shard[0]] if scene_shard else [], str(scene_index_path),
                required_engine_capabilities=["native_zsi_source_archive", "native_zsi_scene_commands",
                                              "native_pica_scene_lighting",
                                              "native_pica_fog", "native_kankyo_environment"],
                ownership={"scene_id": scene_id, "scene_stem": scene_stem,
                           "setup_indices": setup_indices,
                           "scene_shard_manifest_resource": (shard_manifest_resource(scene_shard[1])
                                                             if scene_shard else None)},
                metadata={"scene_path": scene_path, "scene_shard": scene_shard[1] if scene_shard else None,
                          "command_ids": command_ids,
                          "light_setting_count": light_setting_count,
                          "setup_roles": [setup.get("setup_role") for setup in scene.get("setups", [])]},
                runtime_state="packaged_not_bound" if scene_shard else "decoded_not_packaged",
            ))
            for room in scene.get("rooms", []):
                room_path = str(room["room_path"])
                room_asset_id = _source_identity("room", room_path)
                room_shard = scene_shard_resources.get(room_path)
                existing = room_source_records.get(room_asset_id)
                if existing is None:
                    room_source_records[room_asset_id] = _record(
                        room_asset_id, "scene_room_source", room_asset_id,
                        [room_shard[0]] if room_shard else [], str(scene_index_path),
                        dependencies=[scene_asset_id],
                        required_engine_capabilities=["native_zsi_source_archive", "native_cmb_geometry",
                                                      "native_ctxb_texture",
                                                      "native_pica_material"],
                        ownership={"scene_id": scene_id, "scene_stem": scene_stem,
                                   "room_index": room.get("room_index", -1),
                                   "setup_indices": setup_indices,
                                   "scene_shard_manifest_resource": (shard_manifest_resource(room_shard[1])
                                                                     if room_shard else None)},
                        metadata={"room_path": room_path, "scene_shard": room_shard[1] if room_shard else None,
                                  "embedded_cmb_count": (room_shard[2].get("embedded_cmb_count")
                                                         if room_shard else room.get("embedded_cmb_count")),
                                  "embedded_cmb_count_status": ("parsed_from_packaged_source" if room_shard
                                                                else "scene_index_value"),
                                  "embedded_cmbs": room_shard[2].get("embedded_cmbs", []) if room_shard else [],
                                  "room_actor_status": room.get("room_actor_list", {}).get("status"),
                                  "owner_scene_assets": [scene_asset_id]},
                        runtime_state="packaged_not_bound" if room_shard else "decoded_not_packaged",
                    )
                else:
                    existing["dependencies"] = sorted(set(existing["dependencies"] + [scene_asset_id]))
                    owners = existing["metadata"]["owner_scene_assets"]
                    existing["metadata"]["owner_scene_assets"] = sorted(set(owners + [scene_asset_id]))
                    setups = existing["ownership"]["setup_indices"]
                    existing["ownership"]["setup_indices"] = sorted(set(setups + setup_indices))
        records.extend(room_source_records.values())

    for row in manifests["ctxb"].get("records", []):
        if row.get("status") != "exported":
            continue
        if row.get("source_kind") == "embedded_zar":
            source = _source_identity("ctxb", str(row["container_path"]), str(row["embedded_name"]))
        else:
            source = _source_identity("ctxb", str(row["path"]))
        records.append(_record(
            source, "texture", source, [str(row["resource_path"])], MANIFESTS["ctxb"],
            required_engine_capabilities=["native_ctxb_texture"],
            metadata={"width": row.get("width"), "height": row.get("height"),
                      "format_pair": row.get("format_pair")},
        ))

    for row in manifests["static_actor"].get("records", []):
        if row.get("status") != "converted":
            continue
        source_value = _native_path(str(row["source"]))
        container, _, member = source_value.partition("!")
        source = _source_identity("cmb", container, member or None)
        records.append(_record(
            source, "actor_model", source, _resource_paths(row, entry_only=True),
            MANIFESTS["static_actor"],
            required_engine_capabilities=["native_cmb_geometry", "native_ctxb_texture",
                                          "native_pica_material"],
            metadata={"model_kind": "rigid", "resource_root": row.get("resource_root"),
                      "symbol": row.get("symbol"), "resource_count": row.get("resource_count"),
                      "material_signatures": sorted({
                          str(material.get("raw_material_sha256"))
                          for material in row.get("summary", {}).get("materials", [])
                          if material.get("raw_material_sha256")
                      })},
        ))

    for row in manifests["skinned_model"].get("records", []):
        if row.get("status") != "exported":
            continue
        source = _source_identity("cmb", f"actor/{row['container_path']}", str(row["embedded_name"]))
        output = str(row["output"])
        records.append(_record(
            source, "actor_model", source, [output], MANIFESTS["skinned_model"],
            required_engine_capabilities=["native_cmb_geometry", "native_ctxb_texture",
                                          "native_pica_material", "native_cmb_skinning"],
            metadata={"model_kind": "skinned", "model_name": row.get("model_name"),
                      "bone_count": row.get("bone_count"), "counts": row.get("counts", {})},
        ))

    for row in manifests["csab"].get("records", []):
        if row.get("status") != "exported":
            continue
        source = _source_identity("csab", f"actor/{row['archive_path']}", str(row["csab_name"]))
        target = _source_identity("cmb", f"actor/{row['archive_path']}", str(row["target_cmb_name"]))
        track = str(PurePosixPath("animations/oot3d/csab/skinned") / Path(str(row["track_export"])).name)
        records.append(_record(
            source, "skeletal_animation", source, [track], MANIFESTS["csab"],
            dependencies=[target],
            required_engine_capabilities=["native_csab_animation"],
            metadata={"frame_slot_count": row.get("frame_slot_count"),
                      "target_resolution_status": row.get("target_resolution_status")},
        ))

    for row in manifests["collision"].get("records", []):
        if row.get("status") != "converted":
            continue
        source_zsi = _native_path(str(row["source_zsi"]))
        candidate = row.get("summary", {}).get("candidate_index", 0)
        source = f"collision:{source_zsi}#{candidate}"
        records.append(_record(
            source, "scene_collision", source, [str(row["resource_path"])], MANIFESTS["collision"],
            required_engine_capabilities=["native_zsi_collision"],
            ownership={"scene_id": scene_ids_by_stem.get(str(row.get("scene")), -1),
                       "scene_stem": row.get("scene"), "setup_indices": []},
            metadata={"scene_stem": row.get("scene"), "resource_format": row.get("resource_format"),
                      "summary": row.get("summary", {})},
        ))

    for row in manifests["kankyo"].get("records", []):
        if row.get("status") != "converted":
            continue
        source = _source_identity("cmb", f"kankyo/{row['archive_path']}", str(row["embedded_name"]))
        records.append(_record(
            source, "environment_model", source, _resource_paths(row, entry_only=True),
            MANIFESTS["kankyo"],
            required_engine_capabilities=["native_cmb_geometry", "native_ctxb_texture",
                                          "native_pica_material", "native_kankyo_environment"],
            metadata={"environment_group": row.get("environment_group"),
                      "resource_root": row.get("resource_root"), "symbol": row.get("symbol")},
        ))

    if qdb_catalog_path is not None:
        qdb_catalog = _load(qdb_catalog_path)
        if (qdb_catalog.get("format") != "oot3d_qdb_catalog_v1" or
                qdb_catalog.get("status") != "complete"):
            raise ValueError("asset catalog requires a complete oot3d_qdb_catalog_v1")
        for row in qdb_catalog.get("records", []):
            if row.get("status") != "decoded_native_qdb":
                continue
            source = str(row["asset_id"])
            source_archive = str(row["source_archive"])
            scene_stem = ""
            scene_id = -1
            if str(row.get("source_domain")) == "scene":
                scene_stem = Path(str(row["source_archive_relative"])).stem
                scene_id = scene_ids_by_stem.get(scene_stem, -1)
            commands = row.get("commands", [])
            records.append(_record(
                source, "cutscene_timeline", source, [str(row["canonical_resource"])],
                str(qdb_catalog_path),
                required_engine_capabilities=["native_qdb_source", "native_qdb_command_stream"],
                ownership={"scene_id": scene_id, "scene_stem": scene_stem, "setup_indices": []},
                metadata={
                    "source_domain": row.get("source_domain"),
                    "source_archive": source_archive,
                    "source_member": row.get("source_member"),
                    "embedded_index": row.get("embedded_index"),
                    "byte_length": row.get("byte_length"),
                    "sha256": row.get("sha256"),
                    "command_count": row.get("header", {}).get("command_count"),
                    "end_frame": row.get("header", {}).get("end_frame"),
                    "command_categories": sorted({
                        str(command.get("category")) for command in commands
                        if isinstance(command, dict) and command.get("category")
                    }),
                },
                runtime_state="packaged_not_bound",
            ))

    route = manifests["route"]
    route_id = str(route.get("route_id", "unknown"))
    for index, row in enumerate(route.get("resource_targets", [])):
        source_path = str(row.get("oot3d_source", "unknown"))
        kind = str(row.get("kind", "unknown"))
        source = f"route:{route_id}:{kind}:{source_path}:{index}"
        records.append(_record(
            source, "route_binding", source, [], MANIFESTS["route"],
            dependencies=[f"source:{source_path}"],
            metadata={key: value for key, value in row.items() if key != "fallback"},
            runtime_state="runtime_candidate",
        ))

    records.sort(key=lambda row: row["asset_id"])
    ids = [row["asset_id"] for row in records]
    duplicates = sorted(asset_id for asset_id, count in Counter(ids).items() if count > 1)
    dependency_ids = {row["asset_id"] for row in records}
    unresolved_dependencies = sorted({
        dependency
        for row in records
        for dependency in row["dependencies"]
        if not dependency.startswith("source:") and dependency not in dependency_ids
    })
    family_counts = dict(sorted(Counter(row["family"] for row in records).items()))
    resource_count = sum(len(row["canonical_resources"]) for row in records)
    manifest_inputs = []
    for key, path in sorted(paths.items()):
        manifest_inputs.append({
            "key": key,
            "path": str(path),
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "byte_length": path.stat().st_size,
        })
    for key, path in (("scene_index", scene_index_path), ("scene_resource_table", scene_resource_table_path)):
        if path is not None:
            manifest_inputs.append({"key": key, "path": str(path),
                                    "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                                    "byte_length": path.stat().st_size})
    for index, path in enumerate(scene_shard_manifest_paths or []):
        manifest_inputs.append({"key": f"scene_shard_{index}", "path": str(path),
                                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                                "byte_length": path.stat().st_size})
    if qdb_catalog_path is not None:
        manifest_inputs.append({"key": "qdb_catalog", "path": str(qdb_catalog_path),
                                "sha256": hashlib.sha256(qdb_catalog_path.read_bytes()).hexdigest(),
                                "byte_length": qdb_catalog_path.stat().st_size})
    if duplicates:
        status = "invalid_duplicate_asset_ids"
    elif unresolved_dependencies:
        status = "invalid_unresolved_dependencies"
    else:
        status = "complete"
    return {
        "format": FORMAT,
        "generated_utc": now.isoformat(),
        "work_root": str(work_root),
        "status": status,
        "manifest_inputs": manifest_inputs,
        "record_count": len(records),
        "family_counts": family_counts,
        "canonical_resource_reference_count": resource_count,
        "duplicate_asset_ids": duplicates,
        "unresolved_dependencies": unresolved_dependencies,
        "records": records,
        "notes": [
            "Asset ids are native OOT3D container/member identities, never N64 resource paths.",
            "Route bindings remain separate runtime-candidate records and do not redefine global asset identity.",
            "The V1 catalog is generated metadata; binary resource indexing is a later optimization.",
        ],
    }


def render_catalog_markdown(catalog: dict[str, Any]) -> str:
    lines = [
        "# OOT3D Global Asset Catalog",
        "",
        f"- Status: `{catalog['status']}`",
        f"- Records: `{catalog.get('record_count', 0)}`",
        f"- Canonical resource references: `{catalog.get('canonical_resource_reference_count', 0)}`",
        f"- Duplicate asset ids: `{len(catalog.get('duplicate_asset_ids', []))}`",
        f"- Unresolved dependencies: `{len(catalog.get('unresolved_dependencies', []))}`",
        "",
        "## Families",
        "",
        "| Family | Records |",
        "| --- | ---: |",
    ]
    for family, count in catalog.get("family_counts", {}).items():
        lines.append(f"| `{family}` | {count} |")
    lines.extend(["", "## Runtime Policy", "",
                  "- Catalog identities come from OOT3D source containers and members.",
                  "- Packaged records remain Tier 2 until a provider consumes them.",
                  "- Route records are Tier 3 selectors and remain distinct from asset records.",
                  "- N64 paths may appear only in fallback metadata, never as catalog identity.", ""])
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Generate the global native-identity OOT3D asset catalog.")
    parser.add_argument("--work-root", type=Path, required=True)
    parser.add_argument("--output-json", type=Path, required=True)
    parser.add_argument("--output-md", type=Path, required=True)
    parser.add_argument("--scene-index", type=Path)
    parser.add_argument("--scene-resource-table", type=Path)
    parser.add_argument("--scene-shard-manifest", type=Path, action="append", default=[])
    parser.add_argument("--qdb-catalog", type=Path)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args(argv)
    catalog = build_asset_catalog(args.work_root, scene_index_path=args.scene_index,
                                  scene_resource_table_path=args.scene_resource_table,
                                  scene_shard_manifest_paths=args.scene_shard_manifest,
                                  qdb_catalog_path=args.qdb_catalog)
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_md.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(catalog, indent=2) + "\n", encoding="utf-8", newline="\n")
    args.output_md.write_text(render_catalog_markdown(catalog), encoding="utf-8", newline="\n")
    print(args.output_json)
    print(args.output_md)
    if args.verify and catalog["status"] != "complete":
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
