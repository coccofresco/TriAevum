from __future__ import annotations

import json
from collections import Counter, defaultdict
from pathlib import Path

from .actor_inventory import (
    ANIMATION_LIKE_TYPES,
    KNOWN_ANIMATION_TYPES,
    archive_support_status,
    format_magic,
    model_support_status,
    sorted_nested_counter,
)
from .binary import ParseError
from .cmb import CmbModel
from .romfs_inventory import sorted_counter
from .zar import ZarArchive, ZarFile

AUDITED_ANIMATION_LIKE_TYPES = ("anb", "faceb")
ANB_FORMAT_HIGH16_CANDIDATE = 0x0008
ANB_WORD04_SIGNATURE_CANDIDATE = 0x90900000
FACEB_MAGIC = b"fkb\x01"


def audit_actor_animation_like_payloads(
    actor_root: Path,
    output_path: Path | None = None,
    *,
    sample_limit: int = 100,
    include_records: bool = True,
) -> dict[str, object]:
    if not actor_root.is_dir():
        raise ParseError(f"{actor_root}: expected an extracted OOT3D actor directory")

    archive_count = 0
    archive_parse_errors: list[dict[str, object]] = []
    cmb_parse_errors: list[dict[str, object]] = []
    sample_records: list[dict[str, object]] = []
    records: list[dict[str, object]] = []
    payload_names_by_type: dict[str, dict[str, list[str]]] = {
        file_type: defaultdict(list) for file_type in AUDITED_ANIMATION_LIKE_TYPES
    }
    path_stems_by_type: dict[str, set[str]] = {
        file_type: set() for file_type in AUDITED_ANIMATION_LIKE_TYPES
    }

    payload_type_counts: Counter[str] = Counter()
    archives_with_type: Counter[str] = Counter()
    archive_payload_count_counts: dict[str, Counter[str]] = {
        file_type: Counter() for file_type in AUDITED_ANIMATION_LIKE_TYPES
    }
    archive_payload_counts: dict[str, Counter[str]] = {
        file_type: Counter() for file_type in AUDITED_ANIMATION_LIKE_TYPES
    }
    archive_support_counts: dict[str, Counter[str]] = {
        file_type: Counter() for file_type in AUDITED_ANIMATION_LIKE_TYPES
    }
    payload_sizes: dict[str, list[int]] = {
        file_type: [] for file_type in AUDITED_ANIMATION_LIKE_TYPES
    }
    magic_counts: dict[str, Counter[str]] = {
        file_type: Counter() for file_type in AUDITED_ANIMATION_LIKE_TYPES
    }
    first_word_high16_counts: dict[str, Counter[str]] = {
        file_type: Counter() for file_type in AUDITED_ANIMATION_LIKE_TYPES
    }
    first_word_low16_counts: dict[str, Counter[str]] = {
        file_type: Counter() for file_type in AUDITED_ANIMATION_LIKE_TYPES
    }
    word04_counts: dict[str, Counter[str]] = {
        file_type: Counter() for file_type in AUDITED_ANIMATION_LIKE_TYPES
    }
    anb_format_high16_counts: Counter[str] = Counter()
    anb_word04_signature_counts: Counter[str] = Counter()
    faceb_magic_counts: Counter[str] = Counter()
    faceb_size_match_counts: Counter[str] = Counter()
    faceb_entry_count_counts: Counter[str] = Counter()
    faceb_entry_frame_counts: Counter[str] = Counter()
    faceb_entry_value_pair_counts: Counter[str] = Counter()
    faceb_entry_value0_counts: Counter[str] = Counter()
    faceb_entry_value1_counts: Counter[str] = Counter()
    total_faceb_entries = 0

    for zar_path in sorted(actor_root.rglob("*.zar")):
        archive_count += 1
        archive_rel = zar_path.relative_to(actor_root).as_posix()
        try:
            archive = ZarArchive.from_path(zar_path)
        except Exception as exc:
            archive_parse_errors.append(
                {
                    "archive_path": archive_rel,
                    "reason": str(exc),
                }
            )
            continue

        files_by_type = {
            file_type: [
                file for file in archive.files if is_animation_like_file(file, file_type)
            ]
            for file_type in AUDITED_ANIMATION_LIKE_TYPES
        }
        for file_type, files in files_by_type.items():
            if files:
                archives_with_type[file_type] += 1
                archive_payload_count_counts[file_type][str(len(files))] += 1
                archive_payload_counts[file_type][archive_rel] = len(files)

        cmb_records = parsed_cmb_records(
            archive,
            zar_path,
            archive_rel,
            cmb_parse_errors,
        )
        support_status = animation_like_archive_support_status(archive, cmb_records)

        for file_type, files in files_by_type.items():
            for file in files:
                data = archive.read_file(file)
                payload_type_counts[file_type] += 1
                archive_support_counts[file_type][support_status] += 1
                payload_sizes[file_type].append(len(data))
                magic = format_magic(data[:4])
                magic_counts[file_type][magic] += 1

                first_word = u32_or_none(data, 0)
                word04 = u32_or_none(data, 4)
                if first_word is not None:
                    first_word_high16_counts[file_type][str(first_word >> 16)] += 1
                    first_word_low16_counts[file_type][str(first_word & 0xFFFF)] += 1
                if word04 is not None:
                    word04_counts[file_type][str(word04)] += 1

                metadata: dict[str, object]
                if file_type == "anb":
                    metadata = anb_metadata(data)
                    anb_format_high16_counts[str(metadata["first_word_high16"])] += 1
                    anb_word04_signature_counts[str(metadata["word04"])] += 1
                else:
                    metadata = faceb_metadata(data)
                    faceb_magic_counts[str(metadata["magic_status"])] += 1
                    faceb_size_match_counts[str(metadata["size_match_status"])] += 1
                    entry_count = int(metadata["entry_count"])
                    faceb_entry_count_counts[str(entry_count)] += 1
                    total_faceb_entries += entry_count
                    for entry in metadata["entries"]:
                        if not isinstance(entry, dict):
                            continue
                        faceb_entry_frame_counts[str(entry["frame"])] += 1
                        faceb_entry_value_pair_counts[
                            f"{entry['value0']},{entry['value1']}"
                        ] += 1
                        faceb_entry_value0_counts[str(entry["value0"])] += 1
                        faceb_entry_value1_counts[str(entry["value1"])] += 1

                path_stem = payload_path_stem(file.name)
                payload_names_by_type[file_type][file.name.lower()].append(archive_rel)
                path_stems_by_type[file_type].add(path_stem)
                record = payload_record(
                    archive_rel,
                    file,
                    file_type,
                    len(data),
                    magic,
                    first_word,
                    word04,
                    support_status,
                    len(cmb_records),
                    metadata,
                    data,
                )
                if include_records:
                    records.append(record)
                if len(sample_records) < sample_limit:
                    sample_records.append(record)

    name_stats = {
        file_type: payload_name_stats(payload_names_by_type[file_type])
        for file_type in AUDITED_ANIMATION_LIKE_TYPES
    }
    path_stem_overlap = animation_like_path_stem_overlap(path_stems_by_type)

    audit: dict[str, object] = {
        "format": "oot3d_actor_animation_like_payload_audit_v1",
        "actor_root": str(actor_root),
        "archive_count": archive_count,
        "archive_parse_error_count": len(archive_parse_errors),
        "cmb_parse_error_count": len(cmb_parse_errors),
        "payload_type_counts": sorted_counter(payload_type_counts),
        "archives_with_type": sorted_counter(archives_with_type),
        "archive_payload_count_counts": sorted_nested_counter(archive_payload_count_counts),
        "archive_payload_counts": sorted_nested_counter(archive_payload_counts),
        "archive_support_counts": sorted_nested_counter(archive_support_counts),
        "payload_size_summary": {
            file_type: size_summary(payload_sizes[file_type])
            for file_type in AUDITED_ANIMATION_LIKE_TYPES
        },
        "magic_counts": sorted_nested_counter(magic_counts),
        "first_word_high16_counts": sorted_nested_counter(first_word_high16_counts),
        "first_word_low16_counts": sorted_nested_counter(first_word_low16_counts),
        "word04_counts": sorted_nested_counter(word04_counts),
        "anb_format_high16_counts": sorted_counter(anb_format_high16_counts),
        "anb_word04_signature_counts": sorted_counter(anb_word04_signature_counts),
        "faceb_magic_status_counts": sorted_counter(faceb_magic_counts),
        "faceb_size_match_counts": sorted_counter(faceb_size_match_counts),
        "faceb_entry_count_counts": sorted_counter(faceb_entry_count_counts),
        "faceb_entry_total": total_faceb_entries,
        "faceb_entry_frame_counts": sorted_counter(faceb_entry_frame_counts),
        "faceb_entry_value_pair_counts": sorted_counter(faceb_entry_value_pair_counts),
        "faceb_entry_value0_counts": sorted_counter(faceb_entry_value0_counts),
        "faceb_entry_value1_counts": sorted_counter(faceb_entry_value1_counts),
        "payload_name_stats": name_stats,
        "path_stem_overlap": path_stem_overlap,
        "archive_parse_errors": archive_parse_errors,
        "cmb_parse_errors": cmb_parse_errors,
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


def parsed_cmb_records(
    archive: ZarArchive,
    zar_path: Path,
    archive_rel: str,
    cmb_parse_errors: list[dict[str, object]],
) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    for file in archive.files:
        if not is_cmb_file(file):
            continue
        try:
            model = CmbModel.parse(archive.read_file(file), f"{zar_path}!{file.name}")
        except Exception as exc:
            cmb_parse_errors.append(
                {
                    "archive_path": archive_rel,
                    "embedded_name": file.name,
                    "reason": str(exc),
                }
            )
            continue
        records.append(
            {
                "embedded_name": file.name,
                "model_name": model.name,
                "bone_count": model.bone_count,
                "support_status": model_support_status(model),
            }
        )
    return records


def animation_like_archive_support_status(
    archive: ZarArchive,
    cmb_records: list[dict[str, object]],
) -> str:
    type_counts = Counter(file.type_name for file in archive.files)
    known_animation_count = sum(type_counts[file_type] for file_type in KNOWN_ANIMATION_TYPES)
    animation_like_count = sum(type_counts[file_type] for file_type in ANIMATION_LIKE_TYPES)
    nonstatic_cmb_count = sum(
        1
        for record in cmb_records
        if record["support_status"] != "static_cmb_export_supported"
    )
    return archive_support_status(
        len(cmb_records),
        nonstatic_cmb_count,
        known_animation_count,
        animation_like_count,
    )


def anb_metadata(data: bytes) -> dict[str, object]:
    first_word = u32_or_none(data, 0) or 0
    word04 = u32_or_none(data, 4) or 0
    return {
        "first_word": first_word,
        "first_word_high16": first_word >> 16,
        "frame_count_candidate": first_word & 0xFFFF,
        "format_high16_matches_baseline": (first_word >> 16)
        == ANB_FORMAT_HIGH16_CANDIDATE,
        "word04": word04,
        "word04_matches_baseline": word04 == ANB_WORD04_SIGNATURE_CANDIDATE,
    }


def faceb_metadata(data: bytes) -> dict[str, object]:
    magic_status = "fkb01" if data[:4] == FACEB_MAGIC else format_magic(data[:4])
    entry_count = u32_or_none(data, 4) or 0
    expected_size = 8 + entry_count * 4
    size_match_status = "matches" if len(data) == expected_size else "mismatch"
    entries = []
    for index in range(entry_count):
        offset = 8 + index * 4
        if offset + 4 > len(data):
            break
        entries.append(
            {
                "index": index,
                "frame": int.from_bytes(data[offset : offset + 2], "little"),
                "value0": data[offset + 2],
                "value1": data[offset + 3],
            }
        )
    return {
        "magic_status": magic_status,
        "entry_count": entry_count,
        "expected_size": expected_size,
        "size_match_status": size_match_status,
        "entries": entries,
    }


def payload_record(
    archive_path: str,
    file: ZarFile,
    file_type: str,
    size: int,
    magic: str,
    first_word: int | None,
    word04: int | None,
    archive_support: str,
    parsed_cmb_count: int,
    metadata: dict[str, object],
    data: bytes,
) -> dict[str, object]:
    return {
        "archive_path": archive_path,
        "embedded_name": file.name,
        "embedded_index": file.index,
        "type": file_type,
        "size": size,
        "magic": magic,
        "first_word": first_word,
        "word04": word04,
        "archive_support_status": archive_support,
        "parsed_cmb_count": parsed_cmb_count,
        "path_stem": payload_path_stem(file.name),
        "metadata": metadata,
        "first_32_bytes": data[:32].hex(" "),
    }


def payload_name_stats(names_by_payload: dict[str, list[str]]) -> dict[str, object]:
    multiplicity_counts: Counter[str] = Counter()
    duplicate_count = 0
    singletons: list[dict[str, object]] = []
    multi_archive_samples: list[dict[str, object]] = []
    for name in sorted(names_by_payload):
        archives = sorted(names_by_payload[name])
        multiplicity_counts[str(len(archives))] += 1
        record = {
            "embedded_name": name,
            "archives": archives,
        }
        if len(archives) == 1:
            singletons.append(record)
        else:
            duplicate_count += 1
            if len(multi_archive_samples) < 25:
                multi_archive_samples.append(record)
    return {
        "unique_name_count": len(names_by_payload),
        "duplicate_name_count": duplicate_count,
        "multiplicity_counts": sorted_counter(multiplicity_counts),
        "singleton_samples": singletons[:25],
        "multi_archive_samples": multi_archive_samples,
    }


def animation_like_path_stem_overlap(
    path_stems_by_type: dict[str, set[str]],
) -> dict[str, int]:
    anb_stems = path_stems_by_type["anb"]
    faceb_stems = path_stems_by_type["faceb"]
    return {
        "anb_unique_path_stems": len(anb_stems),
        "faceb_unique_path_stems": len(faceb_stems),
        "shared_path_stems": len(anb_stems & faceb_stems),
        "anb_without_faceb_path_stems": len(anb_stems - faceb_stems),
        "faceb_without_anb_path_stems": len(faceb_stems - anb_stems),
    }


def is_animation_like_file(file: ZarFile, file_type: str) -> bool:
    return file.type_name == file_type or file.name.lower().endswith(f".{file_type}")


def is_cmb_file(file: ZarFile) -> bool:
    return file.type_name == "cmb" or file.name.lower().endswith(".cmb")


def payload_path_stem(name: str) -> str:
    return name.replace("\\", "/").lower().rsplit(".", 1)[0]


def u32_or_none(data: bytes, offset: int) -> int | None:
    if len(data) < offset + 4:
        return None
    return int.from_bytes(data[offset : offset + 4], "little")


def size_summary(values: list[int]) -> dict[str, int]:
    if not values:
        return {
            "count": 0,
            "min": 0,
            "max": 0,
            "total": 0,
            "unique_size_count": 0,
        }
    return {
        "count": len(values),
        "min": min(values),
        "max": max(values),
        "total": sum(values),
        "unique_size_count": len(set(values)),
    }
