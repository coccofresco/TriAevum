from __future__ import annotations

from collections import Counter
import hashlib
import json
from pathlib import Path

from .animation_like_audit import (
    ANB_FORMAT_HIGH16_CANDIDATE,
    ANB_WORD04_SIGNATURE_CANDIDATE,
)
from .binary import ParseError
from .romfs_inventory import sorted_counter
from .zar import ZarArchive, ZarFile


ANB_BATCH_EXPORT_FORMAT = "oot3d_anb_payload_batch_export_v1"
ANB_PAYLOAD_EXPORT_FORMAT = "oot3d_anb_payload_export_v1"
ANB_FRAME_STRIDE_BYTES = 134
ANB_FRAME_CHANNEL_COUNT_CANDIDATE = ANB_FRAME_STRIDE_BYTES // 2


def export_anb_payload_batch(
    primary_archive_path: Path,
    output_path: Path,
    *,
    duplicate_archive_path: Path | None = None,
    csab_binding_manifest_path: Path | None = None,
    csab_target_archive: str | None = None,
    csab_target_cmb: str | None = None,
    sample_limit: int = 20,
) -> dict[str, object]:
    primary_archive = ZarArchive.from_path(primary_archive_path)
    duplicate_archive = ZarArchive.from_path(duplicate_archive_path) if duplicate_archive_path is not None else None
    duplicate_by_name = {
        file.name.lower(): file for file in duplicate_archive.files if is_anb_file(file)
    } if duplicate_archive is not None else {}

    records: list[dict[str, object]] = []
    sample_records: list[dict[str, object]] = []
    issue_samples: list[dict[str, object]] = []
    frame_count_candidates: Counter[str] = Counter()
    payload_word_counts: Counter[str] = Counter()
    trailing_byte_counts: Counter[str] = Counter()
    size_counts: Counter[str] = Counter()
    channel_count_candidates: Counter[str] = Counter()
    duplicate_match_count = 0
    duplicate_missing_count = 0
    duplicate_mismatch_count = 0
    invalid_header_count = 0
    decoded_frame_table_match_count = 0
    decoded_frame_table_mismatch_count = 0
    total_decoded_frame_count = 0
    total_s16_sample_count = 0
    channel_aggregate = new_channel_aggregate()
    csab_lookup = csab_stem_lookup(
        csab_binding_manifest_path,
        csab_target_archive=csab_target_archive,
        csab_target_cmb=csab_target_cmb,
    )

    for file in sorted((file for file in primary_archive.files if is_anb_file(file)), key=lambda item: item.name.lower()):
        data = primary_archive.read_file(file)
        duplicate_file = duplicate_by_name.get(file.name.lower())
        duplicate_sha256 = None
        duplicate_matches = None
        if duplicate_archive is not None:
            if duplicate_file is None:
                duplicate_missing_count += 1
                add_issue(issue_samples, sample_limit, "duplicate_payload_missing", file.name)
            else:
                duplicate_data = duplicate_archive.read_file(duplicate_file)
                duplicate_sha256 = hashlib.sha256(duplicate_data).hexdigest()
                duplicate_matches = duplicate_data == data
                if duplicate_matches:
                    duplicate_match_count += 1
                else:
                    duplicate_mismatch_count += 1
                    add_issue(issue_samples, sample_limit, "duplicate_payload_mismatch", file.name)

        record = anb_record(file, data, duplicate_sha256, duplicate_matches)
        attach_csab_lookup(record, csab_lookup)
        metadata = record["metadata"]
        s16_samples = record_s16_samples(record)
        record_channel_summary = summarize_record_channels(s16_samples)
        metadata["active_channel_count"] = record_channel_summary["active_channel_count"]
        metadata["static_channel_count"] = record_channel_summary["static_channel_count"]
        metadata["record_sample_min"] = record_channel_summary["sample_min"]
        metadata["record_sample_max"] = record_channel_summary["sample_max"]
        update_channel_aggregate(channel_aggregate, s16_samples, record_channel_summary)
        if not metadata["format_high16_matches_baseline"] or not metadata["word04_matches_baseline"]:
            invalid_header_count += 1
            add_issue(issue_samples, sample_limit, "unexpected_anb_header", file.name)
        if metadata["frame_table_size_matches_candidate"]:
            decoded_frame_table_match_count += 1
        else:
            decoded_frame_table_mismatch_count += 1
            add_issue(issue_samples, sample_limit, "frame_table_size_mismatch", file.name)
        frame_count_candidates[str(metadata["frame_count_candidate"])] += 1
        channel_count_candidates[str(metadata["channel_count_candidate"])] += 1
        total_decoded_frame_count += int(metadata["decoded_frame_count"])
        total_s16_sample_count += int(metadata["s16_sample_count"])
        payload_word_counts[str(record["payload_word_count"])] += 1
        trailing_byte_counts[str(record["trailing_byte_count"])] += 1
        size_counts[str(record["size"])] += 1
        records.append(record)
        if len(sample_records) < sample_limit:
            sample_records.append(sample_record(record))

    export = {
        "format": ANB_BATCH_EXPORT_FORMAT,
        "primary_archive": str(primary_archive_path),
        "duplicate_archive": str(duplicate_archive_path) if duplicate_archive_path is not None else None,
        "payload_count": len(records),
        "duplicate_payload_match_count": duplicate_match_count,
        "duplicate_payload_missing_count": duplicate_missing_count,
        "duplicate_payload_mismatch_count": duplicate_mismatch_count,
        "invalid_header_count": invalid_header_count,
        "decoded_frame_table_match_count": decoded_frame_table_match_count,
        "decoded_frame_table_mismatch_count": decoded_frame_table_mismatch_count,
        "total_decoded_frame_count": total_decoded_frame_count,
        "total_s16_sample_count": total_s16_sample_count,
        "channel_summary": finalize_channel_aggregate(channel_aggregate),
        "csab_lookup": summarize_csab_lookup(records, csab_lookup),
        "frame_count_candidate_counts": sorted_counter(frame_count_candidates),
        "channel_count_candidate_counts": sorted_counter(channel_count_candidates),
        "payload_word_count_counts": sorted_counter(payload_word_counts),
        "trailing_byte_count_counts": sorted_counter(trailing_byte_counts),
        "payload_size_counts": sorted_counter(size_counts),
        "issue_count": (
            duplicate_missing_count
            + duplicate_mismatch_count
            + invalid_header_count
            + decoded_frame_table_mismatch_count
        ),
        "issue_samples": issue_samples,
        "sample_records": sample_records,
        "records": records,
        "contract": (
            "ANB payloads are exported as a stable raw IR while the semantic channel layout is still being "
            "named. The header, u32 payload word stream, trailing bytes, and duplicate archive equality are "
            "preserved so runtime work can depend on byte-stable decoded records instead of opaque ZAR blobs."
        ),
    }
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(export, indent=2), encoding="utf-8", newline="\n")
    return export


