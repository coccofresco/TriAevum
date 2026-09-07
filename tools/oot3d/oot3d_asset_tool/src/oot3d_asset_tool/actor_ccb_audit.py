from __future__ import annotations

import json
from collections import Counter
from pathlib import Path

from .actor_inventory import format_magic
from .binary import ParseError
from .media_asset_audit import size_summary
from .romfs_inventory import sorted_counter
from .zar import ZarArchive

EXPECTED_CCB_MAGIC = b"ccb\x00"
EXPECTED_CCB_VERSION = 3
CCB_OFFSET_TABLE_BASE = 0x18


def audit_actor_ccb_payloads(
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
    archive_ccb_counts: Counter[str] = Counter()
    ccb_count_distribution: Counter[str] = Counter()
    embedded_parent_dir_counts: Counter[str] = Counter()
    type_name_counts: Counter[str] = Counter()
    magic4_counts: Counter[str] = Counter()
    version_counts: Counter[str] = Counter()
    declared_size_delta_counts: Counter[str] = Counter()
    size_mod16_counts: Counter[str] = Counter()
    caad_count_counts: Counter[str] = Counter()
    caad_marker_count_counts: Counter[str] = Counter()
    mads_marker_count_counts: Counter[str] = Counter()
    cmad_marker_count_counts: Counter[str] = Counter()
    caad_index_sequence_counts: Counter[str] = Counter()
    size_values: list[int] = []

    zar_paths = sorted(actor_root.glob("*.zar"))
    parsed_zar_count = 0
    for zar_path in zar_paths:
        try:
            archive = ZarArchive.from_path(zar_path)
        except ParseError as exc:
            parse_errors.append({"archive": zar_path.name, "error": str(exc)})
            continue
        parsed_zar_count += 1
        archive_records: list[dict[str, object]] = []
        for file in archive.files:
            if not is_ccb_file(file.name, file.type_name):
                continue
            payload = archive.read_file(file)
            metadata = ccb_metadata(payload)
            record_issues = ccb_record_issues(
                zar_path.name,
                file.name,
                file.type_name,
                payload,
                metadata,
            )
            issues.extend(record_issues)
            declared_size = metadata["declared_size_candidate"]
            declared_size_delta = (
                len(payload) - int(declared_size) if declared_size is not None else None
            )
            record = {
                "archive": zar_path.name,
                "embedded_name": file.name,
                "embedded_parent_dir": embedded_parent_dir(file.name),
                "embedded_stem": Path(file.name).stem,
                "embedded_index": file.index,
                "embedded_type": file.type_name,
                "embedded_offset": file.offset,
                "size": len(payload),
                "size_mod16": len(payload) % 16,
                "magic4": format_magic(payload[:4]),
                "signature16": payload[:16].hex(" "),
                "declared_size_delta": declared_size_delta,
                **metadata,
                "issues": record_issues,
            }
            archive_records.append(record)

            size_values.append(len(payload))
            embedded_parent_dir_counts[str(record["embedded_parent_dir"])] += 1
            type_name_counts[file.type_name] += 1
            magic4_counts[str(record["magic4"])] += 1
            version_counts[str(record["version_candidate"])] += 1
            declared_size_delta_counts[str(declared_size_delta)] += 1
            size_mod16_counts[str(record["size_mod16"])] += 1
            caad_count_counts[str(metadata["caad_count_candidate"])] += 1
            caad_marker_count_counts[str(metadata["caad_marker_count"])] += 1
            mads_marker_count_counts[str(metadata["mads_marker_count"])] += 1
            cmad_marker_count_counts[str(metadata["cmad_marker_count"])] += 1
            caad_index_sequence_counts[str(metadata["caad_index_sequence_status"])] += 1

        if archive_records:
            archive_ccb_counts[zar_path.name] += len(archive_records)
            ccb_count_distribution[str(len(archive_records))] += 1
        for record in archive_records:
            if include_records:
                records.append(record)
            if len(sample_records) < sample_limit:
                sample_records.append(record)

    audit: dict[str, object] = {
        "format": "oot3d_actor_ccb_audit_v1",
        "actor_root": str(actor_root),
        "zar_file_count": len(zar_paths),
        "parsed_zar_count": parsed_zar_count,
        "parse_error_count": len(parse_errors),
        "parse_errors": parse_errors,
        "ccb_file_count": len(size_values),
        "archive_with_ccb_count": len(archive_ccb_counts),
        "total_size": sum(size_values),
        "size_summary": size_summary(size_values),
        "archive_ccb_counts": sorted_counter(archive_ccb_counts),
        "ccb_count_distribution": sorted_counter(ccb_count_distribution),
        "embedded_parent_dir_counts": sorted_counter(embedded_parent_dir_counts),
        "type_name_counts": sorted_counter(type_name_counts),
        "magic4_counts": sorted_counter(magic4_counts),
        "version_counts": sorted_counter(version_counts),
        "declared_size_delta_counts": sorted_counter(declared_size_delta_counts),
        "size_mod16_counts": sorted_counter(size_mod16_counts),
        "caad_count_counts": sorted_counter(caad_count_counts),
        "caad_marker_count_counts": sorted_counter(caad_marker_count_counts),
        "mads_marker_count_counts": sorted_counter(mads_marker_count_counts),
        "cmad_marker_count_counts": sorted_counter(cmad_marker_count_counts),
        "caad_index_sequence_counts": sorted_counter(caad_index_sequence_counts),
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


def is_ccb_file(name: str, type_name: str) -> bool:
    return Path(name).suffix.lower() == ".ccb" or type_name.lower() == "ccb"


def embedded_parent_dir(name: str) -> str:
    parent = Path(name).parent.as_posix()
    return parent if parent != "." else "<root>"


def ccb_metadata(data: bytes) -> dict[str, object]:
    header_words = ccb_header_words(data, 0x44)
    caad_count = header_words[4] if len(header_words) > 4 else None
    table_offsets = ccb_table_offsets(data, caad_count)
    caad_records = [caad_record(data, offset) for offset in table_offsets]
    caad_indices = [
        int(record["index"])
        for record in caad_records
        if record.get("index") is not None and record.get("marker") == "ascii:caad"
    ]
    return {
        "header_words_le": header_words,
        "version_candidate": header_words[1] if len(header_words) > 1 else None,
        "declared_size_candidate": header_words[2] if len(header_words) > 2 else None,
        "zero_word_candidate": header_words[3] if len(header_words) > 3 else None,
        "caad_count_candidate": caad_count,
        "span_candidate": header_words[5] if len(header_words) > 5 else None,
        "table_offset_count": len(table_offsets),
        "table_offsets": table_offsets,
        "table_offsets_monotonic": table_offsets == sorted(table_offsets),
        "table_offsets_in_bounds": all(0 <= offset < len(data) for offset in table_offsets),
        "caad_marker_count": count_marker(data, b"caad"),
        "mads_marker_count": count_marker(data, b"mads"),
        "cmad_marker_count": count_marker(data, b"cmad"),
        "caad_records": caad_records,
        "caad_index_sequence_status": caad_index_sequence_status(caad_indices),
    }


def ccb_header_words(data: bytes, limit: int) -> list[int]:
    word_limit = min(limit, len(data) - (len(data) % 4))
    return [
        int.from_bytes(data[offset : offset + 4], "little")
        for offset in range(0, word_limit, 4)
    ]


def ccb_table_offsets(data: bytes, caad_count: object) -> list[int]:
    if not isinstance(caad_count, int):
        return []
    if caad_count < 0 or caad_count > 1024:
        return []
    table_end = CCB_OFFSET_TABLE_BASE + caad_count * 4
    if table_end > len(data):
        return []
    return [
        int.from_bytes(data[offset : offset + 4], "little")
        for offset in range(CCB_OFFSET_TABLE_BASE, table_end, 4)
    ]


def caad_record(data: bytes, offset: int) -> dict[str, object]:
    if offset < 0 or offset + 16 > len(data):
        return {
            "offset": offset,
            "marker": None,
            "index": None,
            "entry_count_candidate": None,
            "frame_count_candidate": None,
        }
    return {
        "offset": offset,
        "marker": format_magic(data[offset : offset + 4]),
        "index": int.from_bytes(data[offset + 4 : offset + 8], "little"),
        "entry_count_candidate": int.from_bytes(data[offset + 8 : offset + 12], "little"),
        "frame_count_candidate": int.from_bytes(data[offset + 12 : offset + 16], "little"),
    }


def count_marker(data: bytes, marker: bytes) -> int:
    count = 0
    offset = 0
    while True:
        found = data.find(marker, offset)
        if found < 0:
            return count
        count += 1
        offset = found + 1


def caad_index_sequence_status(indices: list[int]) -> str:
    if not indices:
        return "none"
    expected = list(range(len(indices)))
    if indices == expected:
        return "zero_based_contiguous"
    if indices == sorted(indices):
        return "monotonic_noncontiguous"
    return "nonmonotonic"


def ccb_record_issues(
    archive: str,
    embedded_name: str,
    type_name: str,
    data: bytes,
    metadata: dict[str, object],
) -> list[dict[str, object]]:
    issues: list[dict[str, object]] = []
    suffix_is_ccb = Path(embedded_name).suffix.lower() == ".ccb"
    type_is_ccb = type_name.lower() == "ccb"
    if suffix_is_ccb != type_is_ccb:
        issues.append(
            {
                "type": "ccb_type_suffix_mismatch",
                "archive": archive,
                "embedded_name": embedded_name,
                "embedded_type": type_name,
            }
        )
    if len(data) < CCB_OFFSET_TABLE_BASE:
        issues.append(
            {
                "type": "ccb_payload_too_short",
                "archive": archive,
                "embedded_name": embedded_name,
                "size": len(data),
            }
        )
        return issues
    if data[:4] != EXPECTED_CCB_MAGIC:
        issues.append(
            {
                "type": "ccb_bad_magic",
                "archive": archive,
                "embedded_name": embedded_name,
                "magic4": format_magic(data[:4]),
            }
        )
    version = int.from_bytes(data[4:8], "little") if len(data) >= 8 else None
    if version != EXPECTED_CCB_VERSION:
        issues.append(
            {
                "type": "ccb_unexpected_version",
                "archive": archive,
                "embedded_name": embedded_name,
                "version": version,
            }
        )
    if len(data) % 16 != 0:
        issues.append(
            {
                "type": "ccb_size_not_16_aligned",
                "archive": archive,
                "embedded_name": embedded_name,
                "size": len(data),
            }
        )
    declared_size = metadata.get("declared_size_candidate")
    if isinstance(declared_size, int) and declared_size > len(data):
        issues.append(
            {
                "type": "ccb_declared_size_exceeds_payload",
                "archive": archive,
                "embedded_name": embedded_name,
                "declared_size": declared_size,
                "size": len(data),
            }
        )
    caad_count = metadata.get("caad_count_candidate")
    if metadata.get("table_offset_count") != caad_count:
        issues.append(
            {
                "type": "ccb_offset_table_count_mismatch",
                "archive": archive,
                "embedded_name": embedded_name,
                "caad_count": caad_count,
                "table_offset_count": metadata.get("table_offset_count"),
            }
        )
    if metadata.get("caad_marker_count") != caad_count:
        issues.append(
            {
                "type": "ccb_caad_marker_count_mismatch",
                "archive": archive,
                "embedded_name": embedded_name,
                "caad_count": caad_count,
                "caad_marker_count": metadata.get("caad_marker_count"),
            }
        )
    if not metadata.get("table_offsets_in_bounds"):
        issues.append(
            {
                "type": "ccb_offset_table_out_of_bounds",
                "archive": archive,
                "embedded_name": embedded_name,
            }
        )
    for record in metadata.get("caad_records", []):
        if not isinstance(record, dict):
            continue
        if record.get("marker") != "ascii:caad":
            issues.append(
                {
                    "type": "ccb_offset_missing_caad_marker",
                    "archive": archive,
                    "embedded_name": embedded_name,
                    "offset": record.get("offset"),
                    "marker": record.get("marker"),
                }
            )
    if metadata.get("caad_index_sequence_status") != "zero_based_contiguous":
        issues.append(
            {
                "type": "ccb_caad_index_sequence_not_contiguous",
                "archive": archive,
                "embedded_name": embedded_name,
                "status": metadata.get("caad_index_sequence_status"),
            }
        )
    return issues
