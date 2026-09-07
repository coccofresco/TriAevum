from __future__ import annotations

import json
import zipfile
from collections import Counter
from pathlib import Path
from xml.etree import ElementTree

from .binary import ParseError
from .romfs_inventory import sorted_counter


ROOT_ARCHIVE_MANIFEST = "manifest.json"
STATIC_BATCH_ARCHIVE_MANIFEST = "oot3d_static_batch_manifest.json"


def pack_static_batch_manifest(
    batch_manifest_path: Path,
    output_path: Path,
    *,
    name: str = "OOT3D Static Batch",
    author: str = "local",
    version: str = "0.1.0",
) -> Path:
    if not batch_manifest_path.is_file():
        raise ParseError(f"{batch_manifest_path}: static batch manifest not found")
    if output_path.suffix.lower() != ".o2r":
        raise ValueError(f"{output_path}: output path must end in .o2r")

    batch_manifest = json.loads(batch_manifest_path.read_text(encoding="utf-8"))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    root_manifest = {
        "name": name,
        "author": author,
        "version": version,
        "description": "OOT3D static/rigid batch resources for offline Shipwright asset testing.",
        "license": "Generated from user-provided local assets; do not redistribute without rights.",
        "code_version": 1,
    }

    written_paths: set[str] = set()
    with zipfile.ZipFile(output_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr(ROOT_ARCHIVE_MANIFEST, json.dumps(root_manifest, indent=2) + "\n")
        archive.writestr(STATIC_BATCH_ARCHIVE_MANIFEST, json.dumps(batch_manifest, indent=2) + "\n")
        written_paths.update({ROOT_ARCHIVE_MANIFEST, STATIC_BATCH_ARCHIVE_MANIFEST})

        for resource in converted_batch_resources(batch_manifest):
            archive_path = normalize_resource_path(resource["path"])
            if not archive_path:
                raise ValueError("converted batch resource has an empty archive path")
            if archive_path in written_paths:
                continue

            source_path = resolve_generated_resource(batch_manifest_path.parent, resource["file"])
            if not source_path.is_file():
                raise ValueError(f"missing generated resource for {archive_path}: {resource['file']}")

            archive.write(source_path, archive_path)
            written_paths.add(archive_path)

    return output_path


def audit_static_batch_package(
    batch_manifest_path: Path,
    archive_path: Path,
    output_path: Path | None = None,
    *,
    sample_limit: int = 100,
) -> dict[str, object]:
    if not batch_manifest_path.is_file():
        raise ParseError(f"{batch_manifest_path}: static batch manifest not found")
    if not archive_path.is_file():
        raise ParseError(f"{archive_path}: O2R archive not found")

    batch_manifest = json.loads(batch_manifest_path.read_text(encoding="utf-8"))
    converted_records = [
        record
        for record in batch_manifest.get("records", [])
        if isinstance(record, dict) and record.get("status") == "converted"
    ]
    expected_resources = converted_batch_resources(batch_manifest)
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
            has_batch_manifest = STATIC_BATCH_ARCHIVE_MANIFEST in archive_name_set
            root_manifest_error = read_json_error(archive, ROOT_ARCHIVE_MANIFEST) if has_root_manifest else None
            batch_manifest_error = (
                read_json_error(archive, STATIC_BATCH_ARCHIVE_MANIFEST)
                if has_batch_manifest
                else None
            )
            archived_batch_manifest_matches = False
            if has_batch_manifest and batch_manifest_error is None:
                archived_batch_manifest = json.loads(
                    archive.read(STATIC_BATCH_ARCHIVE_MANIFEST).decode("utf-8")
                )
                archived_batch_manifest_matches = archived_batch_manifest == batch_manifest

            missing_resource_entries = sorted(path for path in expected_paths if path not in archive_name_set)
            extra_resource_entries = sorted(
                path
                for path in archive_name_set
                if path not in expected_paths
                and path not in {ROOT_ARCHIVE_MANIFEST, STATIC_BATCH_ARCHIVE_MANIFEST}
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
        "missing_static_batch_manifest": 0 if has_batch_manifest else 1,
        "invalid_static_batch_manifest_json": 1 if batch_manifest_error else 0,
        "static_batch_manifest_mismatch": (
            0 if not has_batch_manifest or batch_manifest_error or archived_batch_manifest_matches else 1
        ),
        "missing_resource_entry": len(missing_resource_entries),
        "extra_resource_entry": len(extra_resource_entries),
        "duplicate_archive_entry": sum(int(entry["count"]) - 1 for entry in duplicate_archive_entries),
        "duplicate_expected_resource_path": sum(int(entry["count"]) - 1 for entry in duplicate_expected_resources),
        "invalid_resource_xml": len(invalid_xml_entries),
    }
    issue_counts["total"] = sum(issue_counts.values())

    audit: dict[str, object] = {
        "format": "oot3d_static_batch_package_audit_v1",
        "batch_manifest": str(batch_manifest_path),
        "archive": str(archive_path),
        "archive_entry_count": len(archive_names),
        "has_manifest": has_root_manifest,
        "has_static_batch_manifest": has_batch_manifest,
        "archived_static_batch_manifest_matches": archived_batch_manifest_matches,
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
        "static_batch_manifest_error": batch_manifest_error,
        "sample_missing_resource_entries": missing_resource_entries[:sample_limit],
        "sample_extra_resource_entries": extra_resource_entries[:sample_limit],
        "sample_duplicate_archive_entries": duplicate_archive_entries[:sample_limit],
        "sample_duplicate_expected_resources": duplicate_expected_resources[:sample_limit],
        "sample_invalid_resource_xml": invalid_xml_entries[:sample_limit],
        "fallback_policy": [
            "This package is a testable resource artifact only.",
            "It does not route Shipwright actors or objects to OOT3D replacements by itself.",
            "N64 actor/object assets remain fallback until explicit runtime routing is implemented.",
        ],
    }
    if output_path is not None:
        write_json(output_path, audit)
    return audit


def converted_batch_resources(batch_manifest: dict[str, object]) -> list[dict[str, object]]:
    resources: list[dict[str, object]] = []
    for record in batch_manifest.get("records", []):
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
        if kind == "Texture":
            continue
        archive_path = normalize_resource_path(resource.get("path"))
        if not archive_path or archive_path in seen or archive_path not in archive_name_set:
            continue
        seen.add(archive_path)
        try:
            ElementTree.fromstring(archive.read(archive_path))
        except (ElementTree.ParseError, KeyError, UnicodeDecodeError) as exc:
            invalid_entries.append({"path": archive_path, "error": str(exc)})
    return invalid_entries


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8", newline="\n")