def anb_record(
    file: ZarFile,
    data: bytes,
    duplicate_sha256: str | None,
    duplicate_matches: bool | None,
) -> dict[str, object]:
    if len(data) < 8:
        raise ParseError(f"{file.name}: ANB payload is too small")
    first_word = int.from_bytes(data[0:4], "little")
    word04 = int.from_bytes(data[4:8], "little")
    payload = data[8:]
    frame_count_candidate = first_word & 0xFFFF
    decoded_frame_count = frame_count_candidate
    expected_frame_table_byte_count = decoded_frame_count * ANB_FRAME_STRIDE_BYTES
    frame_table_size_matches_candidate = len(payload) == expected_frame_table_byte_count
    s16_samples = s16_payload_samples(payload)
    word_byte_count = (len(payload) // 4) * 4
    payload_words = [
        int.from_bytes(payload[offset: offset + 4], "little")
        for offset in range(0, word_byte_count, 4)
    ]
    trailing_bytes = payload[word_byte_count:]
    return {
        "format": ANB_PAYLOAD_EXPORT_FORMAT,
        "embedded_name": file.name,
        "path_stem": path_stem(file.name),
        "embedded_index": file.index,
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "duplicate_sha256": duplicate_sha256,
        "duplicate_matches": duplicate_matches,
        "metadata": {
            "first_word": first_word,
            "first_word_high16": first_word >> 16,
            "frame_count_candidate": frame_count_candidate,
            "format_high16_matches_baseline": (first_word >> 16) == ANB_FORMAT_HIGH16_CANDIDATE,
            "word04": word04,
            "word04_matches_baseline": word04 == ANB_WORD04_SIGNATURE_CANDIDATE,
            "frame_stride_bytes": ANB_FRAME_STRIDE_BYTES,
            "channel_count_candidate": ANB_FRAME_CHANNEL_COUNT_CANDIDATE,
            "decoded_frame_count": decoded_frame_count,
            "expected_frame_table_byte_count": expected_frame_table_byte_count,
            "actual_frame_table_byte_count": len(payload),
            "frame_table_size_matches_candidate": frame_table_size_matches_candidate,
            "s16_sample_count": len(s16_samples),
            "sampled_frames": sampled_s16_frames(s16_samples, decoded_frame_count),
        },
        "payload_word_count": len(payload_words),
        "trailing_byte_count": len(trailing_bytes),
        "trailing_bytes_hex": trailing_bytes.hex(" "),
        "payload_words": payload_words,
    }


def csab_stem_lookup(
    csab_binding_manifest_path: Path | None,
    *,
    csab_target_archive: str | None,
    csab_target_cmb: str | None,
) -> dict[str, dict[str, object]]:
    if csab_binding_manifest_path is None:
        return {}
    if not csab_binding_manifest_path.is_file():
        raise ParseError(f"{csab_binding_manifest_path}: CSAB binding manifest not found")
    manifest = json.loads(csab_binding_manifest_path.read_text(encoding="utf-8"))
    targets = manifest.get("targets", [])
    if not isinstance(targets, list):
        return {}
    lookup: dict[str, dict[str, object]] = {}
    for target in targets:
        if not isinstance(target, dict):
            continue
        archive_path = normalize_path_stem(str(target.get("archive_path") or ""))
        target_cmb = normalize_resource_name(str(target.get("target_cmb_name") or ""))
        if csab_target_archive and archive_path != normalize_path_stem(csab_target_archive):
            continue
        if csab_target_cmb and target_cmb != normalize_resource_name(csab_target_cmb):
            continue
        for animation in target.get("animations", []):
            if not isinstance(animation, dict):
                continue
            csab_name = normalize_resource_name(str(animation.get("csab_name") or ""))
            stem = path_stem(csab_name)
            if not stem:
                continue
            lookup[stem] = {
                "csab_name": csab_name,
                "target_archive": target.get("archive_path"),
                "target_cmb": target.get("target_cmb_name"),
                "frame_slot_count": animation.get("frame_slot_count"),
                "target_resolution_status": animation.get("target_resolution_status"),
                "target_support_status": animation.get("target_support_status"),
            }
    return lookup


def attach_csab_lookup(record: dict[str, object], csab_lookup: dict[str, dict[str, object]]) -> None:
    stem = str(record.get("path_stem") or "")
    match = csab_lookup.get(stem)
    if match is not None:
        record["csab_lookup"] = {
            "status": "matched",
            "match_kind": "exact_stem",
            "matched_stem": stem,
            **match,
        }
        return

    normalized_leaf = normalized_animation_leaf(stem)
    alias_matches = [
        (candidate_stem, candidate)
        for candidate_stem, candidate in csab_lookup.items()
        if normalized_animation_leaf(candidate_stem) == normalized_leaf
    ]
    if len(alias_matches) == 1:
        matched_stem, alias_match = alias_matches[0]
        record["csab_lookup"] = {
            "status": "matched",
            "match_kind": "normalized_leaf_unique",
            "matched_stem": matched_stem,
            "anb_normalized_leaf": normalized_leaf,
            **alias_match,
        }
        return

    semantic_aliases = semantic_animation_leaf_aliases(stem)
    semantic_alias_matches: list[tuple[str, dict[str, object], str]] = []
    for candidate_stem, candidate in csab_lookup.items():
        candidate_leaf = path_stem(candidate_stem).rsplit("/", 1)[-1]
        matching_aliases = [alias for alias in semantic_aliases if alias == candidate_leaf]
        if matching_aliases:
            semantic_alias_matches.append((candidate_stem, candidate, matching_aliases[0]))
    if len(semantic_alias_matches) == 1:
        matched_stem, alias_match, matched_alias = semantic_alias_matches[0]
        record["csab_lookup"] = {
            "status": "matched",
            "match_kind": "semantic_alias_unique",
            "matched_stem": matched_stem,
            "anb_normalized_leaf": normalized_leaf,
            "matched_semantic_alias": matched_alias,
            **alias_match,
        }
        return

    record["csab_lookup"] = {
        "status": "unmatched",
        "csab_name": None,
        "anb_normalized_leaf": normalized_leaf,
        "normalized_leaf_candidate_count": len(alias_matches),
        "semantic_alias_candidate_count": len(semantic_alias_matches),
        "semantic_alias_candidate_stems": [
            candidate_stem for candidate_stem, _, _ in semantic_alias_matches[:20]
        ],
    }


def summarize_csab_lookup(
    records: list[dict[str, object]],
    csab_lookup: dict[str, dict[str, object]],
) -> dict[str, object]:
    matched_records = [
        record
        for record in records
        if isinstance(record.get("csab_lookup"), dict)
        and record["csab_lookup"].get("status") == "matched"
    ]
    frame_count_matches = [
        record for record in matched_records if anb_csab_frame_count_matches(record)
    ]
    frame_count_mismatches = [
        record for record in matched_records if not anb_csab_frame_count_matches(record)
    ]
    matched_stems = {str(record.get("path_stem") or "") for record in matched_records}
    matched_csab_stems = {
        path_stem(str(record.get("csab_lookup", {}).get("csab_name") or ""))
        for record in matched_records
    }
    match_kinds = Counter(str(record.get("csab_lookup", {}).get("match_kind") or "unknown") for record in matched_records)
    unmatched_records = [
        record
        for record in records
        if isinstance(record.get("csab_lookup"), dict)
        and record["csab_lookup"].get("status") != "matched"
    ]
    return {
        "status": "provided" if csab_lookup else "not_provided",
        "csab_stem_count": len(csab_lookup),
        "anb_stem_count": len({str(record.get("path_stem") or "") for record in records}),
        "matched_anb_count": len(matched_records),
        "matched_stem_count": len(matched_stems),
        "matched_csab_stem_count": len(matched_csab_stems),
        "match_kind_counts": sorted_counter(match_kinds),
        "matched_frame_count_status_count": len(frame_count_matches),
        "mismatched_frame_count_status_count": len(frame_count_mismatches),
        "unmatched_anb_count": len(unmatched_records),
        "csab_without_anb_count": len(set(csab_lookup) - matched_csab_stems),
        "matched_records": [
            {
                "embedded_name": record.get("embedded_name"),
                "path_stem": record.get("path_stem"),
                "csab_name": record.get("csab_lookup", {}).get("csab_name"),
                "match_kind": record.get("csab_lookup", {}).get("match_kind"),
                "matched_stem": record.get("csab_lookup", {}).get("matched_stem"),
                "frame_count_candidate": record.get("metadata", {}).get("frame_count_candidate"),
                "csab_frame_slot_count": record.get("csab_lookup", {}).get("frame_slot_count"),
            }
            for record in matched_records
        ],
        "sample_unmatched_anb": [
            {
                "embedded_name": record.get("embedded_name"),
                "path_stem": record.get("path_stem"),
            }
            for record in unmatched_records[:20]
        ],
        "sample_frame_count_mismatches": [
            {
                "embedded_name": record.get("embedded_name"),
                "path_stem": record.get("path_stem"),
                "csab_name": record.get("csab_lookup", {}).get("csab_name"),
                "frame_count_candidate": record.get("metadata", {}).get("frame_count_candidate"),
                "csab_frame_slot_count": record.get("csab_lookup", {}).get("frame_slot_count"),
            }
            for record in frame_count_mismatches[:20]
        ],
    }


def anb_csab_frame_count_matches(record: dict[str, object]) -> bool:
    metadata = record.get("metadata", {})
    lookup = record.get("csab_lookup", {})
    if not isinstance(metadata, dict) or not isinstance(lookup, dict):
        return False
    return metadata.get("frame_count_candidate") == lookup.get("frame_slot_count")


def path_stem(name: str) -> str:
    return normalize_resource_name(name).rsplit(".", 1)[0]


def normalize_resource_name(name: str) -> str:
    return name.replace("\\", "/").lower()


def normalize_path_stem(name: str) -> str:
    return Path(name.replace("\\", "/")).name.lower()


def normalized_animation_leaf(name: str) -> str:
    leaf = path_stem(name).rsplit("/", 1)[-1]
    for prefix in ("clink_", "link_", "child_", "kolink_"):
        if leaf.startswith(prefix):
            return leaf[len(prefix):]
    return leaf


SEMANTIC_ANIMATION_PREFIX_ALIASES = (
    ("clink_demo_", "cl_dm_"),
    ("clink_normal_", "cl_nml_"),
    ("clink_op3_", "cl_op3_"),
    ("d_link_", "d_lk_"),
    ("demo_link_", "dm_lk_"),
    ("link_anchor_", "ac_"),
    ("link_bottle_", "bt_"),
    ("link_demo_", "dm_"),
    ("link_drink_demo_", "drink_dm_"),
    ("link_fighter_", "ft_"),
    ("link_hammer_", "hm_"),
    ("link_magic_", "mg_"),
    ("link_normal_", "nml_"),
    ("link_swimer_", "sw_"),
    ("link_swim_", "sw_"),
)


SEMANTIC_ANIMATION_TOKEN_ALIASES = (
    ("finsh", "fin"),
    ("normal", "nml"),
    ("fighter", "ft"),
    ("power", "pow"),
    ("drink_demo", "drink_dm"),
    ("kakeyori", "runup"),
)


def semantic_animation_leaf_aliases(name: str) -> list[str]:
    leaf = path_stem(name).rsplit("/", 1)[-1]
    aliases: list[str] = []
    for prefix, replacement in SEMANTIC_ANIMATION_PREFIX_ALIASES:
        if leaf.startswith(prefix):
            append_semantic_animation_alias(aliases, replacement + leaf[len(prefix):])
    expand_semantic_animation_aliases(aliases)
    return aliases


def expand_semantic_animation_aliases(aliases: list[str]) -> None:
    cursor = 0
    while cursor < len(aliases):
        alias = aliases[cursor]
        cursor += 1
        for before, after in SEMANTIC_ANIMATION_TOKEN_ALIASES:
            if before in alias:
                append_semantic_animation_alias(aliases, alias.replace(before, after))
        if "_kiru_" in alias:
            append_semantic_animation_alias(aliases, alias.replace("_kiru_", "_"))
        if alias.startswith("ac_bom_"):
            suffix = alias[len("ac_bom_"):]
            append_semantic_animation_alias(aliases, "boom_throw_" + suffix)
            append_semantic_animation_alias(aliases, "boom_" + suffix)
        if alias.startswith("anchor_bom_"):
            suffix = alias[len("anchor_bom_"):]
            append_semantic_animation_alias(aliases, "boom_throw_" + suffix)
            append_semantic_animation_alias(aliases, "boom_" + suffix)


def append_semantic_animation_alias(aliases: list[str], alias: str) -> None:
    if alias and alias not in aliases:
        aliases.append(alias)


def sample_record(record: dict[str, object]) -> dict[str, object]:
    return {
        "embedded_name": record["embedded_name"],
        "path_stem": record["path_stem"],
        "embedded_index": record["embedded_index"],
        "size": record["size"],
        "sha256": record["sha256"],
        "duplicate_matches": record["duplicate_matches"],
        "csab_lookup": record["csab_lookup"],
        "metadata": record["metadata"],
        "payload_word_count": record["payload_word_count"],
        "trailing_byte_count": record["trailing_byte_count"],
        "first_payload_words": record["payload_words"][:16],
        "trailing_bytes_hex": record["trailing_bytes_hex"],
    }


def s16_payload_samples(payload: bytes) -> list[int]:
    if len(payload) % 2 != 0:
        raise ParseError("ANB payload frame table has odd byte length")
    return [
        int.from_bytes(payload[offset: offset + 2], "little", signed=True)
        for offset in range(0, len(payload), 2)
    ]


def sampled_s16_frames(samples: list[int], frame_count: int) -> list[dict[str, object]]:
    if frame_count <= 0:
        return []
    frame_indices = sorted({0, frame_count // 2, frame_count - 1})
    sampled: list[dict[str, object]] = []
    for frame_index in frame_indices:
        start = frame_index * ANB_FRAME_CHANNEL_COUNT_CANDIDATE
        end = start + ANB_FRAME_CHANNEL_COUNT_CANDIDATE
        frame_samples = samples[start:end]
        sampled.append(
            {
                "frame": frame_index,
                "channel_count": len(frame_samples),
                "channels_s16": frame_samples,
            }
        )
    return sampled


def record_s16_samples(record: dict[str, object]) -> list[int]:
    payload = bytearray()
    for word in record.get("payload_words", []):
        payload += int(word).to_bytes(4, "little")
    trailing_bytes_hex = str(record.get("trailing_bytes_hex") or "")
    if trailing_bytes_hex:
        payload += bytes.fromhex(trailing_bytes_hex)
    return s16_payload_samples(bytes(payload))


def summarize_record_channels(samples: list[int]) -> dict[str, object]:
    if len(samples) % ANB_FRAME_CHANNEL_COUNT_CANDIDATE != 0:
        raise ParseError("ANB decoded sample count does not align to frame channel count")
    active_channel_count = 0
    static_channel_count = 0
    for channel_index in range(ANB_FRAME_CHANNEL_COUNT_CANDIDATE):
        channel_samples = samples[channel_index::ANB_FRAME_CHANNEL_COUNT_CANDIDATE]
        if len(set(channel_samples)) <= 1:
            static_channel_count += 1
        else:
            active_channel_count += 1
    return {
        "active_channel_count": active_channel_count,
        "static_channel_count": static_channel_count,
        "sample_min": min(samples) if samples else None,
        "sample_max": max(samples) if samples else None,
    }


def new_channel_aggregate() -> dict[str, object]:
    return {
        "channel_min": [None for _ in range(ANB_FRAME_CHANNEL_COUNT_CANDIDATE)],
        "channel_max": [None for _ in range(ANB_FRAME_CHANNEL_COUNT_CANDIDATE)],
        "channel_nonzero_sample_count": [0 for _ in range(ANB_FRAME_CHANNEL_COUNT_CANDIDATE)],
        "channel_distinct_samples": [set() for _ in range(ANB_FRAME_CHANNEL_COUNT_CANDIDATE)],
        "active_channel_count_by_record": Counter(),
        "static_channel_count_by_record": Counter(),
        "record_active_channel_counts": [],
        "record_static_channel_counts": [],
        "sample_min": None,
        "sample_max": None,
        "total_frame_count": 0,
    }


def update_channel_aggregate(
    aggregate: dict[str, object],
    samples: list[int],
    record_channel_summary: dict[str, object],
) -> None:
    total_frame_count = len(samples) // ANB_FRAME_CHANNEL_COUNT_CANDIDATE
    aggregate["total_frame_count"] = int(aggregate["total_frame_count"]) + total_frame_count
    aggregate["active_channel_count_by_record"][str(record_channel_summary["active_channel_count"])] += 1
    aggregate["static_channel_count_by_record"][str(record_channel_summary["static_channel_count"])] += 1
    aggregate["record_active_channel_counts"].append(int(record_channel_summary["active_channel_count"]))
    aggregate["record_static_channel_counts"].append(int(record_channel_summary["static_channel_count"]))
    sample_min = aggregate["sample_min"]
    sample_max = aggregate["sample_max"]
    for sample in samples:
        if sample_min is None or sample < sample_min:
            sample_min = sample
        if sample_max is None or sample > sample_max:
            sample_max = sample
    aggregate["sample_min"] = sample_min
    aggregate["sample_max"] = sample_max

    channel_min = aggregate["channel_min"]
    channel_max = aggregate["channel_max"]
    channel_nonzero_sample_count = aggregate["channel_nonzero_sample_count"]
    channel_distinct_samples = aggregate["channel_distinct_samples"]
    for channel_index in range(ANB_FRAME_CHANNEL_COUNT_CANDIDATE):
        for sample in samples[channel_index::ANB_FRAME_CHANNEL_COUNT_CANDIDATE]:
            if channel_min[channel_index] is None or sample < channel_min[channel_index]:
                channel_min[channel_index] = sample
            if channel_max[channel_index] is None or sample > channel_max[channel_index]:
                channel_max[channel_index] = sample
            if sample != 0:
                channel_nonzero_sample_count[channel_index] += 1
            channel_distinct_samples[channel_index].add(sample)


def finalize_channel_aggregate(aggregate: dict[str, object]) -> dict[str, object]:
    channel_distinct_samples = aggregate["channel_distinct_samples"]
    channel_nonzero_sample_count = aggregate["channel_nonzero_sample_count"]
    total_frame_count = int(aggregate["total_frame_count"])
    channel_bounds = []
    always_zero_channel_indices = []
    never_zero_channel_indices = []
    varying_channel_indices = []
    for channel_index in range(ANB_FRAME_CHANNEL_COUNT_CANDIDATE):
        distinct_count = len(channel_distinct_samples[channel_index])
        nonzero_count = int(channel_nonzero_sample_count[channel_index])
        if distinct_count > 1:
            varying_channel_indices.append(channel_index)
        if nonzero_count == 0:
            always_zero_channel_indices.append(channel_index)
        if nonzero_count == total_frame_count:
            never_zero_channel_indices.append(channel_index)
        channel_bounds.append(
            {
                "channel_index": channel_index,
                "min": aggregate["channel_min"][channel_index],
                "max": aggregate["channel_max"][channel_index],
                "distinct_sample_count": distinct_count,
                "nonzero_sample_count": nonzero_count,
            }
        )
    active_counts = aggregate["record_active_channel_counts"]
    static_counts = aggregate["record_static_channel_counts"]
    return {
        "channel_count": ANB_FRAME_CHANNEL_COUNT_CANDIDATE,
        "sample_min": aggregate["sample_min"],
        "sample_max": aggregate["sample_max"],
        "varying_channel_count": len(varying_channel_indices),
        "varying_channel_indices": varying_channel_indices,
        "always_zero_channel_count": len(always_zero_channel_indices),
        "always_zero_channel_indices": always_zero_channel_indices,
        "never_zero_channel_count": len(never_zero_channel_indices),
        "never_zero_channel_indices": never_zero_channel_indices,
        "record_active_channel_count_min": min(active_counts) if active_counts else None,
        "record_active_channel_count_max": max(active_counts) if active_counts else None,
        "record_static_channel_count_min": min(static_counts) if static_counts else None,
        "record_static_channel_count_max": max(static_counts) if static_counts else None,
        "active_channel_count_by_record": sorted_counter(aggregate["active_channel_count_by_record"]),
        "static_channel_count_by_record": sorted_counter(aggregate["static_channel_count_by_record"]),
        "channel_bounds": channel_bounds,
    }


def is_anb_file(file: ZarFile) -> bool:
    return file.type_name == "anb" or file.name.lower().endswith(".anb")


def add_issue(issues: list[dict[str, object]], sample_limit: int, reason: str, embedded_name: str) -> None:
    if len(issues) >= sample_limit:
        return
    issues.append({"reason": reason, "embedded_name": embedded_name})
