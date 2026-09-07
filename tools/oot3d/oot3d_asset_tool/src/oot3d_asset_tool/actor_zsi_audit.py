from __future__ import annotations

import json
from collections import Counter
from pathlib import Path

from .actor_inventory import format_magic, sorted_nested_counter
from .binary import ParseError
from .media_asset_audit import size_summary
from .romfs_inventory import sorted_counter
from .zar import ZarArchive
from .zsi import ZSI_MAGIC, ZsiFile

EXPECTED_BGDATA_MARKER = "ShUnqueen"
EXPECTED_BGDATA_VERSION = 3
EXPECTED_DECLARED_SIZE_DELTA = 60
BGDATA_FOOTER_SIZE = 0x20
BGDATA_OFFSET_BASE = 0x10


def audit_actor_zsi_payloads(
    actor_root: Path,
    output_path: Path | None = None,
    *,
    sample_limit: int = 100,
    include_records: bool = True,
) -> dict[str, object]:
    if not actor_root.is_dir():
        raise ParseError(f"{actor_root}: expected an extracted OOT3D actor directory")

    records: list[dict[str, object]] = []
    sample_records: list[dict[str, object]] = []
    issues: list[dict[str, object]] = []
    parse_errors: list[dict[str, object]] = []
    archive_zsi_counts: Counter[str] = Counter()
    zsi_count_distribution: Counter[str] = Counter()
    embedded_parent_dir_counts: Counter[str] = Counter()
    embedded_name_counts: Counter[str] = Counter()
    type_name_counts: Counter[str] = Counter()
    magic4_counts: Counter[str] = Counter()
    bgdata_marker_counts: Counter[str] = Counter()
    version_counts: Counter[str] = Counter()
    declared_size_delta_counts: Counter[str] = Counter()
    size_mod4_counts: Counter[str] = Counter()
    size_mod16_counts: Counter[str] = Counter()
    footer_layout_status_counts: Counter[str] = Counter()
    scene_setup_count_counts: Counter[str] = Counter()
    embedded_cmb_count_counts: Counter[str] = Counter()
    collision_candidate_count_counts: Counter[str] = Counter()
    footer_field_counts: dict[str, Counter[str]] = {
        "vertex_count": Counter(),
        "polygon_count": Counter(),
        "surface_type_count": Counter(),
        "unknown_count": Counter(),
        "reserved_0": Counter(),
        "reserved_1": Counter(),
        "vertex_rel_offset": Counter(),
        "polygon_rel_offset": Counter(),
        "surface_type_rel_offset": Counter(),
        "surface_type_end_rel_offset": Counter(),
        "terminator": Counter(),
    }
    totals: Counter[str] = Counter()
    size_values: list[int] = []

    zar_paths = sorted(actor_root.glob("*.zar"))
    parsed_zar_count = 0
    for zar_path in zar_paths:
        try:
            archive = ZarArchive.from_path(zar_path)
        except ParseError as exc:
            parse_errors.append({"archive": zar_path.name, "stage": "zar", "error": str(exc)})
            continue
        parsed_zar_count += 1
        archive_records: list[dict[str, object]] = []
        for file in archive.files:
            if not is_zsi_file(file.name, file.type_name):
                continue
            payload = archive.read_file(file)
            metadata = actor_zsi_metadata(
                payload,
                f"{zar_path.name}!{file.name}",
                parse_errors,
            )
            record_issues = actor_zsi_record_issues(
                zar_path.name,
                file.name,
                file.type_name,
                payload,
                metadata,
            )
            issues.extend(record_issues)
            record = {
                "archive": zar_path.name,
                "embedded_name": file.name,
                "embedded_parent_dir": embedded_parent_dir(file.name),
                "embedded_stem": Path(file.name).stem,
                "embedded_index": file.index,
                "embedded_type": file.type_name,
                "embedded_offset": file.offset,
                "size": len(payload),
                "size_mod4": len(payload) % 4,
                "size_mod16": len(payload) % 16,
                "magic4": format_magic(payload[:4]),
                "signature16": payload[:16].hex(" "),
                **metadata,
                "issues": record_issues,
            }
            archive_records.append(record)

            size_values.append(len(payload))
            embedded_parent_dir_counts[str(record["embedded_parent_dir"])] += 1
            embedded_name_counts[file.name] += 1
            type_name_counts[file.type_name] += 1
            magic4_counts[str(record["magic4"])] += 1
            bgdata_marker_counts[str(metadata["bgdata_marker"])] += 1
            version_counts[str(metadata["version_candidate"])] += 1
            declared_size_delta_counts[str(metadata["declared_size_delta"])] += 1
            size_mod4_counts[str(record["size_mod4"])] += 1
            size_mod16_counts[str(record["size_mod16"])] += 1
            footer = metadata.get("footer")
            if isinstance(footer, dict):
                footer_layout_status_counts[str(footer.get("layout_status"))] += 1
                for key in footer_field_counts:
                    footer_field_counts[key][str(footer.get(key))] += 1
                totals["vertex_count"] += int(footer.get("vertex_count") or 0)
                totals["polygon_count"] += int(footer.get("polygon_count") or 0)
                totals["surface_type_count"] += int(footer.get("surface_type_count") or 0)
                totals["unknown_count"] += int(footer.get("unknown_count") or 0)
            scene_setup_count_counts[str(metadata["scene_setup_count"])] += 1
            embedded_cmb_count_counts[str(metadata["embedded_cmb_count"])] += 1
            collision_candidate_count_counts[str(metadata["collision_candidate_count"])] += 1

        if archive_records:
            archive_zsi_counts[zar_path.name] += len(archive_records)
            zsi_count_distribution[str(len(archive_records))] += 1
        for record in archive_records:
            if include_records:
                records.append(record)
            if len(sample_records) < sample_limit:
                sample_records.append(record)

    duplicate_embedded_names = {
        name: count for name, count in sorted(embedded_name_counts.items()) if count > 1
    }
    audit: dict[str, object] = {
        "format": "oot3d_actor_zsi_audit_v1",
        "actor_root": str(actor_root),
        "zar_file_count": len(zar_paths),
        "parsed_zar_count": parsed_zar_count,
        "parse_error_count": len(parse_errors),
        "parse_errors": parse_errors,
        "zsi_file_count": len(size_values),
        "archive_with_zsi_count": len(archive_zsi_counts),
        "total_size": sum(size_values),
        "size_summary": size_summary(size_values),
        "archive_zsi_counts": sorted_counter(archive_zsi_counts),
        "zsi_count_distribution": sorted_counter(zsi_count_distribution),
        "embedded_parent_dir_counts": sorted_counter(embedded_parent_dir_counts),
        "type_name_counts": sorted_counter(type_name_counts),
        "magic4_counts": sorted_counter(magic4_counts),
        "bgdata_marker_counts": sorted_counter(bgdata_marker_counts),
        "version_counts": sorted_counter(version_counts),
        "declared_size_delta_counts": sorted_counter(declared_size_delta_counts),
        "size_mod4_counts": sorted_counter(size_mod4_counts),
        "size_mod16_counts": sorted_counter(size_mod16_counts),
        "footer_layout_status_counts": sorted_counter(footer_layout_status_counts),
        "footer_field_counts": sorted_nested_counter(footer_field_counts),
        "totals": sorted_counter(totals),
        "scene_setup_count_counts": sorted_counter(scene_setup_count_counts),
        "embedded_cmb_count_counts": sorted_counter(embedded_cmb_count_counts),
        "collision_candidate_count_counts": sorted_counter(collision_candidate_count_counts),
        "embedded_name_count": len(embedded_name_counts),
        "duplicate_embedded_names": duplicate_embedded_names,
        "issue_count": len(issues),
        "issues": issues,
        "sample_records": sample_records,
        "records": records if include_records else [],
    }

    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(
            json.dumps(audit, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    return audit


def is_zsi_file(name: str, type_name: str) -> bool:
    return Path(name).suffix.lower() == ".zsi" or type_name.lower() == "zsi"


def embedded_parent_dir(name: str) -> str:
    parent = Path(name).parent.as_posix()
    return parent if parent != "." else "<root>"


def actor_zsi_metadata(
    data: bytes,
    source: str,
    parse_errors: list[dict[str, object]],
) -> dict[str, object]:
    zsi = ZsiFile(Path(source), data)
    scene_setups = safe_zsi_stage(zsi.scene_setups, parse_errors, source, "scene_setups")
    embedded_cmbs = safe_zsi_stage(zsi.embedded_cmbs, parse_errors, source, "embedded_cmbs")
    collision_candidates = safe_zsi_stage(
        zsi.collision_header_candidates,
        parse_errors,
        source,
        "collision_header_candidates",
    )
    declared_size = u32_or_none(data, 0x14)
    footer = bgdata_footer(data)
    return {
        "bgdata_marker": bgdata_marker(data),
        "version_candidate": u32_or_none(data, 0x10),
        "declared_size_candidate": declared_size,
        "declared_size_delta": len(data) - declared_size if declared_size is not None else None,
        "scene_setup_count": len(scene_setups),
        "embedded_cmb_count": len(embedded_cmbs),
        "collision_candidate_count": len(collision_candidates),
        "footer": footer,
    }


def safe_zsi_stage(
    fn,
    parse_errors: list[dict[str, object]],
    source: str,
    stage: str,
) -> list[object]:
    try:
        return list(fn())
    except Exception as exc:
        parse_errors.append({"source": source, "stage": stage, "error": str(exc)})
        return []


def bgdata_marker(data: bytes) -> str | None:
    if len(data) < 0x10:
        return None
    return data[4:16].rstrip(b"\0").decode("ascii", errors="replace")


def bgdata_footer(data: bytes) -> dict[str, object] | None:
    if len(data) < 0x18 + BGDATA_FOOTER_SIZE:
        return None
    offset = len(data) - BGDATA_FOOTER_SIZE
    vertex_count = int.from_bytes(data[offset : offset + 2], "little")
    polygon_count = int.from_bytes(data[offset + 2 : offset + 4], "little")
    surface_type_count = int.from_bytes(data[offset + 4 : offset + 6], "little")
    unknown_count = int.from_bytes(data[offset + 6 : offset + 8], "little")
    reserved_0 = int.from_bytes(data[offset + 8 : offset + 10], "little")
    reserved_1 = int.from_bytes(data[offset + 10 : offset + 12], "little")
    vertex_rel_offset = int.from_bytes(data[offset + 12 : offset + 16], "little")
    polygon_rel_offset = int.from_bytes(data[offset + 16 : offset + 20], "little")
    surface_type_rel_offset = int.from_bytes(data[offset + 20 : offset + 24], "little")
    surface_type_end_rel_offset = int.from_bytes(data[offset + 24 : offset + 28], "little")
    terminator = int.from_bytes(data[offset + 28 : offset + 32], "little")
    expected_polygon_rel_offset = align4(vertex_rel_offset + vertex_count * 6)
    expected_surface_type_rel_offset = expected_polygon_rel_offset + polygon_count * 0x14
    expected_surface_type_end_rel_offset = (
        expected_surface_type_rel_offset + surface_type_count * 8
    )
    declared_size = u32_or_none(data, 0x14)
    layout_checks = {
        "vertex_rel_offset_is_8": vertex_rel_offset == 8,
        "polygon_rel_offset_matches_vertex_count": (
            polygon_rel_offset == expected_polygon_rel_offset
        ),
        "surface_type_rel_offset_matches_polygon_count": (
            surface_type_rel_offset == expected_surface_type_rel_offset
        ),
        "surface_type_end_rel_offset_matches_surface_type_count": (
            surface_type_end_rel_offset == expected_surface_type_end_rel_offset
        ),
        "declared_size_matches_surface_type_end_plus_8": (
            declared_size == surface_type_end_rel_offset + 8
        ),
        "unknown_count_is_1": unknown_count == 1,
        "reserved_words_zero": reserved_0 == 0 and reserved_1 == 0,
        "terminator_zero": terminator == 0,
    }
    layout_status = "valid" if all(layout_checks.values()) else "invalid"
    return {
        "offset": offset,
        "vertex_count": vertex_count,
        "polygon_count": polygon_count,
        "surface_type_count": surface_type_count,
        "unknown_count": unknown_count,
        "reserved_0": reserved_0,
        "reserved_1": reserved_1,
        "vertex_rel_offset": vertex_rel_offset,
        "polygon_rel_offset": polygon_rel_offset,
        "surface_type_rel_offset": surface_type_rel_offset,
        "surface_type_end_rel_offset": surface_type_end_rel_offset,
        "terminator": terminator,
        "expected_polygon_rel_offset": expected_polygon_rel_offset,
        "expected_surface_type_rel_offset": expected_surface_type_rel_offset,
        "expected_surface_type_end_rel_offset": expected_surface_type_end_rel_offset,
        "vertex_padding_bytes": expected_polygon_rel_offset
        - (vertex_rel_offset + vertex_count * 6),
        "layout_status": layout_status,
        "layout_checks": layout_checks,
        "post_surface_metadata_bytes": offset
        - (BGDATA_OFFSET_BASE + surface_type_end_rel_offset),
    }


def align4(value: int) -> int:
    return (value + 3) & ~3


def u32_or_none(data: bytes, offset: int) -> int | None:
    if offset < 0 or offset + 4 > len(data):
        return None
    return int.from_bytes(data[offset : offset + 4], "little")


def actor_zsi_record_issues(
    archive: str,
    embedded_name: str,
    type_name: str,
    data: bytes,
    metadata: dict[str, object],
) -> list[dict[str, object]]:
    issues: list[dict[str, object]] = []
    suffix_is_zsi = Path(embedded_name).suffix.lower() == ".zsi"
    type_is_zsi = type_name.lower() == "zsi"
    if suffix_is_zsi != type_is_zsi:
        issues.append(
            {
                "type": "zsi_type_suffix_mismatch",
                "archive": archive,
                "embedded_name": embedded_name,
                "embedded_type": type_name,
            }
        )
    if len(data) < 0x18 + BGDATA_FOOTER_SIZE:
        issues.append(
            {
                "type": "zsi_payload_too_short",
                "archive": archive,
                "embedded_name": embedded_name,
                "size": len(data),
            }
        )
        return issues
    if data[:4] != ZSI_MAGIC:
        issues.append(
            {
                "type": "zsi_bad_magic",
                "archive": archive,
                "embedded_name": embedded_name,
                "magic4": format_magic(data[:4]),
            }
        )
    if metadata.get("bgdata_marker") != EXPECTED_BGDATA_MARKER:
        issues.append(
            {
                "type": "zsi_unexpected_bgdata_marker",
                "archive": archive,
                "embedded_name": embedded_name,
                "marker": metadata.get("bgdata_marker"),
            }
        )
    if metadata.get("version_candidate") != EXPECTED_BGDATA_VERSION:
        issues.append(
            {
                "type": "zsi_unexpected_version",
                "archive": archive,
                "embedded_name": embedded_name,
                "version": metadata.get("version_candidate"),
            }
        )
    if metadata.get("declared_size_delta") != EXPECTED_DECLARED_SIZE_DELTA:
        issues.append(
            {
                "type": "zsi_unexpected_declared_size_delta",
                "archive": archive,
                "embedded_name": embedded_name,
                "declared_size_delta": metadata.get("declared_size_delta"),
            }
        )
    if len(data) % 4 != 0:
        issues.append(
            {
                "type": "zsi_size_not_4_aligned",
                "archive": archive,
                "embedded_name": embedded_name,
                "size": len(data),
            }
        )
    footer = metadata.get("footer")
    if not isinstance(footer, dict):
        issues.append(
            {
                "type": "zsi_missing_bgdata_footer",
                "archive": archive,
                "embedded_name": embedded_name,
            }
        )
        return issues
    if footer.get("layout_status") != "valid":
        issues.append(
            {
                "type": "zsi_invalid_bgdata_footer_layout",
                "archive": archive,
                "embedded_name": embedded_name,
                "layout_checks": footer.get("layout_checks"),
            }
        )
    if metadata.get("scene_setup_count") != 0:
        issues.append(
            {
                "type": "zsi_actor_bgdata_unexpected_scene_setup",
                "archive": archive,
                "embedded_name": embedded_name,
                "scene_setup_count": metadata.get("scene_setup_count"),
            }
        )
    if metadata.get("embedded_cmb_count") != 0:
        issues.append(
            {
                "type": "zsi_actor_bgdata_unexpected_embedded_cmb",
                "archive": archive,
                "embedded_name": embedded_name,
                "embedded_cmb_count": metadata.get("embedded_cmb_count"),
            }
        )
    if metadata.get("collision_candidate_count") != 0:
        issues.append(
            {
                "type": "zsi_actor_bgdata_unexpected_scene_collision_candidate",
                "archive": archive,
                "embedded_name": embedded_name,
                "collision_candidate_count": metadata.get("collision_candidate_count"),
            }
        )
    return issues
