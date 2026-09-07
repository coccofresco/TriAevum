from __future__ import annotations

import json
from collections import Counter
from pathlib import Path

from .actor_inventory import (
    cmb_candidate_stems,
    csab_header_candidates,
    normalized_asset_stem,
    sorted_counter,
)
from .binary import ParseError
from .csab_tracks import is_csab_file, parsed_archive_cmb_records, resolve_csab_target
from .zar import ZarArchive


def audit_csab_target_resolution(
    actor_root: Path,
    output_path: Path | None = None,
    *,
    include_resolved_records: bool = False,
) -> dict[str, object]:
    if not actor_root.is_dir():
        raise ParseError(f"{actor_root}: expected an extracted OOT3D actor directory")

    considered_csab = 0
    archive_parse_errors: list[dict[str, object]] = []
    target_resolution_counts: Counter[str] = Counter()
    target_support_counts: Counter[str] = Counter()
    unresolved_archive_counts: Counter[str] = Counter()
    unresolved_same_bone_candidate_count_counts: Counter[str] = Counter()
    unresolved_namespace_candidate_count_counts: Counter[str] = Counter()
    unresolved_same_bone_support_counts: Counter[str] = Counter()
    unresolved_records: list[dict[str, object]] = []
    resolved_records: list[dict[str, object]] = []

    for zar_path in sorted(actor_root.rglob("*.zar")):
        archive_rel = zar_path.relative_to(actor_root).as_posix()
        try:
            archive = ZarArchive.from_path(zar_path)
            cmb_records, _models_by_name = parsed_archive_cmb_records(archive, zar_path)
        except Exception as exc:
            archive_parse_errors.append(
                {
                    "archive_path": archive_rel,
                    "reason": str(exc),
                }
            )
            continue

        for csab_file in archive.files:
            if not is_csab_file(csab_file):
                continue
            considered_csab += 1
            csab_data = archive.read_file(csab_file)
            header = csab_header_candidates(csab_data)
            target_status, target_candidates = resolve_csab_target(
                archive,
                csab_file,
                cmb_records,
            )
            target_resolution_counts[target_status] += 1

            if len(target_candidates) == 1:
                support_status = str(target_candidates[0]["support_status"])
                target_support_counts[support_status] += 1
                if include_resolved_records:
                    resolved_records.append(
                        {
                            "archive_path": archive_rel,
                            "csab_name": csab_file.name,
                            "target_resolution_status": target_status,
                            "target_cmb": candidate_record(target_candidates[0]),
                        }
                    )
                continue

            record = unresolved_target_record(
                archive_rel,
                csab_file.name,
                header,
                target_status,
                cmb_records,
            )
            unresolved_records.append(record)
            unresolved_archive_counts[archive_rel] += 1
            unresolved_same_bone_candidate_count_counts[
                str(record["same_bone_candidate_count"])
            ] += 1
            unresolved_namespace_candidate_count_counts[
                str(record["namespace_candidate_count"])
            ] += 1
            for candidate in record["same_bone_candidates"]:
                if isinstance(candidate, dict):
                    unresolved_same_bone_support_counts[str(candidate["support_status"])] += 1

    resolved_count = sum(
        count
        for status, count in target_resolution_counts.items()
        if status
        in {
            "single_bone_count_match",
            "multiple_bone_count_exact_name_match",
            "multiple_bone_count_contained_name_match",
        }
    )
    unresolved_or_missing = len(unresolved_records)
    audit: dict[str, object] = {
        "format": "oot3d_csab_target_resolution_audit_v1",
        "actor_root": str(actor_root),
        "considered_csab": considered_csab,
        "resolved_count": resolved_count,
        "unresolved_or_missing": unresolved_or_missing,
        "archive_parse_error_count": len(archive_parse_errors),
        "target_resolution_counts": sorted_counter(target_resolution_counts),
        "target_support_counts": sorted_counter(target_support_counts),
        "unresolved_archive_counts": sorted_counter(unresolved_archive_counts),
        "unresolved_same_bone_candidate_count_counts": sorted_counter(
            unresolved_same_bone_candidate_count_counts
        ),
        "unresolved_namespace_candidate_count_counts": sorted_counter(
            unresolved_namespace_candidate_count_counts
        ),
        "unresolved_same_bone_support_counts": sorted_counter(
            unresolved_same_bone_support_counts
        ),
        "archive_parse_errors": archive_parse_errors,
        "unresolved_records": unresolved_records,
    }
    if include_resolved_records:
        audit["resolved_records"] = resolved_records

    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(
            json.dumps(audit, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    return audit


def unresolved_target_record(
    archive_path: str,
    csab_name: str,
    header: dict[str, int],
    target_status: str,
    cmb_records: list[dict[str, object]],
) -> dict[str, object]:
    skeleton_bone_count = header.get("skeleton_bone_count_candidate")
    same_bone_candidates = [
        record for record in cmb_records if record["bone_count"] == skeleton_bone_count
    ]
    namespace = animation_namespace(csab_name)
    namespace_candidates = [
        record
        for record in cmb_records
        if namespace is not None and cmb_namespace(record) == namespace
    ]
    same_bone_namespace_candidates = [
        record
        for record in same_bone_candidates
        if namespace is not None and cmb_namespace(record) == namespace
    ]
    animation_stem = normalized_asset_stem(csab_name)
    stem_related_candidates = [
        record
        for record in cmb_records
        if stem_related(animation_stem, record)
    ]

    return {
        "archive_path": archive_path,
        "csab_name": csab_name,
        "animation_stem": animation_stem,
        "animation_namespace": namespace,
        "target_resolution_status": target_status,
        "frame_count_candidate": header.get("frame_count_candidate"),
        "animated_bone_count_candidate": header.get("animated_bone_count_candidate"),
        "skeleton_bone_count_candidate": skeleton_bone_count,
        "cmb_count": len(cmb_records),
        "same_bone_candidate_count": len(same_bone_candidates),
        "namespace_candidate_count": len(namespace_candidates),
        "same_bone_namespace_candidate_count": len(same_bone_namespace_candidates),
        "stem_related_candidate_count": len(stem_related_candidates),
        "same_bone_candidates": candidate_records(same_bone_candidates),
        "same_bone_namespace_candidates": candidate_records(
            same_bone_namespace_candidates
        ),
        "namespace_candidates": candidate_records(namespace_candidates),
        "stem_related_candidates": candidate_records(stem_related_candidates),
    }


def candidate_records(records: list[dict[str, object]]) -> list[dict[str, object]]:
    return [candidate_record(record) for record in records]


def candidate_record(record: dict[str, object]) -> dict[str, object]:
    return {
        "embedded_name": record["embedded_name"],
        "model_name": record["model_name"],
        "bone_count": record["bone_count"],
        "support_status": record["support_status"],
        "namespace": cmb_namespace(record),
        "stems": sorted(cmb_candidate_stems(record)),
    }


def animation_namespace(name: str) -> str | None:
    parts = [part.lower() for part in name.replace("\\", "/").split("/")[:-1]]
    for part in parts:
        if part not in {"anim", "animation", "animations"}:
            return part
    return None


def cmb_namespace(record: dict[str, object]) -> str | None:
    embedded_name = record.get("embedded_name")
    if not isinstance(embedded_name, str):
        return None
    parts = embedded_name.replace("\\", "/").split("/")
    return parts[0].lower() if len(parts) > 1 else None


def stem_related(animation_stem: str, record: dict[str, object]) -> bool:
    stems = cmb_candidate_stems(record)
    if animation_stem in stems:
        return True
    return any(
        animation_stem in stem or stem in animation_stem
        for stem in stems
        if len(animation_stem) >= 3 and len(stem) >= 3
    )
