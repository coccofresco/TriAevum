from __future__ import annotations

import json
import zipfile
from collections import Counter, defaultdict
from pathlib import Path
import xml.etree.ElementTree as ET

from .binary import ParseError
from .prerendered_room_audit import (
    audit_prerendered_room_replacements,
    discover_room_files,
)
from .romfs_inventory import sorted_counter
from .legacy_fast_resource import LegacyFastResourceOptions, export_static_model
from .static_batch_audit import audit_static_batch
from .zsi import ZsiFile


PRERENDERED_ROOM_ARCHIVE_MANIFEST = "oot3d_prerendered_room_export_manifest.json"
ROOT_ARCHIVE_MANIFEST = "manifest.json"


def export_prerendered_room_replacements(
    asset_xml_root: Path,
    scene_root: Path,
    output_dir: Path,
    *,
    resource_prefix: str = "scenes/prerendered_room_replacements/oot3d",
    include_textures: bool = True,
    sample_limit: int = 100,
) -> dict[str, object]:
    replacement_audit = audit_prerendered_room_replacements(
        asset_xml_root,
        scene_root,
        sample_limit=sample_limit,
        include_records=True,
    )
    output_dir.mkdir(parents=True, exist_ok=True)
    resource_output = output_dir / "resources"

    associations_by_stem: dict[str, list[dict[str, object]]] = defaultdict(list)
    for record in replacement_audit["records"]:
        if record.get("status") != "ready_for_pilot_export":
            continue
        alias = record.get("alias")
        oot3d_candidate = record.get("oot3d_candidate")
        if not isinstance(alias, dict) or not isinstance(oot3d_candidate, dict):
            continue
        stem = str(alias["oot3d_scene_stem"])
        associations_by_stem[stem].append(
            {
                "background_file": record["file_name"],
                "background_label": record["family_label"],
                "n64_scene_stem": alias["n64_scene_stem"],
                "n64_scene_enum": alias["n64_scene_enum"],
                "shipwright_scene_stem": alias["n64_scene_stem"],
                "oot3d_scene_stem": stem,
            }
        )

    records: list[dict[str, object]] = []
    scene_records: list[dict[str, object]] = []
    resource_kind_counts: Counter[str] = Counter()
    considered = 0
    converted = 0
    skipped = 0
    failed = 0
    parse_failed = 0
    room_file_count = 0

    for stem in sorted(associations_by_stem):
        room_files = discover_room_files(scene_root, stem)
        scene_record = {
            "oot3d_scene_stem": stem,
            "room_file_count": len(room_files),
            "associations": sorted(
                associations_by_stem[stem],
                key=lambda item: (str(item["n64_scene_stem"]), str(item["background_file"])),
            ),
            "converted": 0,
            "skipped": 0,
            "failed": 0,
            "parse_failed": 0,
        }
        scene_records.append(scene_record)
        room_file_count += len(room_files)

        for room_index, room_path in room_files:
            try:
                zsi = ZsiFile.from_path(room_path)
                cmbs = zsi.embedded_cmbs()
            except (OSError, ParseError) as exc:
                parse_failed += 1
                scene_record["parse_failed"] = int(scene_record["parse_failed"]) + 1
                records.append(
                    {
                        "status": "parse_failed",
                        "asset_id": f"{stem}_room_{room_index}",
                        "oot3d_scene_stem": stem,
                        "room": room_index,
                        "source": room_path.as_posix(),
                        "reason": str(exc),
                        "associations": scene_record["associations"],
                    }
                )
                continue

            if not cmbs:
                failed += 1
                scene_record["failed"] = int(scene_record["failed"]) + 1
                records.append(
                    {
                        "status": "failed",
                        "asset_id": f"{stem}_room_{room_index}",
                        "oot3d_scene_stem": stem,
                        "room": room_index,
                        "source": room_path.as_posix(),
                        "reason": "room_zsi_contains_no_cmb",
                        "associations": scene_record["associations"],
                    }
                )
                continue

            for cmb in cmbs:
                considered += 1
                asset_id = f"{stem}_room_{room_index}" if len(cmbs) == 1 else f"{stem}_room_{room_index}_cmb_{cmb.index}"
                resource_root = f"{normalize_resource_prefix(resource_prefix)}/{asset_id}"
                symbol = f"gOot3dPrerendered{to_pascal_case(stem)}Room{room_index}"
                if len(cmbs) != 1:
                    symbol += f"Cmb{cmb.index}"
                record_base = {
                    "asset_id": asset_id,
                    "oot3d_scene_stem": stem,
                    "room": room_index,
                    "cmb_index": cmb.index,
                    "source": cmb.model.source,
                    "resource_root": resource_root,
                    "symbol": symbol,
                    "associations": scene_record["associations"],
                    "summary": cmb.model.summary(),
                }

                if not cmb.model.is_rigid_export_candidate():
                    skipped += 1
                    scene_record["skipped"] = int(scene_record["skipped"]) + 1
                    records.append(
                        {
                            "status": "skipped",
                            "reason": "not_rigid_export_candidate",
                            **record_base,
                        }
                    )
                    continue

                try:
                    result = export_static_model(
                        cmb.model,
                        LegacyFastResourceOptions(
                            output_dir=resource_output / asset_id,
                            resource_root=resource_root,
                            symbol=symbol,
                            include_textures=include_textures,
                            allow_nonstatic=False,
                        ),
                    )
                except Exception as exc:
                    failed += 1
                    scene_record["failed"] = int(scene_record["failed"]) + 1
                    records.append(
                        {
                            "status": "failed",
                            "reason": str(exc),
                            **record_base,
                        }
                    )
                    continue

                converted += 1
                scene_record["converted"] = int(scene_record["converted"]) + 1
                for resource in result.resources:
                    resource_kind_counts[resource.kind] += 1
                records.append(
                    {
                        "status": "converted",
                        "manifest": str(result.manifest_path),
                        "resource_count": len(result.resources),
                        "resources": [resource.__dict__ for resource in result.resources],
                        **record_base,
                    }
                )

    manifest_path = output_dir / "prerendered_room_export_manifest.json"
    resource_audit_path = output_dir / "prerendered_room_export_resource_audit.json"
    manifest: dict[str, object] = {
        "format": "oot3d_prerendered_room_export_manifest_v1",
        "asset_xml_root": str(asset_xml_root),
        "scene_root": str(scene_root),
        "output": str(output_dir),
        "resource_prefix": normalize_resource_prefix(resource_prefix),
        "include_textures": include_textures,
        "replacement_background_family_count": replacement_audit["background_family_count"],
        "replacement_ready_family_count": replacement_audit["ready_for_pilot_export_family_count"],
        "unique_oot3d_scene_count": len(associations_by_stem),
        "room_file_count": room_file_count,
        "considered": considered,
        "converted": converted,
        "skipped": skipped,
        "failed": failed,
        "parse_failed": parse_failed,
        "resource_kind_counts": sorted_counter(resource_kind_counts),
        "scene_records": scene_records,
        "records": records,
        "resource_audit": str(resource_audit_path),
        "fallback_policy": [
            "Generated resources are export-readiness artifacts only.",
            "N64/Shipwright prerendered-background rooms remain fallback until explicit runtime routing is implemented.",
            "Collision replacement is not generated by this batch.",
        ],
    }
    write_json(manifest_path, manifest)

    resource_audit = audit_static_batch(manifest_path, resource_audit_path)
    manifest["resource_audit_summary"] = {
        "converted_record_count": resource_audit["converted_record_count"],
        "resource_count": resource_audit["resource_count"],
        "material_display_list_count": resource_audit["material_display_list_count"],
        "mesh_display_list_count": resource_audit["mesh_display_list_count"],
        "vertex_resource_count": resource_audit["vertex_resource_count"],
        "set_texture_image_count": resource_audit["set_texture_image_count"],
        "load_texture_block_count": resource_audit["load_texture_block_count"],
        "negative_st_count": resource_audit["negative_st_count"],
        "min_st": resource_audit["min_st"],
        "max_st": resource_audit["max_st"],
        "issue_counts": resource_audit["issue_counts"],
    }
    write_json(manifest_path, manifest)
    return manifest


