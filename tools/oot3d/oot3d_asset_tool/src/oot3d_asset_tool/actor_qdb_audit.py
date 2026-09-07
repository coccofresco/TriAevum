from __future__ import annotations

import json
from collections import Counter
from pathlib import Path

from .actor_inventory import format_magic, sorted_nested_counter
from .binary import ParseError
from .media_asset_audit import size_summary
from .romfs_inventory import sorted_counter
from .zar import ZarArchive

EXPECTED_QDB_MAGIC = b" BDQ"
EXPECTED_QDB_VERSION = 3


def audit_actor_qdb_payloads(
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
    archive_qdb_counts: Counter[str] = Counter()
    qdb_count_distribution: Counter[str] = Counter()
    embedded_parent_dir_counts: Counter[str] = Counter()
    embedded_name_counts: Counter[str] = Counter()
    embedded_stem_counts: Counter[str] = Counter()
    type_name_counts: Counter[str] = Counter()
    magic4_counts: Counter[str] = Counter()
    signature16_counts: Counter[str] = Counter()
    version_counts: Counter[str] = Counter()
    size_mod16_counts: Counter[str] = Counter()
    size_values: list[int] = []
    header_word_counts: dict[str, Counter[str]] = {
        f"word_{index:02d}": Counter() for index in range(8)
    }

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
            if not is_qdb_file(file.name, file.type_name):
                continue
            payload = archive.read_file(file)
            record_issues = qdb_record_issues(zar_path.name, file.name, file.type_name, payload)
            issues.extend(record_issues)
            header_words = qdb_header_words(payload)
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
                "header_words_le": header_words,
                "version_candidate": header_words[1] if len(header_words) > 1 else None,
                "entry_count_candidate": header_words[2] if len(header_words) > 2 else None,
                "span_candidate": header_words[3] if len(header_words) > 3 else None,
                "issues": record_issues,
            }
            archive_records.append(record)

            size_values.append(len(payload))
            embedded_parent_dir_counts[
                qdb_parent_dir_counter_key(str(record["embedded_parent_dir"]))
            ] += 1
            embedded_name_counts[file.name] += 1
            embedded_stem_counts[str(record["embedded_stem"])] += 1
            type_name_counts[file.type_name] += 1
            magic4_counts[str(record["magic4"])] += 1
            signature16_counts[str(record["signature16"])] += 1
            version_counts[str(record["version_candidate"])] += 1
            size_mod16_counts[str(record["size_mod16"])] += 1
            for index, word in enumerate(header_words[:8]):
                header_word_counts[f"word_{index:02d}"][str(word)] += 1

        if archive_records:
            archive_qdb_counts[zar_path.name] += len(archive_records)
            qdb_count_distribution[str(len(archive_records))] += 1
        for record in archive_records:
            if include_records:
                records.append(record)
            if len(sample_records) < sample_limit:
                sample_records.append(record)

    duplicate_embedded_names = {
        name: count for name, count in sorted(embedded_name_counts.items()) if count > 1
    }
    duplicate_embedded_stems = {
        stem: count for stem, count in sorted(embedded_stem_counts.items()) if count > 1
    }

    audit: dict[str, object] = {
        "format": "oot3d_actor_qdb_audit_v1",
        "actor_root": str(actor_root),
        "zar_file_count": len(zar_paths),
        "parsed_zar_count": parsed_zar_count,
        "parse_error_count": len(parse_errors),
        "parse_errors": parse_errors,
        "qdb_file_count": len(size_values),
        "archive_with_qdb_count": len(archive_qdb_counts),
        "total_size": sum(size_values),
        "size_summary": size_summary(size_values),
        "archive_qdb_counts": sorted_counter(archive_qdb_counts),
        "qdb_count_distribution": sorted_counter(qdb_count_distribution),
        "embedded_parent_dir_counts": sorted_counter(embedded_parent_dir_counts),
        "type_name_counts": sorted_counter(type_name_counts),
        "magic4_counts": sorted_counter(magic4_counts),
        "signature16_counts": sorted_counter(signature16_counts),
        "version_counts": sorted_counter(version_counts),
        "size_mod16_counts": sorted_counter(size_mod16_counts),
        "header_word_counts": sorted_nested_counter(header_word_counts),
        "embedded_name_count": len(embedded_name_counts),
        "embedded_stem_count": len(embedded_stem_counts),
        "duplicate_embedded_names": duplicate_embedded_names,
        "duplicate_embedded_stems": duplicate_embedded_stems,
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


def is_qdb_file(name: str, type_name: str) -> bool:
    return Path(name).suffix.lower() == ".qdb" or type_name.lower() == "qdb"


def qdb_header_words(data: bytes) -> list[int]:
    limit = min(32, len(data) - (len(data) % 4))
    return [int.from_bytes(data[offset : offset + 4], "little") for offset in range(0, limit, 4)]


def embedded_parent_dir(name: str) -> str:
    parent = Path(name).parent.as_posix()
    return parent if parent != "." else "<root>"


def qdb_parent_dir_counter_key(parent_dir: str) -> str:
    if parent_dir == "demo":
        return "demo_lowercase"
    if parent_dir == "Demo":
        return "Demo_uppercase"
    return parent_dir


def qdb_record_issues(
    archive: str,
    embedded_name: str,
    type_name: str,
    data: bytes,
) -> list[dict[str, object]]:
    issues: list[dict[str, object]] = []
    suffix_is_qdb = Path(embedded_name).suffix.lower() == ".qdb"
    type_is_qdb = type_name.lower() == "qdb"
    if suffix_is_qdb != type_is_qdb:
        issues.append(
            {
                "type": "qdb_type_suffix_mismatch",
                "archive": archive,
                "embedded_name": embedded_name,
                "embedded_type": type_name,
            }
        )
    if len(data) < 16:
        issues.append(
            {
                "type": "qdb_payload_too_short",
                "archive": archive,
                "embedded_name": embedded_name,
                "size": len(data),
            }
        )
        return issues
    if data[:4] != EXPECTED_QDB_MAGIC:
        issues.append(
            {
                "type": "qdb_bad_magic",
                "archive": archive,
                "embedded_name": embedded_name,
                "magic4": format_magic(data[:4]),
            }
        )
    version = int.from_bytes(data[4:8], "little")
    if version != EXPECTED_QDB_VERSION:
        issues.append(
            {
                "type": "qdb_unexpected_version",
                "archive": archive,
                "embedded_name": embedded_name,
                "version": version,
            }
        )
    if len(data) % 16 != 0:
        issues.append(
            {
                "type": "qdb_size_not_16_aligned",
                "archive": archive,
                "embedded_name": embedded_name,
                "size": len(data),
            }
        )
    return issues
