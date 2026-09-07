from __future__ import annotations

from collections import Counter
import json
from pathlib import Path
import zipfile

from .binary import ParseError
from .romfs_inventory import sorted_counter


ROOT_ARCHIVE_MANIFEST = "manifest.json"
CSAB_TRACK_BATCH_ARCHIVE_MANIFEST = "oot3d_csab_track_batch_manifest.json"


def pack_csab_track_batch_manifest(
    batch_manifest_path: Path,
    output_path: Path,
    *,
    name: str = "OOT3D CSAB Track Candidates",
    author: str = "local",
    version: str = "0.1.0",
    archive_prefix: str = "animations/oot3d/csab",
) -> Path:
    if not batch_manifest_path.is_file():
        raise ParseError(f"{batch_manifest_path}: CSAB track batch manifest not found")
    if output_path.suffix.lower() != ".o2r":
        raise ValueError(f"{output_path}: output path must end in .o2r")

    batch_manifest = json.loads(batch_manifest_path.read_text(encoding="utf-8"))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    root_manifest = {
        "name": name,
        "author": author,
        "version": version,
        "description": "OOT3D CSAB skeleton track JSON candidates for offline Shipwright animation testing.",
        "license": "Generated from user-provided local assets; do not redistribute without rights.",
        "code_version": 1,
    }

    written_paths: set[str] = set()
    with zipfile.ZipFile(output_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr(ROOT_ARCHIVE_MANIFEST, json.dumps(root_manifest, indent=2) + "\n")
        archive.writestr(
            CSAB_TRACK_BATCH_ARCHIVE_MANIFEST,
            json.dumps(batch_manifest, indent=2) + "\n",
        )
        written_paths.update({ROOT_ARCHIVE_MANIFEST, CSAB_TRACK_BATCH_ARCHIVE_MANIFEST})

        for record in exported_track_records(batch_manifest):
            track_rel = normalize_resource_path(record["track_export"])
            archive_path = normalize_resource_path(f"{archive_prefix}/{track_rel}")
            if not archive_path:
                raise ValueError("exported CSAB track has an empty archive path")
            if archive_path in written_paths:
                continue

            source_path = resolve_generated_path(batch_manifest_path.parent, track_rel)
            if not source_path.is_file():
                raise ValueError(f"missing generated CSAB track for {archive_path}: {track_rel}")

            archive.write(source_path, archive_path)
            written_paths.add(archive_path)

    return output_path


def audit_csab_track_batch_package(
    batch_manifest_path: Path,
    archive_path: Path,
    output_path: Path | None = None,
    *,
    archive_prefix: str = "animations/oot3d/csab",
    sample_limit: int = 100,
) -> dict[str, object]:
    if not batch_manifest_path.is_file():
        raise ParseError(f"{batch_manifest_path}: CSAB track batch manifest not found")
    if not archive_path.is_file():
        raise ParseError(f"{archive_path}: O2R archive not found")

    batch_manifest = json.loads(batch_manifest_path.read_text(encoding="utf-8"))
    exported_records = exported_track_records(batch_manifest)
    expected_track_paths = [
        normalize_resource_path(f"{archive_prefix}/{normalize_resource_path(record['track_export'])}")
        for record in exported_records
    ]
    expected_path_counts = Counter(expected_track_paths)
    expected_path_set = set(expected_path_counts)

    missing_source_tracks: list[dict[str, str]] = []
    invalid_source_track_json: list[dict[str, str]] = []
    expected_format_counts: Counter[str] = Counter()
    for record, archive_track_path in zip(exported_records, expected_track_paths):
        source_path = resolve_generated_path(
            batch_manifest_path.parent,
            normalize_resource_path(record["track_export"]),
        )
        if not source_path.is_file():
            missing_source_tracks.append(
                {
                    "track_export": str(record["track_export"]),
                    "archive_path": archive_track_path,
                }
            )
            continue
        error, loaded = read_path_json_error(source_path)
        if error is not None:
            invalid_source_track_json.append(
                {
                    "track_export": str(record["track_export"]),
                    "archive_path": archive_track_path,
                    "error": error,
                }
            )
            continue
        if isinstance(loaded, dict):
            expected_format_counts[str(loaded.get("format", "<missing>"))] += 1

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
            has_batch_manifest = CSAB_TRACK_BATCH_ARCHIVE_MANIFEST in archive_name_set
            root_manifest_error = (
                read_archive_json_error(archive, ROOT_ARCHIVE_MANIFEST)
                if has_root_manifest
                else None
            )
            batch_manifest_error = (
                read_archive_json_error(archive, CSAB_TRACK_BATCH_ARCHIVE_MANIFEST)
                if has_batch_manifest
                else None
            )
            archived_batch_manifest_matches = False
            if has_batch_manifest and batch_manifest_error is None:
                archived_batch_manifest = json.loads(
                    archive.read(CSAB_TRACK_BATCH_ARCHIVE_MANIFEST).decode("utf-8")
                )
                archived_batch_manifest_matches = archived_batch_manifest == batch_manifest

            missing_track_entries = sorted(path for path in expected_path_set if path not in archive_name_set)
            extra_entries = sorted(
                path
                for path in archive_name_set
                if path not in expected_path_set
                and path not in {ROOT_ARCHIVE_MANIFEST, CSAB_TRACK_BATCH_ARCHIVE_MANIFEST}
            )
            duplicate_archive_entries = sorted(
                (
                    {"path": path, "count": count}
                    for path, count in archive_name_counts.items()
                    if count > 1
                ),
                key=lambda item: str(item["path"]),
            )
            duplicate_expected_tracks = sorted(
                (
                    {"path": path, "count": count}
                    for path, count in expected_path_counts.items()
                    if count > 1
                ),
                key=lambda item: str(item["path"]),
            )
            invalid_archived_track_json, archive_format_counts = audit_archived_tracks(
                archive,
                expected_path_set,
                archive_name_set,
            )
    except zipfile.BadZipFile as exc:
        raise ParseError(f"{archive_path}: invalid O2R/zip archive: {exc}") from exc

    track_archive_paths = expected_path_set & archive_name_set
    issue_counts = {
        "missing_manifest": 0 if has_root_manifest else 1,
        "invalid_manifest_json": 1 if root_manifest_error else 0,
        "missing_csab_track_batch_manifest": 0 if has_batch_manifest else 1,
        "invalid_csab_track_batch_manifest_json": 1 if batch_manifest_error else 0,
        "csab_track_batch_manifest_mismatch": (
            0 if not has_batch_manifest or batch_manifest_error or archived_batch_manifest_matches else 1
        ),
        "missing_source_track_file": len(missing_source_tracks),
        "invalid_source_track_json": len(invalid_source_track_json),
        "missing_track_entry": len(missing_track_entries),
        "extra_archive_entry": len(extra_entries),
        "duplicate_archive_entry": sum(int(entry["count"]) - 1 for entry in duplicate_archive_entries),
        "duplicate_expected_track_path": sum(int(entry["count"]) - 1 for entry in duplicate_expected_tracks),
        "invalid_archived_track_json": len(invalid_archived_track_json),
    }
    issue_counts["total"] = sum(issue_counts.values())

    audit: dict[str, object] = {
        "format": "oot3d_csab_track_batch_package_audit_v1",
        "batch_manifest": str(batch_manifest_path),
        "archive": str(archive_path),
        "archive_prefix": normalize_resource_path(archive_prefix),
        "archive_entry_count": len(archive_names),
        "has_manifest": has_root_manifest,
        "has_csab_track_batch_manifest": has_batch_manifest,
        "archived_csab_track_batch_manifest_matches": archived_batch_manifest_matches,
        "exported_record_count": len(exported_records),
        "expected_track_count": len(expected_track_paths),
        "expected_unique_track_count": len(expected_path_set),
        "track_entry_count": len(track_archive_paths),
        "missing_source_track_file_count": len(missing_source_tracks),
        "invalid_source_track_json_count": len(invalid_source_track_json),
        "missing_track_entry_count": len(missing_track_entries),
        "extra_archive_entry_count": len(extra_entries),
        "duplicate_archive_entry_count": len(duplicate_archive_entries),
        "duplicate_expected_track_path_count": len(duplicate_expected_tracks),
        "invalid_archived_track_json_count": len(invalid_archived_track_json),
        "expected_track_format_counts": sorted_counter(expected_format_counts),
        "archived_track_format_counts": sorted_counter(archive_format_counts),
        "issue_counts": issue_counts,
        "root_manifest_error": root_manifest_error,
        "csab_track_batch_manifest_error": batch_manifest_error,
        "sample_missing_source_tracks": missing_source_tracks[:sample_limit],
        "sample_invalid_source_track_json": invalid_source_track_json[:sample_limit],
        "sample_missing_track_entries": missing_track_entries[:sample_limit],
        "sample_extra_archive_entries": extra_entries[:sample_limit],
        "sample_duplicate_archive_entries": duplicate_archive_entries[:sample_limit],
        "sample_duplicate_expected_tracks": duplicate_expected_tracks[:sample_limit],
        "sample_invalid_archived_track_json": invalid_archived_track_json[:sample_limit],
        "fallback_policy": [
            "This package is a testable animation-track candidate artifact only.",
            "It does not route Shipwright actor animation playback to OOT3D CSAB tracks by itself.",
            "N64 actor animations remain fallback until explicit runtime binding is implemented.",
        ],
    }
    if output_path is not None:
        write_json(output_path, audit)
    return audit


def exported_track_records(batch_manifest: dict[str, object]) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    for record in batch_manifest.get("records", []):
        if not isinstance(record, dict):
            continue
        if record.get("status") != "exported":
            continue
        if "track_export" not in record:
            continue
        records.append(record)
    return records


def resolve_generated_path(manifest_dir: Path, value: object) -> Path:
    path = Path(str(value))
    if path.exists() or path.is_absolute():
        return path
    return manifest_dir / path


def normalize_resource_path(value: object) -> str:
    return str(value).replace("\\", "/").strip("/")


def read_archive_json_error(archive: zipfile.ZipFile, archive_path: str) -> str | None:
    try:
        json.loads(archive.read(archive_path).decode("utf-8"))
    except (KeyError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        return str(exc)
    return None


def read_path_json_error(path: Path) -> tuple[str | None, object | None]:
    try:
        return None, json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        return str(exc), None


def audit_archived_tracks(
    archive: zipfile.ZipFile,
    expected_paths: set[str],
    archive_name_set: set[str],
) -> tuple[list[dict[str, str]], Counter[str]]:
    invalid_entries: list[dict[str, str]] = []
    format_counts: Counter[str] = Counter()
    for archive_path in sorted(expected_paths & archive_name_set):
        try:
            loaded = json.loads(archive.read(archive_path).decode("utf-8"))
        except (KeyError, UnicodeDecodeError, json.JSONDecodeError) as exc:
            invalid_entries.append({"path": archive_path, "error": str(exc)})
            continue
        if isinstance(loaded, dict):
            format_counts[str(loaded.get("format", "<missing>"))] += 1
    return invalid_entries, format_counts


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8", newline="\n")