def pack_prerendered_room_export_manifest(
    export_manifest_path: Path,
    output_path: Path,
    *,
    name: str,
    author: str,
    version: str,
) -> Path:
    if output_path.suffix.lower() != ".o2r":
        raise ValueError(f"{output_path}: output path must end in .o2r")
    if not export_manifest_path.is_file():
        raise ParseError(f"{export_manifest_path}: prerendered room export manifest not found")

    export_manifest = json.loads(export_manifest_path.read_text(encoding="utf-8"))
    output_path.parent.mkdir(parents=True, exist_ok=True)

    root_manifest = {
        "name": name,
        "author": author,
        "version": version,
        "description": "OOT3D full-3D room candidate resources for Shipwright prerendered-background replacement testing.",
        "license": "Generated from user-provided local assets; do not redistribute without rights.",
        "code_version": 1,
    }

    written_paths: set[str] = set()
    with zipfile.ZipFile(output_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr(ROOT_ARCHIVE_MANIFEST, json.dumps(root_manifest, indent=2) + "\n")
        archive.writestr(PRERENDERED_ROOM_ARCHIVE_MANIFEST, json.dumps(export_manifest, indent=2) + "\n")
        written_paths.update({ROOT_ARCHIVE_MANIFEST, PRERENDERED_ROOM_ARCHIVE_MANIFEST})

        for resource in converted_export_resources(export_manifest):
            archive_path = normalize_resource_path(resource["path"])
            if not archive_path:
                raise ValueError("converted export resource has an empty archive path")
            if archive_path in written_paths:
                continue

            source_path = resolve_generated_resource(export_manifest_path.parent, resource["file"])
            if not source_path.is_file():
                raise ValueError(f"missing generated resource for {archive_path}: {resource['file']}")

            archive.write(source_path, archive_path)
            written_paths.add(archive_path)

    return output_path


def audit_prerendered_room_export_package(
    export_manifest_path: Path,
    archive_path: Path,
    output_path: Path | None = None,
    *,
    sample_limit: int = 100,
) -> dict[str, object]:
    if not export_manifest_path.is_file():
        raise ParseError(f"{export_manifest_path}: prerendered room export manifest not found")
    if not archive_path.is_file():
        raise ParseError(f"{archive_path}: O2R archive not found")

    export_manifest = json.loads(export_manifest_path.read_text(encoding="utf-8"))
    converted_records = [
        record
        for record in export_manifest.get("records", [])
        if isinstance(record, dict) and record.get("status") == "converted"
    ]
    expected_resources = converted_export_resources(export_manifest)
    expected_path_counts = Counter(normalize_resource_path(resource["path"]) for resource in expected_resources)
    expected_paths = set(expected_path_counts)
    kind_by_path = {
        normalize_resource_path(resource["path"]): str(resource.get("kind"))
        for resource in expected_resources
    }
    expected_kind_counts = Counter(str(resource.get("kind")) for resource in expected_resources)

    try:
        with zipfile.ZipFile(archive_path, "r") as archive:
            archive_names = [
                normalize_resource_path(info.filename)
                for info in archive.infolist()
                if not info.is_dir()
            ]
            archive_name_counts = Counter(archive_names)
            archive_name_set = set(archive_names)
            has_root_manifest = ROOT_ARCHIVE_MANIFEST in archive_name_set
            has_export_manifest = PRERENDERED_ROOM_ARCHIVE_MANIFEST in archive_name_set
            root_manifest_error = read_json_error(archive, ROOT_ARCHIVE_MANIFEST) if has_root_manifest else None
            export_manifest_error = (
                read_json_error(archive, PRERENDERED_ROOM_ARCHIVE_MANIFEST)
                if has_export_manifest
                else None
            )
            archived_export_manifest_matches = False
            if has_export_manifest and export_manifest_error is None:
                archived_export_manifest = json.loads(
                    archive.read(PRERENDERED_ROOM_ARCHIVE_MANIFEST).decode("utf-8")
                )
                archived_export_manifest_matches = archived_export_manifest == export_manifest

            missing_resource_entries = sorted(path for path in expected_paths if path not in archive_name_set)
            extra_resource_entries = sorted(
                path
                for path in archive_name_set
                if path not in expected_paths
                and path not in {ROOT_ARCHIVE_MANIFEST, PRERENDERED_ROOM_ARCHIVE_MANIFEST}
            )
            duplicate_archive_entries = [
                {"path": path, "count": count}
                for path, count in archive_name_counts.items()
                if count > 1
            ]
            duplicate_archive_entries.sort(key=lambda item: str(item["path"]))
            duplicate_expected_resources = [
                {"path": path, "count": count}
                for path, count in expected_path_counts.items()
                if count > 1
            ]
            duplicate_expected_resources.sort(key=lambda item: str(item["path"]))
            invalid_xml_entries = audit_archived_resource_xml(
                archive,
                expected_resources,
                archive_name_set,
            )
    except zipfile.BadZipFile as exc:
        raise ParseError(f"{archive_path}: invalid O2R/zip archive: {exc}") from exc

    resource_archive_paths = expected_paths & archive_name_set
    archive_kind_counts = Counter(kind_by_path[path] for path in resource_archive_paths)
    issue_counts = {
        "missing_manifest": 0 if has_root_manifest else 1,
        "invalid_manifest_json": 1 if root_manifest_error else 0,
        "missing_prerendered_room_export_manifest": 0 if has_export_manifest else 1,
        "invalid_prerendered_room_export_manifest_json": 1 if export_manifest_error else 0,
        "prerendered_room_export_manifest_mismatch": (
            0 if not has_export_manifest or export_manifest_error or archived_export_manifest_matches else 1
        ),
        "missing_resource_entry": len(missing_resource_entries),
        "extra_resource_entry": len(extra_resource_entries),
        "duplicate_archive_entry": sum(int(entry["count"]) - 1 for entry in duplicate_archive_entries),
        "duplicate_expected_resource_path": sum(int(entry["count"]) - 1 for entry in duplicate_expected_resources),
        "invalid_resource_xml": len(invalid_xml_entries),
    }
    issue_counts["total"] = sum(issue_counts.values())

    audit: dict[str, object] = {
        "format": "oot3d_prerendered_room_export_package_audit_v1",
        "export_manifest": str(export_manifest_path),
        "archive": str(archive_path),
        "archive_entry_count": len(archive_names),
        "has_manifest": has_root_manifest,
        "has_prerendered_room_export_manifest": has_export_manifest,
        "archived_export_manifest_matches": archived_export_manifest_matches,
        "converted_record_count": len(converted_records),
        "expected_resource_count": len(expected_resources),
        "expected_unique_resource_count": len(expected_paths),
        "resource_entry_count": len(resource_archive_paths),
        "missing_resource_entry_count": len(missing_resource_entries),
        "extra_resource_entry_count": len(extra_resource_entries),
        "duplicate_archive_entry_count": len(duplicate_archive_entries),
        "duplicate_expected_resource_path_count": len(duplicate_expected_resources),
        "invalid_resource_xml_count": len(invalid_xml_entries),
        "resource_kind_counts_expected": sorted_counter(expected_kind_counts),
        "resource_kind_counts_archive": sorted_counter(archive_kind_counts),
        "issue_counts": issue_counts,
        "root_manifest_error": root_manifest_error,
        "prerendered_room_export_manifest_error": export_manifest_error,
        "sample_missing_resource_entries": missing_resource_entries[:sample_limit],
        "sample_extra_resource_entries": extra_resource_entries[:sample_limit],
        "sample_duplicate_archive_entries": duplicate_archive_entries[:sample_limit],
        "sample_duplicate_expected_resources": duplicate_expected_resources[:sample_limit],
        "sample_invalid_resource_xml": invalid_xml_entries[:sample_limit],
        "fallback_policy": [
            "This package is a testable resource artifact only.",
            "It does not route Shipwright rooms to OOT3D replacements by itself.",
            "The original N64 prerendered-background rooms remain fallback until per-room runtime routing is implemented.",
        ],
    }
    if output_path is not None:
        write_json(output_path, audit)
    return audit


def converted_export_resources(export_manifest: dict[str, object]) -> list[dict[str, object]]:
    resources: list[dict[str, object]] = []
    for record in export_manifest.get("records", []):
        if not isinstance(record, dict) or record.get("status") != "converted":
            continue
        for resource in record.get("resources", []):
            if isinstance(resource, dict):
                resources.append(resource)
    return resources


def resolve_generated_resource(manifest_dir: Path, value: object) -> Path:
    path = Path(str(value))
    if path.exists() or path.is_absolute():
        return path
    return manifest_dir / path


def normalize_resource_path(value: object) -> str:
    return str(value).replace("\\", "/").strip("/")


def read_json_error(archive: zipfile.ZipFile, archive_path: str) -> str | None:
    try:
        json.loads(archive.read(archive_path).decode("utf-8"))
    except (KeyError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        return str(exc)
    return None


def audit_archived_resource_xml(
    archive: zipfile.ZipFile,
    expected_resources: list[dict[str, object]],
    archive_name_set: set[str],
) -> list[dict[str, str]]:
    invalid_entries: list[dict[str, str]] = []
    seen: set[str] = set()
    for resource in expected_resources:
        kind = str(resource.get("kind"))
        if kind not in {"DisplayList", "Vertex"}:
            continue
        archive_path = normalize_resource_path(resource["path"])
        if archive_path in seen or archive_path not in archive_name_set:
            continue
        seen.add(archive_path)
        try:
            ET.fromstring(archive.read(archive_path).decode("utf-8"))
        except (KeyError, UnicodeDecodeError, ET.ParseError) as exc:
            invalid_entries.append({"path": archive_path, "error": str(exc)})
    return invalid_entries


def write_json(path: Path, value: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8", newline="\n")


def normalize_resource_prefix(value: str) -> str:
    return value.replace("\\", "/").strip("/")


def to_pascal_case(value: str) -> str:
    parts = [part for part in sanitize_path_part(value).split("_") if part]
    return "".join(part[:1].upper() + part[1:] for part in parts) or "Model"


def sanitize_path_part(value: str) -> str:
    chars = []
    for char in value:
        if char.isalnum():
            chars.append(char)
        else:
            chars.append("_")
    return "_".join(part for part in "".join(chars).split("_") if part).lower()
