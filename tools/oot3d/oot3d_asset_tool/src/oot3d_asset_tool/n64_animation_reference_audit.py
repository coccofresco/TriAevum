from __future__ import annotations

from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import xml.etree.ElementTree as ET
import zipfile

from .binary import ParseError
from .romfs_inventory import sorted_counter


N64_ANIMATION_REFERENCE_AUDIT_FORMAT = "oot3d_n64_animation_reference_audit_v1"
N64_PLAYER_ANIMATION_ROTATION_TRIPLETS_PER_FRAME = 22
N64_PLAYER_ANIMATION_BYTES_PER_FRAME = 6 * N64_PLAYER_ANIMATION_ROTATION_TRIPLETS_PER_FRAME + 2
N64_PLAYER_ANIMATION_S16_VALUES_PER_FRAME = N64_PLAYER_ANIMATION_BYTES_PER_FRAME // 2
N64_PLAYER_LIMB_NAMES = [
    "PLAYER_LIMB_NONE",
    "PLAYER_LIMB_ROOT",
    "PLAYER_LIMB_WAIST",
    "PLAYER_LIMB_LOWER",
    "PLAYER_LIMB_R_THIGH",
    "PLAYER_LIMB_R_SHIN",
    "PLAYER_LIMB_R_FOOT",
    "PLAYER_LIMB_L_THIGH",
    "PLAYER_LIMB_L_SHIN",
    "PLAYER_LIMB_L_FOOT",
    "PLAYER_LIMB_UPPER",
    "PLAYER_LIMB_HEAD",
    "PLAYER_LIMB_HAT",
    "PLAYER_LIMB_COLLAR",
    "PLAYER_LIMB_L_SHOULDER",
    "PLAYER_LIMB_L_FOREARM",
    "PLAYER_LIMB_L_HAND",
    "PLAYER_LIMB_R_SHOULDER",
    "PLAYER_LIMB_R_FOREARM",
    "PLAYER_LIMB_R_HAND",
    "PLAYER_LIMB_SHEATH",
    "PLAYER_LIMB_TORSO",
]
SHIPWRIGHT_BINARY_RESOURCE_HEADER_SIZE = 64
SHIPWRIGHT_PLAYER_ANIMATION_ENTRY_PREFIX_SIZE = SHIPWRIGHT_BINARY_RESOURCE_HEADER_SIZE + 4


class O2RBinaryReader:
    """Small reader for Shipwright binary resource bodies after the O2R header."""

    def __init__(self, data: bytes, offset: int = SHIPWRIGHT_BINARY_RESOURCE_HEADER_SIZE) -> None:
        self.data = data
        self.offset = offset

    def _read(self, fmt: str) -> object:
        size = struct.calcsize(fmt)
        if self.offset + size > len(self.data):
            raise ParseError("binary resource ended before expected field")
        value = struct.unpack_from(fmt, self.data, self.offset)[0]
        self.offset += size
        return value

    def i8(self) -> int:
        return int(self._read("<b"))

    def u8(self) -> int:
        return int(self._read("<B"))

    def i16(self) -> int:
        return int(self._read("<h"))

    def u16(self) -> int:
        return int(self._read("<H"))

    def i32(self) -> int:
        return int(self._read("<i"))

    def u32(self) -> int:
        return int(self._read("<I"))

    def f32(self) -> float:
        return float(self._read("<f"))

    def string(self) -> str:
        length = self.i32()
        if length < 0:
            raise ParseError("binary resource string has negative length")
        if self.offset + length > len(self.data):
            raise ParseError("binary resource ended inside string")
        raw = self.data[self.offset : self.offset + length]
        self.offset += length
        return raw.decode("utf-8", errors="replace")


def audit_n64_animation_reference(
    skinned_binding_manifest_path: Path,
    n64_player_animation_xml_path: Path,
    n64_link_object_xml_path: Path,
    output_path: Path | None = None,
    *,
    n64_player_animation_data_xml_path: Path | None = None,
    n64_base_o2r_path: Path | None = None,
    oot3d_pose_batch_manifest_path: Path | None = None,
    profile_id: str,
    model_archive: str,
    model_cmb: str,
    n64_skeleton_name: str | None = None,
    oot3d_strip_prefixes: list[str] | None = None,
    n64_strip_prefixes: list[str] | None = None,
    reference_version: str = "N64_NTSC_12",
    sample_limit: int = 25,
) -> dict[str, object]:
    """Build an explicit N64 animation reference candidate map for an OOT3D skinned target."""

    required_paths = [
        (skinned_binding_manifest_path, "skinned animation binding manifest"),
        (n64_player_animation_xml_path, "N64 player animation XML"),
        (n64_link_object_xml_path, "N64 Link object XML"),
    ]
    if n64_player_animation_data_xml_path is not None:
        required_paths.append((n64_player_animation_data_xml_path, "N64 player animation data XML"))
    if n64_base_o2r_path is not None:
        required_paths.append((n64_base_o2r_path, "N64 base O2R archive"))
    if oot3d_pose_batch_manifest_path is not None:
        required_paths.append((oot3d_pose_batch_manifest_path, "OOT3D pose batch manifest"))
    for path, label in required_paths:
        if not path.is_file():
            raise ParseError(f"{path}: {label} not found")

    binding = load_json(skinned_binding_manifest_path)
    pose_batch = load_json(oot3d_pose_batch_manifest_path) if oot3d_pose_batch_manifest_path is not None else None
    target = find_target(binding, model_archive, model_cmb)
    oot3d_records = oot3d_animation_records(target, oot3d_strip_prefixes or [])
    player_animation_data_records = (
        n64_player_animation_data_records(n64_player_animation_data_xml_path)
        if n64_player_animation_data_xml_path is not None
        else None
    )
    payload_decode_summary = decode_player_animation_payloads(
        n64_base_o2r_path,
        player_animation_data_records,
        sample_limit,
    )
    player_animation_records = n64_player_animation_records(
        n64_player_animation_xml_path,
        n64_strip_prefixes or [],
        player_animation_data_records,
    )
    skeleton_summary = n64_link_skeleton_summary(n64_link_object_xml_path, n64_skeleton_name)
    skeleton_pose_reference = decode_n64_skeleton_pose_reference(n64_base_o2r_path, skeleton_summary, sample_limit)
    candidate_mapping = map_animation_candidates(oot3d_records, player_animation_records, sample_limit)
    limb_mapping = n64_oot3d_limb_mapping_analysis(target, skeleton_summary, sample_limit)
    pose_sample_comparison = time_normalized_pose_sample_comparison(
        oot3d_records,
        player_animation_records,
        pose_batch,
        oot3d_pose_batch_manifest_path,
        n64_base_o2r_path,
        model_archive,
        model_cmb,
        sample_limit,
    )
    pose_error_metric = time_normalized_pose_error_metric(
        oot3d_records,
        player_animation_records,
        pose_batch,
        oot3d_pose_batch_manifest_path,
        n64_base_o2r_path,
        model_archive,
        model_cmb,
        limb_mapping,
        skeleton_pose_reference,
        sample_limit,
    )
    blockers = reference_blockers(
        target,
        skeleton_summary,
        player_animation_records,
        player_animation_data_records,
        payload_decode_summary,
        pose_sample_comparison,
        limb_mapping,
        pose_error_metric,
        candidate_mapping,
    )
    validation_scope_status = (
        validation_scope_status_for(
            player_animation_data_records,
            payload_decode_summary,
            pose_sample_comparison,
            limb_mapping,
            pose_error_metric,
        )
    )

    audit: dict[str, object] = {
        "format": N64_ANIMATION_REFERENCE_AUDIT_FORMAT,
        "profile_id": profile_id,
        "reference_version": reference_version,
        "source_files": {
            "skinned_animation_binding": str(skinned_binding_manifest_path),
            "n64_player_animation_xml": str(n64_player_animation_xml_path),
            "n64_link_object_xml": str(n64_link_object_xml_path),
            "n64_player_animation_data_xml": (
                str(n64_player_animation_data_xml_path)
                if n64_player_animation_data_xml_path is not None
                else None
            ),
            "n64_base_o2r": str(n64_base_o2r_path) if n64_base_o2r_path is not None else None,
            "oot3d_pose_batch_manifest": (
                str(oot3d_pose_batch_manifest_path)
                if oot3d_pose_batch_manifest_path is not None
                else None
            ),
        },
        "normalization_policy": {
            "source": "declared_prefix_stripping_only",
            "oot3d_strip_prefixes": oot3d_strip_prefixes or [],
            "n64_strip_prefixes": n64_strip_prefixes or [],
            "case": "lower",
            "path_policy": "use_basename_without_extension",
            "alias_policy": "none",
        },
        "oot3d_target": oot3d_target_summary(target, model_archive, model_cmb),
        "n64_reference": {
            "skeleton": skeleton_summary,
            "player_animation_count": len(player_animation_records),
            "player_animation_data_count": (
                len(player_animation_data_records)
                if player_animation_data_records is not None
                else 0
            ),
            "player_animation_data_count_status": player_animation_data_count_status(
                player_animation_records,
                player_animation_data_records,
            ),
            "player_animation_data_summary": player_animation_data_summary(player_animation_data_records),
            "player_animation_payload_decode": payload_decode_summary,
            "skeleton_pose_reference": skeleton_pose_reference,
            "sample_player_animations": player_animation_records[:sample_limit],
        },
        "candidate_mapping": candidate_mapping,
        "limb_mapping": limb_mapping,
        "time_normalized_pose_samples": pose_sample_comparison,
        "pose_error_metric": pose_error_metric,
        "validation_scope": {
            "status": validation_scope_status,
            "proves": [
                "the N64 Link child skeleton reference is present in the selected Shipwright XML",
                "the N64 PlayerAnimation reference inventory is present",
                "OOT3D CSAB tracks have deterministic name-based N64 candidate records where names agree",
                "N64 PlayerAnimationData frame counts and expected payload sizes are available"
                if player_animation_data_records is not None
                else "N64 PlayerAnimationData frame counts were not requested",
                "N64 PlayerAnimationData payload values decode from the base O2R with expected s16 counts"
                if payload_decode_summary.get("status") == "decoded"
                else "N64 PlayerAnimationData payload values were not decoded",
                "time-normalized OOT3D pose sample frames are paired with decoded N64 frame-table samples"
                if pose_sample_comparison.get("status") == "sampled"
                else "time-normalized OOT3D/N64 pose sample pairs were not requested",
                "OOT3D CSAB bone coverage has a candidate mapping to N64 PlayerLimb frame-table slots"
                if limb_mapping.get("status") == "candidate_ready_for_metric"
                else "OOT3D/N64 limb mapping is not ready for the pose metric",
                "N64 skeleton bounds and OOT3D sampled mesh bounds have numeric time-normalized pose metrics"
                if pose_error_metric.get("status") == "measured"
                else "numeric OOT3D/N64 pose metrics have not been measured",
            ],
            "does_not_prove": [
                "pose equivalence",
                "timing equivalence after framerate normalization beyond declared frame counts",
                "per-limb orientation equivalence",
                "skinned deformation equivalence against N64 geometry",
            ],
            "next_gate": (
                "Calibrate acceptance thresholds for the measured skeleton-bounds pose metric, then extend "
                "the comparison to per-bone orientation and skinned deformation samples."
            ),
        },
        "blocker_counts": sorted_counter(blockers),
    }

    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8", newline="\n")
    return audit


def load_json(path: Path) -> dict[str, object]:
    return json.loads(path.read_text(encoding="utf-8"))


def load_xml(path: Path) -> ET.Element:
    try:
        return ET.parse(path).getroot()
    except ET.ParseError as exc:
        raise ParseError(f"{path}: XML parse error: {exc}") from exc


def normalize_path(value: object) -> str:
    return str(value).replace("\\", "/").strip("/")


def parse_int(value: str | None) -> int | None:
    if value is None:
        return None
    text = value.strip()
    if not text:
        return None
    try:
        return int(text, 0)
    except ValueError:
        return None


def find_target(
    skinned_binding: dict[str, object],
    model_archive: str,
    model_cmb: str,
) -> dict[str, object] | None:
    archive = normalize_path(model_archive)
    cmb = normalize_path(model_cmb)
    for target in skinned_binding.get("targets", []):
        if not isinstance(target, dict):
            continue
        if normalize_path(target.get("archive_path")) == archive and normalize_path(target.get("target_cmb_name")) == cmb:
            return target
    return None


def oot3d_target_summary(
    target: dict[str, object] | None,
    model_archive: str,
    model_cmb: str,
) -> dict[str, object]:
    if target is None:
        return {
            "status": "missing",
            "archive_path": normalize_path(model_archive),
            "target_cmb_name": normalize_path(model_cmb),
            "animation_count": 0,
            "frame_slot_min": 0,
            "frame_slot_max": 0,
        }
    frame_slots = [
        int(animation.get("frame_slot_count") or 0)
        for animation in target.get("animations", [])
        if isinstance(animation, dict) and int(animation.get("frame_slot_count") or 0) > 0
    ]
    return {
        "status": "resolved",
        "target_id": target.get("target_id"),
        "archive_path": normalize_path(target.get("archive_path")),
        "target_cmb_name": normalize_path(target.get("target_cmb_name")),
        "model_name": target.get("model_name"),
        "bone_count": target.get("bone_count"),
        "animation_count": len([a for a in target.get("animations", []) if isinstance(a, dict)]),
        "frame_slot_min": min(frame_slots, default=0),
        "frame_slot_max": max(frame_slots, default=0),
    }


def oot3d_animation_records(
    target: dict[str, object] | None,
    strip_prefixes: list[str],
) -> list[dict[str, object]]:
    if target is None:
        return []
    records: list[dict[str, object]] = []
    for animation in target.get("animations", []):
        if not isinstance(animation, dict):
            continue
        csab_name = normalize_path(animation.get("csab_name"))
        stem = path_stem(csab_name)
        records.append(
            {
                "csab_name": csab_name,
                "stem": stem,
                "normalized_key": canonical_animation_key(stem, strip_prefixes),
                "frame_slot_count": animation.get("frame_slot_count"),
                "target_resolution_status": animation.get("target_resolution_status"),
                "target_support_status": animation.get("target_support_status"),
            }
        )
    return records


def n64_player_animation_records(
    xml_path: Path,
    strip_prefixes: list[str],
    data_records: list[dict[str, object]] | None = None,
) -> list[dict[str, object]]:
    root = load_xml(xml_path)
    records: list[dict[str, object]] = []
    for element in root.iter():
        if element.tag != "PlayerAnimation":
            continue
        name = element.attrib.get("Name", "")
        stem = strip_n64_player_anim_prefix(name)
        index = len(records)
        record = {
            "index": index,
            "name": name,
            "offset": parse_int(element.attrib.get("Offset")),
            "stem": stem,
            "normalized_key": canonical_animation_key(stem, strip_prefixes),
        }
        if data_records is not None:
            data_record = data_records[index] if index < len(data_records) else None
            record.update(player_animation_data_fields(data_record))
        records.append(record)
    return records


def n64_player_animation_data_records(xml_path: Path) -> list[dict[str, object]]:
    root = load_xml(xml_path)
    records: list[dict[str, object]] = []
    for element in root.iter():
        if element.tag != "PlayerAnimationData":
            continue
        frame_count = parse_int(element.attrib.get("FrameCount"))
        expected_payload_byte_count = (
            N64_PLAYER_ANIMATION_BYTES_PER_FRAME * frame_count
            if frame_count is not None
            else None
        )
        records.append(
            {
                "index": len(records),
                "data_name": element.attrib.get("Name", ""),
                "data_offset": parse_int(element.attrib.get("Offset")),
                "frame_count": frame_count,
                "bytes_per_frame": N64_PLAYER_ANIMATION_BYTES_PER_FRAME,
                "s16_values_per_frame": N64_PLAYER_ANIMATION_S16_VALUES_PER_FRAME,
                "expected_payload_byte_count": expected_payload_byte_count,
                "expected_limb_rot_s16_count": (
                    N64_PLAYER_ANIMATION_S16_VALUES_PER_FRAME * frame_count
                    if frame_count is not None
                    else None
                ),
            }
        )

    for index, record in enumerate(records[:-1]):
        current_offset = record.get("data_offset")
        next_offset = records[index + 1].get("data_offset")
        expected_payload_byte_count = record.get("expected_payload_byte_count")
        if (
            isinstance(current_offset, int)
            and isinstance(next_offset, int)
            and isinstance(expected_payload_byte_count, int)
        ):
            next_data_offset_delta = next_offset - current_offset
            record["next_data_offset_delta"] = next_data_offset_delta
            record["padding_to_next_data"] = next_data_offset_delta - expected_payload_byte_count
        else:
            record["next_data_offset_delta"] = None
            record["padding_to_next_data"] = None
    if records:
        records[-1]["next_data_offset_delta"] = None
        records[-1]["padding_to_next_data"] = None
    return records


def player_animation_data_fields(data_record: dict[str, object] | None) -> dict[str, object]:
    if data_record is None:
        return {
            "data_name": None,
            "data_offset": None,
            "frame_count": None,
            "bytes_per_frame": N64_PLAYER_ANIMATION_BYTES_PER_FRAME,
            "s16_values_per_frame": N64_PLAYER_ANIMATION_S16_VALUES_PER_FRAME,
            "expected_payload_byte_count": None,
            "expected_limb_rot_s16_count": None,
            "next_data_offset_delta": None,
            "padding_to_next_data": None,
            "payload_decode": None,
        }
    return {
        "data_name": data_record.get("data_name"),
        "data_offset": data_record.get("data_offset"),
        "frame_count": data_record.get("frame_count"),
        "bytes_per_frame": data_record.get("bytes_per_frame"),
        "s16_values_per_frame": data_record.get("s16_values_per_frame"),
        "expected_payload_byte_count": data_record.get("expected_payload_byte_count"),
        "expected_limb_rot_s16_count": data_record.get("expected_limb_rot_s16_count"),
        "next_data_offset_delta": data_record.get("next_data_offset_delta"),
        "padding_to_next_data": data_record.get("padding_to_next_data"),
        "payload_decode": data_record.get("payload_decode"),
    }


def player_animation_data_count_status(
    player_animation_records: list[dict[str, object]],
    data_records: list[dict[str, object]] | None,
) -> str:
    if data_records is None:
        return "not_provided"
    if len(player_animation_records) == len(data_records):
        return "matched_by_xml_order"
    if len(player_animation_records) > len(data_records):
        return "missing_player_animation_data_records"
    return "extra_player_animation_data_records"


def player_animation_data_summary(data_records: list[dict[str, object]] | None) -> dict[str, object]:
    if data_records is None:
        return {
            "status": "not_provided",
            "frame_count_min": 0,
            "frame_count_max": 0,
            "frame_count_total": 0,
            "expected_payload_byte_total": 0,
            "expected_limb_rot_s16_total": 0,
            "padding_to_next_data_counts": {},
        }
    frame_counts = [
        int(record["frame_count"])
        for record in data_records
        if isinstance(record.get("frame_count"), int)
    ]
    payload_byte_counts = [
        int(record["expected_payload_byte_count"])
        for record in data_records
        if isinstance(record.get("expected_payload_byte_count"), int)
    ]
    limb_rot_s16_counts = [
        int(record["expected_limb_rot_s16_count"])
        for record in data_records
        if isinstance(record.get("expected_limb_rot_s16_count"), int)
    ]
    padding_counts: Counter[str] = Counter()
    for record in data_records:
        padding = record.get("padding_to_next_data")
        if isinstance(padding, int):
            padding_counts[str(padding)] += 1
    return {
        "status": "resolved",
        "bytes_per_frame": N64_PLAYER_ANIMATION_BYTES_PER_FRAME,
        "s16_values_per_frame": N64_PLAYER_ANIMATION_S16_VALUES_PER_FRAME,
        "frame_count_min": min(frame_counts, default=0),
        "frame_count_max": max(frame_counts, default=0),
        "frame_count_total": sum(frame_counts),
        "expected_payload_byte_total": sum(payload_byte_counts),
        "expected_limb_rot_s16_total": sum(limb_rot_s16_counts),
        "padding_to_next_data_counts": dict(sorted(padding_counts.items(), key=lambda item: int(item[0]))),
    }


def decode_player_animation_payloads(
    o2r_path: Path | None,
    data_records: list[dict[str, object]] | None,
    sample_limit: int,
) -> dict[str, object]:
    if o2r_path is None:
        return {
            "status": "not_provided",
            "o2r_path": None,
            "decoded_count": 0,
            "missing_resource_count": 0,
            "count_mismatch_count": 0,
            "invalid_resource_count": 0,
            "issue_count": 0,
            "sample_decoded_payloads": [],
            "sample_issues": [],
        }
    if data_records is None:
        return {
            "status": "missing_player_animation_data_records",
            "o2r_path": str(o2r_path),
            "decoded_count": 0,
            "missing_resource_count": 0,
            "count_mismatch_count": 0,
            "invalid_resource_count": 0,
            "issue_count": 1,
            "sample_decoded_payloads": [],
            "sample_issues": [{"type": "missing_player_animation_data_records"}],
        }

    decoded_records: list[dict[str, object]] = []
    issues: list[dict[str, object]] = []
    status_counts: Counter[str] = Counter()
    try:
        with zipfile.ZipFile(o2r_path) as archive:
            archive_names = set(archive.namelist())
            for record in data_records:
                decode = decode_player_animation_payload_record(archive, archive_names, record)
                record["payload_decode"] = decode
                status = str(decode.get("status", "invalid_resource"))
                status_counts[status] += 1
                if status == "decoded":
                    decoded_records.append(decode)
                else:
                    issues.append(decode)
    except zipfile.BadZipFile as exc:
        return {
            "status": "invalid_o2r_archive",
            "o2r_path": str(o2r_path),
            "decoded_count": 0,
            "missing_resource_count": 0,
            "count_mismatch_count": 0,
            "invalid_resource_count": 1,
            "issue_count": 1,
            "sample_decoded_payloads": [],
            "sample_issues": [{"type": "invalid_o2r_archive", "message": str(exc)}],
        }

    issue_count = len(data_records) - len(decoded_records)
    return {
        "status": "decoded" if issue_count == 0 else "incomplete",
        "o2r_path": str(o2r_path),
        "frame_ir": player_animation_frame_ir_summary(),
        "resource_header_size": SHIPWRIGHT_BINARY_RESOURCE_HEADER_SIZE,
        "entry_count_offset": SHIPWRIGHT_BINARY_RESOURCE_HEADER_SIZE,
        "payload_offset": SHIPWRIGHT_PLAYER_ANIMATION_ENTRY_PREFIX_SIZE,
        "decoded_count": len(decoded_records),
        "missing_resource_count": status_counts.get("missing_resource", 0),
        "count_mismatch_count": status_counts.get("count_mismatch", 0),
        "invalid_resource_count": sum(
            count
            for status, count in status_counts.items()
            if status not in ("decoded", "missing_resource", "count_mismatch")
        ),
        "issue_count": issue_count,
        "status_counts": sorted_counter(status_counts),
        "sample_decoded_payloads": decoded_records[:sample_limit],
        "sample_issues": issues[:sample_limit],
    }


def player_animation_frame_ir_summary() -> dict[str, object]:
    return {
        "source": "soh/src/code/z_skelanime.c AnimationContext_SetLoadFrame",
        "layout": "Vec3s[PLAYER_LIMB_MAX] + s16 trailing_value",
        "limb_count": len(N64_PLAYER_LIMB_NAMES),
        "vec3s_per_frame": N64_PLAYER_ANIMATION_ROTATION_TRIPLETS_PER_FRAME,
        "s16_values_per_frame": N64_PLAYER_ANIMATION_S16_VALUES_PER_FRAME,
        "bytes_per_frame": N64_PLAYER_ANIMATION_BYTES_PER_FRAME,
        "trailing_s16_per_frame": 1,
        "limb_names": N64_PLAYER_LIMB_NAMES,
    }


def decode_player_animation_payload_record(
    archive: zipfile.ZipFile,
    archive_names: set[str],
    record: dict[str, object],
) -> dict[str, object]:
    data_name = str(record.get("data_name") or "")
    resource_path = f"misc/link_animetion/{data_name}"
    base = {
        "data_name": data_name,
        "resource_path": resource_path,
        "expected_limb_rot_s16_count": record.get("expected_limb_rot_s16_count"),
        "expected_payload_byte_count": record.get("expected_payload_byte_count"),
        "frame_count": record.get("frame_count"),
    }
    if not data_name or resource_path not in archive_names:
        return {**base, "status": "missing_resource"}

    payload = archive.read(resource_path)
    if len(payload) < SHIPWRIGHT_PLAYER_ANIMATION_ENTRY_PREFIX_SIZE:
        return {**base, "status": "invalid_resource", "resource_size": len(payload), "reason": "too_short"}
    decoded_s16_count = struct.unpack_from("<I", payload, SHIPWRIGHT_BINARY_RESOURCE_HEADER_SIZE)[0]
    decoded_payload_byte_count = decoded_s16_count * 2
    payload_start = SHIPWRIGHT_PLAYER_ANIMATION_ENTRY_PREFIX_SIZE
    payload_end = payload_start + decoded_payload_byte_count
    if payload_end > len(payload):
        return {
            **base,
            "status": "invalid_resource",
            "resource_size": len(payload),
            "decoded_limb_rot_s16_count": decoded_s16_count,
            "decoded_payload_byte_count": decoded_payload_byte_count,
            "reason": "declared_count_exceeds_resource_size",
        }
    expected_s16_count = record.get("expected_limb_rot_s16_count")
    expected_payload_byte_count = record.get("expected_payload_byte_count")
    raw_payload = payload[payload_start:payload_end]
    status = "decoded"
    if decoded_s16_count != expected_s16_count or decoded_payload_byte_count != expected_payload_byte_count:
        status = "count_mismatch"
    return {
        **base,
        "status": status,
        "resource_size": len(payload),
        "decoded_limb_rot_s16_count": decoded_s16_count,
        "decoded_payload_byte_count": decoded_payload_byte_count,
        "payload_sha1": hashlib.sha1(raw_payload).hexdigest(),
        "frame_sample_signatures": frame_sample_signatures(raw_payload, record.get("frame_count")),
    }


def frame_sample_signatures(raw_payload: bytes, frame_count_value: object) -> list[dict[str, object]]:
    frame_count = int_or_none(frame_count_value)
    if frame_count is None or frame_count <= 0:
        return []
    frame_byte_count = N64_PLAYER_ANIMATION_BYTES_PER_FRAME
    indices = sorted({0, frame_count // 2, frame_count - 1})
    signatures: list[dict[str, object]] = []
    for frame_index in indices:
        start = frame_index * frame_byte_count
        end = start + frame_byte_count
        if end > len(raw_payload):
            continue
        frame_payload = raw_payload[start:end]
        values = [value[0] for value in struct.iter_unpack("<h", frame_payload)]
        limb_vec3s = frame_limb_vec3s(values)
        signatures.append(
            {
                "frame_index": frame_index,
                "s16_count": len(values),
                "min_s16": min(values, default=0),
                "max_s16": max(values, default=0),
                "sha1": hashlib.sha1(frame_payload).hexdigest(),
                "first_s16_values": values[:12],
                "limb_vec3s": limb_vec3s,
                "trailing_s16": values[-1] if values else None,
            }
        )
    return signatures


def frame_limb_vec3s(values: list[int]) -> list[dict[str, object]]:
    limbs: list[dict[str, object]] = []
    for index, limb_name in enumerate(N64_PLAYER_LIMB_NAMES):
        base = index * 3
        if base + 2 >= len(values):
            break
        limbs.append(
            {
                "index": index,
                "name": limb_name,
                "x": values[base],
                "y": values[base + 1],
                "z": values[base + 2],
            }
        )
    return limbs


def n64_oot3d_limb_mapping_analysis(
    target: dict[str, object] | None,
    skeleton_summary: dict[str, object],
    sample_limit: int,
) -> dict[str, object]:
    if target is None:
        return {
            "status": "missing_oot3d_target",
            "issue_count": 1,
            "issues": [{"type": "missing_oot3d_target"}],
            "mapping": [],
        }

    issues: list[dict[str, object]] = []
    target_bone_count = int_or_none(target.get("bone_count")) or 0
    bind_pose_path = bind_pose_export_path_for_target(target)
    bind_export: dict[str, object] | None = None
    if bind_pose_path is None or not bind_pose_path.is_file():
        issues.append({"type": "missing_oot3d_bind_pose_export", "path": str(bind_pose_path) if bind_pose_path else None})
    else:
        try:
            bind_export = load_json(bind_pose_path)
        except (OSError, json.JSONDecodeError) as exc:
            issues.append({"type": "invalid_oot3d_bind_pose_export", "path": str(bind_pose_path), "message": str(exc)})

    skeleton_bones = bind_export.get("skeleton_bones", []) if isinstance(bind_export, dict) else []
    if bind_export is not None and not isinstance(skeleton_bones, list):
        issues.append({"type": "missing_oot3d_skeleton_bones"})
        skeleton_bones = []

    geometry_usage = oot3d_bone_geometry_usage(bind_export)
    track_coverage = oot3d_track_bone_coverage(target, sample_limit)
    issues.extend(track_coverage["issues"])  # type: ignore[arg-type]

    n64_order = n64_player_limb_order(skeleton_summary)
    issues.extend(n64_order["issues"])  # type: ignore[arg-type]

    root_motion_bone = oot3d_root_motion_bone_candidate(track_coverage)
    if root_motion_bone is None:
        issues.append({"type": "missing_oot3d_root_motion_bone_candidate"})

    animation_count = int(track_coverage["animation_count"])
    auxiliary_bones = oot3d_auxiliary_animated_bones(track_coverage, geometry_usage, animation_count)
    unanimated_bones = [
        index
        for index in range(target_bone_count)
        if int(track_coverage["bone_presence_counts"].get(str(index), 0)) == 0  # type: ignore[index]
    ]
    candidate_player_bones = [
        index
        for index in sorted(int(key) for key in track_coverage["bone_presence_counts"].keys())  # type: ignore[index]
        if index != root_motion_bone and index not in auxiliary_bones
    ]
    required_non_root_slots = max(0, len(N64_PLAYER_LIMB_NAMES) - 2)
    if len(candidate_player_bones) < required_non_root_slots:
        issues.append(
            {
                "type": "insufficient_oot3d_player_bones_for_n64_slots",
                "required_non_root_slots": required_non_root_slots,
                "candidate_player_bone_count": len(candidate_player_bones),
            }
        )

    mapping: list[dict[str, object]] = []
    if root_motion_bone is not None:
        mapping.append(
            limb_mapping_record(
                0,
                N64_PLAYER_LIMB_NAMES[0],
                root_motion_bone,
                "root_translation",
                "root_motion_channel_candidate",
                track_coverage,
                geometry_usage,
            )
        )
        mapping.append(
            limb_mapping_record(
                1,
                N64_PLAYER_LIMB_NAMES[1],
                root_motion_bone,
                "root_rotation",
                "shared_root_control_bone",
                track_coverage,
                geometry_usage,
            )
        )

    for offset, n64_index in enumerate(range(2, len(N64_PLAYER_LIMB_NAMES))):
        if offset >= len(candidate_player_bones):
            break
        mapping.append(
            limb_mapping_record(
                n64_index,
                N64_PLAYER_LIMB_NAMES[n64_index],
                candidate_player_bones[offset],
                "limb_rotation",
                "ordered_animated_bone_candidate",
                track_coverage,
                geometry_usage,
            )
        )

    mapping_complete = len(mapping) == len(N64_PLAYER_LIMB_NAMES)
    if not mapping_complete:
        issues.append(
            {
                "type": "incomplete_n64_player_limb_mapping",
                "mapping_count": len(mapping),
                "required_mapping_count": len(N64_PLAYER_LIMB_NAMES),
            }
        )
    status = "candidate_ready_for_metric" if mapping_complete and not issues else "incomplete"
    return {
        "status": status,
        "mapping_policy": {
            "n64_frame_table": "slot 0 is root translation; slots 1..PLAYER_LIMB_MAX-1 are limb rotations",
            "oot3d_root": "the highest-coverage OOT3D CSAB translation bone supplies root translation and root rotation",
            "oot3d_limb_order": "remaining high-coverage animated OOT3D bones are mapped in skeleton order",
            "auxiliary_policy": "low-coverage animated bones with no bind-pose geometry are kept as auxiliary, not PlayerLimb slots",
            "validation_role": "candidate mapping only; pose-error metric must still validate orientation/deformation",
        },
        "n64_player_limb_count": len(N64_PLAYER_LIMB_NAMES),
        "n64_skeleton_limb_count": skeleton_summary.get("limb_count"),
        "n64_limb_order_status": n64_order["status"],
        "oot3d_bone_count": target_bone_count,
        "oot3d_animation_count": animation_count,
        "oot3d_animated_bone_union_count": len(track_coverage["bone_presence_counts"]),  # type: ignore[arg-type]
        "oot3d_common_animated_bone_count": len(
            [
                key
                for key, count in track_coverage["bone_presence_counts"].items()  # type: ignore[union-attr]
                if int(count) == animation_count
            ]
        ),
        "oot3d_root_motion_bone": root_motion_bone,
        "oot3d_auxiliary_animated_bones": auxiliary_bones,
        "oot3d_unanimated_bones": unanimated_bones,
        "mapping_count": len(mapping),
        "mapping": mapping,
        "sample_n64_limb_order": n64_order["records"][:sample_limit],  # type: ignore[index]
        "sample_oot3d_bone_coverage": track_coverage["bone_records"][:sample_limit],  # type: ignore[index]
        "sample_track_bone_sets": track_coverage["sample_track_bone_sets"],
        "issue_count": len(issues),
        "issues": issues[:sample_limit],
    }


def bind_pose_export_path_for_target(target: dict[str, object]) -> Path | None:
    bind_pose = target.get("bind_pose")
    if not isinstance(bind_pose, dict):
        return None
    export_path = bind_pose.get("export")
    if not export_path:
        return None
    return Path(str(export_path))


def oot3d_bone_geometry_usage(bind_export: dict[str, object] | None) -> dict[int, dict[str, object]]:
    usage: dict[int, dict[str, object]] = {}
    if not isinstance(bind_export, dict):
        return usage
    for mesh in bind_export.get("meshes", []):
        if not isinstance(mesh, dict):
            continue
        mesh_index = int_or_none(mesh.get("mesh_index"))
        for primitive in mesh.get("primitives", []):
            if not isinstance(primitive, dict):
                continue
            for vertex in primitive.get("vertices", []):
                if not isinstance(vertex, dict):
                    continue
                for influence in vertex.get("influences", []):
                    if not isinstance(influence, dict):
                        continue
                    bone_index = int_or_none(influence.get("bone_index"))
                    if bone_index is None:
                        continue
                    weight = float(influence.get("weight", 0.0) or 0.0)
                    record = usage.setdefault(
                        bone_index,
                        {
                            "influence_rows": 0,
                            "nonzero_influence_rows": 0,
                            "weight_sum": 0.0,
                            "mesh_indices": set(),
                        },
                    )
                    record["influence_rows"] = int(record["influence_rows"]) + 1
                    if weight > 0.0:
                        record["nonzero_influence_rows"] = int(record["nonzero_influence_rows"]) + 1
                        record["weight_sum"] = float(record["weight_sum"]) + weight
                        if mesh_index is not None:
                            record["mesh_indices"].add(mesh_index)  # type: ignore[union-attr]
    for record in usage.values():
        mesh_indices = record.get("mesh_indices", set())
        record["mesh_indices"] = sorted(mesh_indices) if isinstance(mesh_indices, set) else []
        record["weight_sum"] = round(float(record.get("weight_sum", 0.0)), 6)
    return usage


def oot3d_track_bone_coverage(target: dict[str, object], sample_limit: int) -> dict[str, object]:
    animations = target.get("animations", [])
    if not isinstance(animations, list):
        animations = []
    issues: list[dict[str, object]] = []
    bone_presence_counts: Counter[int] = Counter()
    channel_counts_by_bone: dict[int, Counter[str]] = {}
    track_bone_sets: Counter[tuple[int, ...]] = Counter()
    sample_track_bone_sets: list[dict[str, object]] = []
    read_track_count = 0

    for animation in animations:
        if not isinstance(animation, dict):
            continue
        track_export_file = animation.get("track_export_file")
        if not track_export_file:
            issues.append({"type": "missing_track_export_file", "csab_name": animation.get("csab_name")})
            continue
        track_path = Path(str(track_export_file))
        if not track_path.is_file():
            issues.append(
                {
                    "type": "track_export_file_not_found",
                    "csab_name": animation.get("csab_name"),
                    "track_export_file": str(track_path),
                }
            )
            continue
        try:
            track_export = load_json(track_path)
        except (OSError, json.JSONDecodeError) as exc:
            issues.append(
                {
                    "type": "invalid_track_export_file",
                    "csab_name": animation.get("csab_name"),
                    "track_export_file": str(track_path),
                    "message": str(exc),
                }
            )
            continue

        read_track_count += 1
        bones_in_track: set[int] = set()
        tracks = track_export.get("tracks", [])
        if not isinstance(tracks, list):
            issues.append({"type": "track_export_missing_tracks", "csab_name": animation.get("csab_name")})
            continue
        for track in tracks:
            if not isinstance(track, dict):
                continue
            bone_index = int_or_none(track.get("bone_index"))
            if bone_index is None:
                continue
            bones_in_track.add(bone_index)
            channel_counter = channel_counts_by_bone.setdefault(bone_index, Counter())
            channels = track.get("channels", [])
            if isinstance(channels, list):
                for channel in channels:
                    if isinstance(channel, dict):
                        channel_counter[str(channel.get("semantic", "unknown"))] += 1
        for bone_index in bones_in_track:
            bone_presence_counts[bone_index] += 1
        track_bone_set = tuple(sorted(bones_in_track))
        track_bone_sets[track_bone_set] += 1
        if len(sample_track_bone_sets) < sample_limit:
            sample_track_bone_sets.append(
                {
                    "csab_name": animation.get("csab_name"),
                    "bone_indices": list(track_bone_set),
                    "bone_count": len(track_bone_set),
                }
            )

    bone_records = []
    for bone_index in sorted(bone_presence_counts):
        channel_counts = channel_counts_by_bone.get(bone_index, Counter())
        bone_records.append(
            {
                "bone_index": bone_index,
                "track_presence_count": bone_presence_counts[bone_index],
                "track_presence_ratio": (
                    round(bone_presence_counts[bone_index] / read_track_count, 6)
                    if read_track_count
                    else 0.0
                ),
                "channel_counts": dict(sorted(channel_counts.items())),
            }
        )
    return {
        "animation_count": len(animations),
        "read_track_count": read_track_count,
        "bone_presence_counts": {str(key): value for key, value in sorted(bone_presence_counts.items())},
        "bone_records": bone_records,
        "track_bone_set_counts": {
            ",".join(str(value) for value in key): count for key, count in track_bone_sets.most_common()
        },
        "sample_track_bone_sets": sample_track_bone_sets,
        "issues": issues,
    }


def oot3d_root_motion_bone_candidate(track_coverage: dict[str, object]) -> int | None:
    best_bone = None
    best_score: tuple[int, int, int] | None = None
    for record in track_coverage.get("bone_records", []):
        if not isinstance(record, dict):
            continue
        bone_index = int_or_none(record.get("bone_index"))
        channel_counts = record.get("channel_counts", {})
        if bone_index is None or not isinstance(channel_counts, dict):
            continue
        translation_count = sum(
            int(channel_counts.get(channel, 0) or 0)
            for channel in ("translation_x", "translation_y", "translation_z")
        )
        rotation_count = sum(
            int(channel_counts.get(channel, 0) or 0)
            for channel in ("rotation_x", "rotation_y", "rotation_z")
        )
        presence_count = int(record.get("track_presence_count", 0) or 0)
        score = (translation_count, presence_count, rotation_count)
        if translation_count > 0 and (best_score is None or score > best_score):
            best_score = score
            best_bone = bone_index
    if best_bone is not None:
        return best_bone
    records = [record for record in track_coverage.get("bone_records", []) if isinstance(record, dict)]
    if not records:
        return None
    return int_or_none(records[0].get("bone_index"))


def oot3d_auxiliary_animated_bones(
    track_coverage: dict[str, object],
    geometry_usage: dict[int, dict[str, object]],
    animation_count: int,
) -> list[int]:
    auxiliary: list[int] = []
    if animation_count <= 0:
        return auxiliary
    low_coverage_cutoff = max(1, int(animation_count * 0.1))
    for key, count in track_coverage.get("bone_presence_counts", {}).items():  # type: ignore[union-attr]
        bone_index = int(key)
        presence_count = int(count)
        geometry = geometry_usage.get(bone_index, {})
        nonzero_rows = int(geometry.get("nonzero_influence_rows", 0) or 0) if isinstance(geometry, dict) else 0
        if presence_count < animation_count and presence_count <= low_coverage_cutoff and nonzero_rows == 0:
            auxiliary.append(bone_index)
    return sorted(auxiliary)


def n64_player_limb_order(skeleton_summary: dict[str, object]) -> dict[str, object]:
    issues: list[dict[str, object]] = []
    limbs = skeleton_summary.get("sample_limbs", [])
    if not isinstance(limbs, list):
        limbs = []
    records = [
        {
            "n64_slot": 0,
            "player_limb": N64_PLAYER_LIMB_NAMES[0],
            "semantic_key": player_limb_semantic_key(N64_PLAYER_LIMB_NAMES[0]),
            "source": "joint_table_root_translation",
        }
    ]
    xml_semantics = []
    for index, limb in enumerate(limbs, start=1):
        if not isinstance(limb, dict):
            continue
        semantic_key = xml_limb_semantic_key(limb.get("name"))
        expected_name = N64_PLAYER_LIMB_NAMES[index] if index < len(N64_PLAYER_LIMB_NAMES) else None
        expected_key = player_limb_semantic_key(expected_name) if expected_name is not None else None
        xml_semantics.append(semantic_key)
        records.append(
            {
                "n64_slot": index,
                "player_limb": expected_name,
                "xml_limb_name": limb.get("name"),
                "xml_limb_offset": limb.get("offset"),
                "semantic_key": semantic_key,
                "expected_semantic_key": expected_key,
                "semantic_status": "matches" if semantic_key == expected_key else "mismatch",
            }
        )
    expected_semantics = [player_limb_semantic_key(name) for name in N64_PLAYER_LIMB_NAMES[1:]]
    if len(xml_semantics) != len(expected_semantics):
        issues.append(
            {
                "type": "n64_skeleton_limb_count_does_not_match_player_limb_order",
                "xml_limb_count": len(xml_semantics),
                "expected_limb_count": len(expected_semantics),
            }
        )
    mismatches = [record for record in records[1:] if record.get("semantic_status") != "matches"]
    if mismatches:
        issues.append(
            {
                "type": "n64_skeleton_limb_order_mismatch",
                "count": len(mismatches),
                "sample_mismatches": mismatches[:5],
            }
        )
    status = "matches_player_limb_order" if not issues else "mismatch"
    return {"status": status, "records": records, "issues": issues}


def player_limb_semantic_key(player_limb_name: str | None) -> str | None:
    if player_limb_name is None:
        return None
    key = player_limb_name.lower()
    key = re.sub(r"^player_limb_", "", key)
    return key


def xml_limb_semantic_key(limb_name: object) -> str | None:
    text = re.sub(r"[^a-z0-9]", "", str(limb_name or "").lower())
    if not text:
        return None
    special_cases = {
        "swordandsheath": "sheath",
        "lowercontrol": "lower",
        "uppercontrol": "upper",
        "rightleg": "r_shin",
        "leftleg": "l_shin",
        "rightarm": "r_forearm",
        "leftarm": "l_forearm",
    }
    for needle, key in special_cases.items():
        if needle in text:
            return key
    for key in sorted((player_limb_semantic_key(name) for name in N64_PLAYER_LIMB_NAMES[1:]), key=len, reverse=True):
        if key is None:
            continue
        needle = key.replace("l_", "left").replace("r_", "right").replace("_", "")
        if needle in text:
            return key
    return None


def limb_mapping_record(
    n64_slot: int,
    player_limb_name: str,
    oot3d_bone_index: int,
    transform_role: str,
    confidence: str,
    track_coverage: dict[str, object],
    geometry_usage: dict[int, dict[str, object]],
) -> dict[str, object]:
    bone_record = next(
        (
            record
            for record in track_coverage.get("bone_records", [])
            if isinstance(record, dict) and int_or_none(record.get("bone_index")) == oot3d_bone_index
        ),
        {},
    )
    geometry = geometry_usage.get(oot3d_bone_index, {})
    return {
        "n64_slot": n64_slot,
        "player_limb": player_limb_name,
        "oot3d_bone_index": oot3d_bone_index,
        "transform_role": transform_role,
        "confidence": confidence,
        "track_presence_count": bone_record.get("track_presence_count") if isinstance(bone_record, dict) else None,
        "track_presence_ratio": bone_record.get("track_presence_ratio") if isinstance(bone_record, dict) else None,
        "channel_counts": bone_record.get("channel_counts", {}) if isinstance(bone_record, dict) else {},
        "geometry": geometry if isinstance(geometry, dict) else {},
    }


def time_normalized_pose_sample_comparison(
    oot3d_records: list[dict[str, object]],
    player_animation_records: list[dict[str, object]],
    pose_batch: dict[str, object] | None,
    pose_batch_manifest_path: Path | None,
    n64_base_o2r_path: Path | None,
    model_archive: str,
    model_cmb: str,
    sample_limit: int,
) -> dict[str, object]:
    if pose_batch is None or pose_batch_manifest_path is None:
        return {
            "status": "not_provided",
            "pose_batch_manifest": None,
            "compared_match_count": 0,
            "sample_pair_count": 0,
            "issue_count": 0,
            "sample_comparisons": [],
            "sample_issues": [],
        }
    if n64_base_o2r_path is None:
        return {
            "status": "missing_n64_base_o2r",
            "pose_batch_manifest": str(pose_batch_manifest_path),
            "compared_match_count": 0,
            "sample_pair_count": 0,
            "issue_count": 1,
            "sample_comparisons": [],
            "sample_issues": [{"type": "missing_n64_base_o2r"}],
        }

    pose_records = indexed_pose_batch_records(pose_batch, model_archive, model_cmb)
    exact_by_stem, normalized_by_key = n64_candidate_indices(player_animation_records)
    comparisons: list[dict[str, object]] = []
    issues: list[dict[str, object]] = []
    sample_pair_count = 0
    bounds_pair_count = 0
    n64_signature_pair_count = 0

    try:
        with zipfile.ZipFile(n64_base_o2r_path) as archive:
            for oot3d_record in oot3d_records:
                candidates = single_n64_candidates_for_oot3d_record(oot3d_record, exact_by_stem, normalized_by_key)
                if len(candidates) != 1:
                    continue
                pose_record = pose_records.get(normalize_path(oot3d_record.get("csab_name")))
                if pose_record is None:
                    issues.append(
                        {
                            "type": "missing_oot3d_pose_record",
                            "csab_name": oot3d_record.get("csab_name"),
                        }
                    )
                    continue
                pose_sample_path = resolve_pose_sample_path(pose_batch_manifest_path, pose_batch, pose_record)
                if pose_sample_path is None or not pose_sample_path.is_file():
                    issues.append(
                        {
                            "type": "missing_oot3d_pose_sample_file",
                            "csab_name": oot3d_record.get("csab_name"),
                            "pose_sample_export": pose_record.get("pose_sample_export"),
                        }
                    )
                    continue
                pose_sample = load_json(pose_sample_path)
                candidate = candidates[0]
                raw_payload = read_player_animation_raw_payload(archive, candidate)
                if raw_payload is None:
                    issues.append(
                        {
                            "type": "missing_n64_payload",
                            "csab_name": oot3d_record.get("csab_name"),
                            "n64_name": candidate.get("name"),
                            "data_name": candidate.get("data_name"),
                        }
                    )
                    continue

                sample_pairs = pose_sample_pairs(oot3d_record, pose_record, pose_sample, candidate, raw_payload)
                sample_pair_count += len(sample_pairs)
                bounds_pair_count += sum(1 for pair in sample_pairs if pair.get("oot3d_position_bounds") is not None)
                n64_signature_pair_count += sum(
                    1 for pair in sample_pairs if pair.get("n64_frame_signature") is not None
                )
                incomplete_pairs = [pair for pair in sample_pairs if pair.get("mapping_status") != "paired"]
                if incomplete_pairs:
                    issues.append(
                        {
                            "type": "incomplete_time_normalized_pose_sample_pair",
                            "csab_name": oot3d_record.get("csab_name"),
                            "n64_name": candidate.get("name"),
                            "count": len(incomplete_pairs),
                        }
                    )
                comparisons.append(
                    {
                        "status": "sampled",
                        "csab_name": oot3d_record.get("csab_name"),
                        "n64_name": candidate.get("name"),
                        "data_name": candidate.get("data_name"),
                        "oot3d_frame_count_candidate": pose_record.get("frame_count_candidate"),
                        "oot3d_frame_slot_count": oot3d_record.get("frame_slot_count"),
                        "n64_frame_count": candidate.get("frame_count"),
                        "frame_ratio": (
                            round(
                                float(int_or_none(oot3d_record.get("frame_slot_count")) or 0)
                                / float(int_or_none(candidate.get("frame_count")) or 1),
                                6,
                            )
                            if int_or_none(candidate.get("frame_count"))
                            else None
                        ),
                        "sample_pairs": sample_pairs,
                    }
                )
    except zipfile.BadZipFile as exc:
        return {
            "status": "invalid_n64_base_o2r",
            "pose_batch_manifest": str(pose_batch_manifest_path),
            "compared_match_count": 0,
            "sample_pair_count": 0,
            "issue_count": 1,
            "sample_comparisons": [],
            "sample_issues": [{"type": "invalid_n64_base_o2r", "message": str(exc)}],
        }

    status = "sampled" if comparisons and not issues else "incomplete"
    return {
        "status": status,
        "pose_batch_manifest": str(pose_batch_manifest_path),
        "time_normalization_policy": {
            "oot3d_time_domain": "pose_batch frame_count_candidate is treated as inclusive max sample frame",
            "n64_time_domain": "PlayerAnimation frame_count is treated as count, so max frame is frame_count - 1",
            "sample_frame_mapping": "round((oot3d_frame / oot3d_max_frame) * n64_max_frame)",
            "purpose": "sample-pair construction only; not yet a pose-error equivalence metric",
        },
        "matched_candidate_count": len(comparisons) + sum(1 for issue in issues if "csab_name" in issue),
        "pose_record_count": len(pose_records),
        "compared_match_count": len(comparisons),
        "sample_pair_count": sample_pair_count,
        "oot3d_bounds_pair_count": bounds_pair_count,
        "n64_frame_signature_pair_count": n64_signature_pair_count,
        "issue_count": len(issues),
        "sample_comparisons": comparisons[:sample_limit],
        "sample_issues": issues[:sample_limit],
    }


def indexed_pose_batch_records(
    pose_batch: dict[str, object],
    model_archive: str,
    model_cmb: str,
) -> dict[str, dict[str, object]]:
    archive = normalize_path(model_archive)
    cmb = normalize_path(model_cmb)
    records: dict[str, dict[str, object]] = {}
    for record in pose_batch.get("records", []):
        if not isinstance(record, dict):
            continue
        if normalize_path(record.get("archive_path")) != archive:
            continue
        if normalize_path(record.get("target_cmb_name")) != cmb:
            continue
        if record.get("status") != "exported":
            continue
        records[normalize_path(record.get("csab_name"))] = record
    return records


def n64_candidate_indices(
    player_animation_records: list[dict[str, object]],
) -> tuple[dict[str, list[dict[str, object]]], dict[str, list[dict[str, object]]]]:
    exact_by_stem: dict[str, list[dict[str, object]]] = {}
    normalized_by_key: dict[str, list[dict[str, object]]] = {}
    for record in player_animation_records:
        exact_by_stem.setdefault(str(record["stem"]).lower(), []).append(record)
        normalized_by_key.setdefault(str(record["normalized_key"]), []).append(record)
    return exact_by_stem, normalized_by_key


def single_n64_candidates_for_oot3d_record(
    oot3d_record: dict[str, object],
    exact_by_stem: dict[str, list[dict[str, object]]],
    normalized_by_key: dict[str, list[dict[str, object]]],
) -> list[dict[str, object]]:
    exact_candidates = exact_by_stem.get(str(oot3d_record["stem"]).lower(), [])
    normalized_candidates = normalized_by_key.get(str(oot3d_record["normalized_key"]), [])
    if len(exact_candidates) == 1:
        return exact_candidates
    if not exact_candidates and len(normalized_candidates) == 1:
        return normalized_candidates
    return []


def resolve_pose_sample_path(
    pose_batch_manifest_path: Path,
    pose_batch: dict[str, object],
    record: dict[str, object],
) -> Path | None:
    pose_export = record.get("pose_sample_export")
    if not pose_export:
        return None
    pose_path = Path(str(pose_export))
    if pose_path.is_absolute():
        return pose_path
    output = pose_batch.get("output")
    if output:
        return Path(str(output)) / pose_path
    return pose_batch_manifest_path.parent / pose_path


def read_player_animation_raw_payload(
    archive: zipfile.ZipFile,
    candidate: dict[str, object],
) -> bytes | None:
    data_name = str(candidate.get("data_name") or "")
    if not data_name:
        return None
    resource_path = f"misc/link_animetion/{data_name}"
    try:
        payload = archive.read(resource_path)
    except KeyError:
        return None
    if len(payload) < SHIPWRIGHT_PLAYER_ANIMATION_ENTRY_PREFIX_SIZE:
        return None
    decoded_s16_count = struct.unpack_from("<I", payload, SHIPWRIGHT_BINARY_RESOURCE_HEADER_SIZE)[0]
    payload_start = SHIPWRIGHT_PLAYER_ANIMATION_ENTRY_PREFIX_SIZE
    payload_end = payload_start + decoded_s16_count * 2
    if payload_end > len(payload):
        return None
    return payload[payload_start:payload_end]


def pose_sample_pairs(
    oot3d_record: dict[str, object],
    pose_record: dict[str, object],
    pose_sample: dict[str, object],
    n64_candidate: dict[str, object],
    raw_payload: bytes,
) -> list[dict[str, object]]:
    oot3d_max_frame = int_or_none(pose_record.get("frame_count_candidate"))
    n64_frame_count = int_or_none(n64_candidate.get("frame_count"))
    if oot3d_max_frame is None or n64_frame_count is None or n64_frame_count <= 0:
        return []
    n64_max_frame = max(0, n64_frame_count - 1)
    frame_bounds = pose_sample_frame_bounds(pose_sample)
    raw_sample_frames = pose_record.get("sample_frames", [])
    if not isinstance(raw_sample_frames, list):
        raw_sample_frames = []
    sample_frames = [int(frame) for frame in raw_sample_frames if int_or_none(frame) is not None]
    if not sample_frames:
        sample_frames = [0, max(0, oot3d_max_frame // 2), max(0, oot3d_max_frame)]
    pairs: list[dict[str, object]] = []
    for oot3d_frame in sorted(set(sample_frames)):
        normalized_t = 0.0 if oot3d_max_frame <= 0 else max(0.0, min(1.0, oot3d_frame / oot3d_max_frame))
        n64_frame = int(round(normalized_t * n64_max_frame))
        n64_signature = frame_signature(raw_payload, n64_frame)
        oot3d_bounds = frame_bounds.get(oot3d_frame) or pose_sample.get("position_bounds")
        pairs.append(
            {
                "normalized_t": round(normalized_t, 6),
                "oot3d_frame": oot3d_frame,
                "n64_frame": n64_frame,
                "oot3d_position_bounds": oot3d_bounds,
                "oot3d_position_extent": position_extent_from_bounds(oot3d_bounds),
                "oot3d_aabb_volume": aabb_volume(oot3d_bounds),
                "n64_frame_signature": compact_frame_signature(n64_signature),
                "mapping_status": "paired" if n64_signature is not None and oot3d_bounds is not None else "incomplete",
            }
        )
    return pairs


def pose_sample_frame_bounds(pose_sample: dict[str, object]) -> dict[int, dict[str, object]]:
    bounds: dict[int, dict[str, object]] = {}
    for frame in pose_sample.get("frames", []):
        if not isinstance(frame, dict):
            continue
        frame_index = int_or_none(frame.get("frame"))
        position_bounds = frame.get("position_bounds")
        if frame_index is not None and isinstance(position_bounds, dict):
            bounds[frame_index] = position_bounds
    return bounds


def frame_signature(raw_payload: bytes, frame_index: int) -> dict[str, object] | None:
    frame_byte_count = N64_PLAYER_ANIMATION_BYTES_PER_FRAME
    start = frame_index * frame_byte_count
    end = start + frame_byte_count
    if start < 0 or end > len(raw_payload):
        return None
    frame_payload = raw_payload[start:end]
    values = [value[0] for value in struct.iter_unpack("<h", frame_payload)]
    return {
        "frame_index": frame_index,
        "s16_count": len(values),
        "min_s16": min(values, default=0),
        "max_s16": max(values, default=0),
        "sha1": hashlib.sha1(frame_payload).hexdigest(),
        "first_s16_values": values[:12],
        "limb_vec3s": frame_limb_vec3s(values),
        "trailing_s16": values[-1] if values else None,
    }


def compact_frame_signature(signature: dict[str, object] | None) -> dict[str, object] | None:
    if signature is None:
        return None
    limb_vec3s = signature.get("limb_vec3s", [])
    sampled_limbs = []
    if isinstance(limb_vec3s, list):
        sampled_limbs = [
            limb
            for limb in limb_vec3s
            if isinstance(limb, dict)
            and limb.get("name") in {"PLAYER_LIMB_NONE", "PLAYER_LIMB_ROOT", "PLAYER_LIMB_WAIST", "PLAYER_LIMB_UPPER"}
        ]
    return {
        "frame_index": signature.get("frame_index"),
        "s16_count": signature.get("s16_count"),
        "min_s16": signature.get("min_s16"),
        "max_s16": signature.get("max_s16"),
        "sha1": signature.get("sha1"),
        "first_s16_values": signature.get("first_s16_values"),
        "sampled_limb_vec3s": sampled_limbs,
        "trailing_s16": signature.get("trailing_s16"),
    }


def position_extent_from_bounds(bounds: object) -> dict[str, float] | None:
    if not isinstance(bounds, dict):
        return None
    extent: dict[str, float] = {}
    for axis in ("x", "y", "z"):
        values = bounds.get(axis)
        if not isinstance(values, dict):
            return None
        min_value = values.get("min")
        max_value = values.get("max")
        if not isinstance(min_value, (int, float)) or not isinstance(max_value, (int, float)):
            return None
        extent[axis] = round(float(max_value) - float(min_value), 6)
    return extent


def aabb_volume(bounds: object) -> float | None:
    extent = position_extent_from_bounds(bounds)
    if extent is None:
        return None
    return round(extent["x"] * extent["y"] * extent["z"], 6)


def decode_n64_skeleton_pose_reference(
    o2r_path: Path | None,
    skeleton_summary: dict[str, object],
    sample_limit: int,
) -> dict[str, object]:
    if o2r_path is None:
        return {
            "status": "not_provided",
            "o2r_path": None,
            "skeleton_resource_path": None,
            "limb_count": 0,
            "issue_count": 0,
            "sample_issues": [],
        }
    if skeleton_summary.get("status") != "resolved":
        return {
            "status": "missing_n64_skeleton_summary",
            "o2r_path": str(o2r_path),
            "skeleton_resource_path": None,
            "limb_count": 0,
            "issue_count": 1,
            "sample_issues": [{"type": "missing_n64_skeleton_summary"}],
        }

    resource_prefix = str(skeleton_summary.get("resource_prefix") or "").strip("/")
    skeleton_name = str(skeleton_summary.get("name") or "").strip("/")
    skeleton_resource_path = f"{resource_prefix}/{skeleton_name}" if resource_prefix and skeleton_name else ""
    if not skeleton_resource_path:
        return {
            "status": "missing_skeleton_resource_path",
            "o2r_path": str(o2r_path),
            "skeleton_resource_path": None,
            "limb_count": 0,
            "issue_count": 1,
            "sample_issues": [{"type": "missing_skeleton_resource_path"}],
        }

    issues: list[dict[str, object]] = []
    try:
        with zipfile.ZipFile(o2r_path) as archive:
            archive_names = set(archive.namelist())
            if skeleton_resource_path not in archive_names:
                return {
                    "status": "missing_skeleton_resource",
                    "o2r_path": str(o2r_path),
                    "skeleton_resource_path": skeleton_resource_path,
                    "limb_count": 0,
                    "issue_count": 1,
                    "sample_issues": [
                        {
                            "type": "missing_skeleton_resource",
                            "resource_path": skeleton_resource_path,
                        }
                    ],
                }

            try:
                skeleton_resource = decode_n64_skeleton_resource(
                    archive.read(skeleton_resource_path),
                    skeleton_resource_path,
                )
            except ParseError as exc:
                return {
                    "status": "invalid_skeleton_resource",
                    "o2r_path": str(o2r_path),
                    "skeleton_resource_path": skeleton_resource_path,
                    "limb_count": 0,
                    "issue_count": 1,
                    "sample_issues": [
                        {
                            "type": "invalid_skeleton_resource",
                            "resource_path": skeleton_resource_path,
                            "message": str(exc),
                        }
                    ],
                }

            limbs: list[dict[str, object]] = []
            for limb_index, limb_path in enumerate(skeleton_resource["limb_table"]):
                if not isinstance(limb_path, str) or limb_path not in archive_names:
                    issues.append(
                        {
                            "type": "missing_limb_resource",
                            "limb_index": limb_index,
                            "resource_path": limb_path,
                        }
                    )
                    continue
                try:
                    limb = decode_n64_limb_resource(archive.read(limb_path), limb_path, limb_index)
                except ParseError as exc:
                    issues.append(
                        {
                            "type": "invalid_limb_resource",
                            "limb_index": limb_index,
                            "resource_path": limb_path,
                            "message": str(exc),
                        }
                    )
                    continue
                limbs.append(limb)
    except zipfile.BadZipFile as exc:
        return {
            "status": "invalid_o2r_archive",
            "o2r_path": str(o2r_path),
            "skeleton_resource_path": skeleton_resource_path,
            "limb_count": 0,
            "issue_count": 1,
            "sample_issues": [{"type": "invalid_o2r_archive", "message": str(exc)}],
        }

    expected_limb_count = int_or_none(skeleton_resource.get("limb_count")) or 0
    if expected_limb_count and len(limbs) != expected_limb_count:
        issues.append(
            {
                "type": "decoded_limb_count_mismatch",
                "expected_limb_count": expected_limb_count,
                "decoded_limb_count": len(limbs),
            }
        )

    return {
        "status": "decoded" if limbs and not issues else "incomplete",
        "o2r_path": str(o2r_path),
        "skeleton_resource_path": skeleton_resource_path,
        "skeleton": skeleton_resource,
        "limb_count": len(limbs),
        "root_limb_index": 0,
        "draw_policy": {
            "source": "soh/src/code/z_skelanime.c SkelAnime_DrawFlexOpa/SkelAnime_DrawFlexLimbOpa",
            "joint_table_0": "root translation",
            "joint_table_1": "root rotation",
            "child_limb_rotation": "jointTable[limbIndex + 1]",
            "root_limb_joint_pos": "not applied by the Flex draw root path",
        },
        "limbs": limbs,
        "sample_limbs": limbs[:sample_limit],
        "issue_count": len(issues),
        "sample_issues": issues[:sample_limit],
    }


def decode_n64_skeleton_resource(payload: bytes, resource_path: str) -> dict[str, object]:
    if len(payload) < SHIPWRIGHT_BINARY_RESOURCE_HEADER_SIZE:
        raise ParseError(f"{resource_path}: skeleton resource shorter than Shipwright header")
    reader = O2RBinaryReader(payload)
    skeleton_type = reader.i8()
    limb_type = reader.i8()
    limb_count = reader.u32()
    dlist_count = reader.u32()
    limb_table_type = reader.i8()
    limb_table_count = reader.u32()
    limb_table = [reader.string() for _ in range(limb_table_count)]
    return {
        "resource_path": resource_path,
        "skeleton_type": skeleton_type,
        "limb_type": limb_type,
        "limb_count": limb_count,
        "dlist_count": dlist_count,
        "limb_table_type": limb_table_type,
        "limb_table_count": limb_table_count,
        "limb_table": limb_table,
    }


def decode_n64_limb_resource(payload: bytes, resource_path: str, limb_index: int) -> dict[str, object]:
    if len(payload) < SHIPWRIGHT_BINARY_RESOURCE_HEADER_SIZE:
        raise ParseError(f"{resource_path}: limb resource shorter than Shipwright header")
    reader = O2RBinaryReader(payload)
    limb_type = reader.i8()
    skin_segment_type = reader.i8()
    skin_dlist = reader.string()
    skin_vtx_count = reader.u16()
    skin_limb_modif_count = reader.u32()
    for _ in range(skin_limb_modif_count):
        reader.u16()
        skin_vertex_count = reader.i32()
        for _ in range(skin_vertex_count):
            reader.i16()
            reader.i16()
            reader.i16()
            reader.i8()
            reader.i8()
            reader.i8()
            reader.u8()
        skin_transform_count = reader.i32()
        for _ in range(skin_transform_count):
            reader.u8()
            reader.i16()
            reader.i16()
            reader.i16()
            reader.u8()
    skin_dlist_2 = reader.string()
    leg_trans = {"x": reader.f32(), "y": reader.f32(), "z": reader.f32()}
    base_rot = {"x": reader.u16(), "y": reader.u16(), "z": reader.u16()}
    child_ptr = reader.string()
    sibling_ptr = reader.string()
    dlist_ptr = reader.string()
    dlist_2_ptr = reader.string()
    joint_pos = {"x": reader.i16(), "y": reader.i16(), "z": reader.i16()}
    child_index = reader.u8()
    sibling_index = reader.u8()
    return {
        "index": limb_index,
        "resource_path": resource_path,
        "limb_type": limb_type,
        "skin_segment_type": skin_segment_type,
        "skin_dlist": skin_dlist,
        "skin_vtx_count": skin_vtx_count,
        "skin_limb_modif_count": skin_limb_modif_count,
        "skin_dlist_2": skin_dlist_2,
        "leg_trans": rounded_xyz(leg_trans),
        "base_rot": base_rot,
        "child_ptr": child_ptr,
        "sibling_ptr": sibling_ptr,
        "dlist_ptr": dlist_ptr,
        "dlist_2_ptr": dlist_2_ptr,
        "joint_pos": joint_pos,
        "child_index": child_index,
        "sibling_index": sibling_index,
    }


def time_normalized_pose_error_metric(
    oot3d_records: list[dict[str, object]],
    player_animation_records: list[dict[str, object]],
    pose_batch: dict[str, object] | None,
    pose_batch_manifest_path: Path | None,
    n64_base_o2r_path: Path | None,
    model_archive: str,
    model_cmb: str,
    limb_mapping: dict[str, object],
    skeleton_pose_reference: dict[str, object],
    sample_limit: int,
) -> dict[str, object]:
    if pose_batch is None or pose_batch_manifest_path is None:
        return {
            "status": "not_provided",
            "acceptance_status": "not_started",
            "compared_match_count": 0,
            "sample_pair_count": 0,
            "issue_count": 0,
            "sample_metrics": [],
            "sample_issues": [],
        }
    if n64_base_o2r_path is None:
        return {
            "status": "missing_n64_base_o2r",
            "acceptance_status": "not_started",
            "compared_match_count": 0,
            "sample_pair_count": 0,
            "issue_count": 1,
            "sample_metrics": [],
            "sample_issues": [{"type": "missing_n64_base_o2r"}],
        }
    if limb_mapping.get("status") != "candidate_ready_for_metric":
        return {
            "status": "missing_limb_mapping",
            "acceptance_status": "not_started",
            "compared_match_count": 0,
            "sample_pair_count": 0,
            "issue_count": int(limb_mapping.get("issue_count", 1) or 1),
            "sample_metrics": [],
            "sample_issues": [{"type": "missing_limb_mapping"}],
        }
    if skeleton_pose_reference.get("status") != "decoded":
        return {
            "status": "missing_n64_skeleton_pose_reference",
            "acceptance_status": "not_started",
            "compared_match_count": 0,
            "sample_pair_count": 0,
            "issue_count": int(skeleton_pose_reference.get("issue_count", 1) or 1),
            "sample_metrics": [],
            "sample_issues": [{"type": "missing_n64_skeleton_pose_reference"}],
        }

    pose_records = indexed_pose_batch_records(pose_batch, model_archive, model_cmb)
    exact_by_stem, normalized_by_key = n64_candidate_indices(player_animation_records)
    comparisons: list[dict[str, object]] = []
    sample_metrics: list[dict[str, object]] = []
    issues: list[dict[str, object]] = []
    normalized_extent_mean_abs_deltas: list[float] = []
    normalized_extent_max_abs_deltas: list[float] = []
    center_delta_normalized_values: list[float] = []
    scale_factors: list[float] = []
    extent_ratios: dict[str, list[float]] = {"x": [], "y": [], "z": []}
    volume_ratios: list[float] = []
    sample_pair_count = 0

    try:
        with zipfile.ZipFile(n64_base_o2r_path) as archive:
            for oot3d_record in oot3d_records:
                candidates = single_n64_candidates_for_oot3d_record(oot3d_record, exact_by_stem, normalized_by_key)
                if len(candidates) != 1:
                    continue
                pose_record = pose_records.get(normalize_path(oot3d_record.get("csab_name")))
                if pose_record is None:
                    issues.append(
                        {
                            "type": "missing_oot3d_pose_record",
                            "csab_name": oot3d_record.get("csab_name"),
                        }
                    )
                    continue
                pose_sample_path = resolve_pose_sample_path(pose_batch_manifest_path, pose_batch, pose_record)
                if pose_sample_path is None or not pose_sample_path.is_file():
                    issues.append(
                        {
                            "type": "missing_oot3d_pose_sample_file",
                            "csab_name": oot3d_record.get("csab_name"),
                            "pose_sample_export": pose_record.get("pose_sample_export"),
                        }
                    )
                    continue
                pose_sample = load_json(pose_sample_path)
                candidate = candidates[0]
                raw_payload = read_player_animation_raw_payload(archive, candidate)
                if raw_payload is None:
                    issues.append(
                        {
                            "type": "missing_n64_payload",
                            "csab_name": oot3d_record.get("csab_name"),
                            "n64_name": candidate.get("name"),
                            "data_name": candidate.get("data_name"),
                        }
                    )
                    continue

                pair_metrics: list[dict[str, object]] = []
                for pair in pose_sample_pairs(oot3d_record, pose_record, pose_sample, candidate, raw_payload):
                    sample_pair_count += 1
                    oot3d_bounds = pair.get("oot3d_position_bounds")
                    n64_frame = int_or_none(pair.get("n64_frame"))
                    if n64_frame is None or oot3d_bounds is None:
                        issues.append(
                            {
                                "type": "incomplete_pose_metric_pair",
                                "csab_name": oot3d_record.get("csab_name"),
                                "n64_name": candidate.get("name"),
                                "oot3d_frame": pair.get("oot3d_frame"),
                                "n64_frame": pair.get("n64_frame"),
                            }
                        )
                        continue
                    n64_bounds = n64_frame_world_bounds(raw_payload, n64_frame, skeleton_pose_reference)
                    if n64_bounds is None:
                        issues.append(
                            {
                                "type": "missing_n64_skeleton_bounds",
                                "csab_name": oot3d_record.get("csab_name"),
                                "n64_name": candidate.get("name"),
                                "n64_frame": n64_frame,
                            }
                        )
                        continue
                    metric = pose_bounds_metric(oot3d_bounds, n64_bounds["bounds"])
                    if metric is None:
                        issues.append(
                            {
                                "type": "invalid_pose_bounds_metric",
                                "csab_name": oot3d_record.get("csab_name"),
                                "n64_name": candidate.get("name"),
                                "oot3d_frame": pair.get("oot3d_frame"),
                                "n64_frame": n64_frame,
                            }
                        )
                        continue
                    metric_record = {
                        "normalized_t": pair.get("normalized_t"),
                        "oot3d_frame": pair.get("oot3d_frame"),
                        "n64_frame": n64_frame,
                        **metric,
                        "n64_point_count": n64_bounds.get("point_count"),
                    }
                    pair_metrics.append(metric_record)
                    if len(sample_metrics) < sample_limit:
                        sample_metrics.append(
                            {
                                "csab_name": oot3d_record.get("csab_name"),
                                "n64_name": candidate.get("name"),
                                "data_name": candidate.get("data_name"),
                                **metric_record,
                            }
                        )
                    normalized_extent_mean_abs_deltas.append(
                        float(metric["normalized_extent_mean_abs_delta"])
                    )
                    normalized_extent_max_abs_deltas.append(
                        float(metric["normalized_extent_max_abs_delta"])
                    )
                    center_delta_normalized_values.append(float(metric["center_delta_normalized"]))
                    scale_factors.append(float(metric["scale_factor_oot3d_per_n64"]))
                    metric_extent_ratios = metric.get("extent_ratio_oot3d_per_n64")
                    if isinstance(metric_extent_ratios, dict):
                        for axis in ("x", "y", "z"):
                            value = metric_extent_ratios.get(axis)
                            if isinstance(value, (int, float)) and math.isfinite(float(value)):
                                extent_ratios[axis].append(float(value))
                    volume_ratio = metric.get("volume_ratio_oot3d_per_n64")
                    if isinstance(volume_ratio, (int, float)):
                        volume_ratios.append(float(volume_ratio))

                comparisons.append(
                    {
                        "status": "measured" if pair_metrics else "incomplete",
                        "csab_name": oot3d_record.get("csab_name"),
                        "n64_name": candidate.get("name"),
                        "data_name": candidate.get("data_name"),
                        "sample_metric_count": len(pair_metrics),
                    }
                )
    except zipfile.BadZipFile as exc:
        return {
            "status": "invalid_n64_base_o2r",
            "acceptance_status": "not_started",
            "compared_match_count": 0,
            "sample_pair_count": 0,
            "issue_count": 1,
            "sample_metrics": [],
            "sample_issues": [{"type": "invalid_n64_base_o2r", "message": str(exc)}],
        }

    measured_pair_count = len(normalized_extent_mean_abs_deltas)
    status = "measured" if measured_pair_count > 0 and not issues else "incomplete"
    return {
        "status": status,
        "acceptance_status": "requires_thresholds" if status == "measured" else "not_started",
        "metric_policy": {
            "oot3d_reference": "sampled skinned CMB position bounds from the OOT3D pose batch",
            "n64_reference": "Shipwright O2R child-Link skeleton joint bounds under the decoded PlayerAnimation frame table",
            "time_domain": "same OOT3D/N64 normalized sample pairs as time_normalized_pose_samples",
            "scope": "scale/orientation sanity metric only; not per-vertex deformation acceptance",
        },
        "compared_match_count": len([comparison for comparison in comparisons if comparison["status"] == "measured"]),
        "sample_pair_count": sample_pair_count,
        "measured_pair_count": measured_pair_count,
        "issue_count": len(issues),
        "normalized_extent_mean_abs_delta": numeric_summary(normalized_extent_mean_abs_deltas),
        "normalized_extent_max_abs_delta": numeric_summary(normalized_extent_max_abs_deltas),
        "center_delta_normalized": numeric_summary(center_delta_normalized_values),
        "scale_factor_oot3d_per_n64": numeric_summary(scale_factors),
        "extent_ratio_oot3d_per_n64": {
            axis: numeric_summary(values)
            for axis, values in extent_ratios.items()
        },
        "volume_ratio_oot3d_per_n64": numeric_summary(volume_ratios),
        "sample_metrics": sample_metrics,
        "sample_comparisons": comparisons[:sample_limit],
        "sample_issues": issues[:sample_limit],
    }


def n64_frame_world_bounds(
    raw_payload: bytes,
    frame_index: int,
    skeleton_pose_reference: dict[str, object],
) -> dict[str, object] | None:
    values = n64_frame_s16_values(raw_payload, frame_index)
    if values is None:
        return None
    joint_vecs = [
        {"x": values[index * 3], "y": values[index * 3 + 1], "z": values[index * 3 + 2]}
        for index in range(N64_PLAYER_ANIMATION_ROTATION_TRIPLETS_PER_FRAME)
    ]
    limbs = skeleton_pose_reference.get("limbs", [])
    if not isinstance(limbs, list) or not limbs:
        return None
    root_translation = joint_vecs[0]
    root_rotation = joint_vecs[1]
    root_matrix = matrix_translate_rotate_zyx(root_translation, root_rotation)
    points = [matrix_translation(root_matrix)]

    root_limb = limbs[0] if isinstance(limbs[0], dict) else {}
    child_index = int_or_none(root_limb.get("child_index"))
    if child_index is not None and child_index != 255:
        collect_n64_limb_points(child_index, root_matrix, limbs, joint_vecs, points)

    bounds = bounds_from_points(points)
    if bounds is None:
        return None
    return {
        "bounds": bounds,
        "extent": position_extent_from_bounds(bounds),
        "diagonal": bounds_diagonal(bounds),
        "volume": aabb_volume(bounds),
        "point_count": len(points),
    }


def collect_n64_limb_points(
    limb_index: int,
    parent_matrix: list[list[float]],
    limbs: list[object],
    joint_vecs: list[dict[str, int]],
    points: list[dict[str, float]],
) -> None:
    while limb_index != 255 and 0 <= limb_index < len(limbs):
        limb = limbs[limb_index]
        if not isinstance(limb, dict):
            return
        joint_pos = limb.get("joint_pos", {})
        if not isinstance(joint_pos, dict):
            joint_pos = {}
        rotation_index = limb_index + 1
        rotation = joint_vecs[rotation_index] if rotation_index < len(joint_vecs) else {"x": 0, "y": 0, "z": 0}
        local_matrix = matrix_translate_rotate_zyx(joint_pos, rotation)
        world_matrix = matrix_multiply(parent_matrix, local_matrix)
        points.append(matrix_translation(world_matrix))

        child_index = int_or_none(limb.get("child_index"))
        if child_index is not None and child_index != 255:
            collect_n64_limb_points(child_index, world_matrix, limbs, joint_vecs, points)

        sibling_index = int_or_none(limb.get("sibling_index"))
        if sibling_index is None or sibling_index == 255:
            break
        limb_index = sibling_index


def n64_frame_s16_values(raw_payload: bytes, frame_index: int) -> list[int] | None:
    frame_byte_count = N64_PLAYER_ANIMATION_BYTES_PER_FRAME
    start = frame_index * frame_byte_count
    end = start + frame_byte_count
    if start < 0 or end > len(raw_payload):
        return None
    return [value[0] for value in struct.iter_unpack("<h", raw_payload[start:end])]


def pose_bounds_metric(oot3d_bounds: object, n64_bounds: object) -> dict[str, object] | None:
    oot3d_extent = position_extent_from_bounds(oot3d_bounds)
    n64_extent = position_extent_from_bounds(n64_bounds)
    if oot3d_extent is None or n64_extent is None:
        return None
    oot3d_diagonal = bounds_diagonal(oot3d_bounds)
    n64_diagonal = bounds_diagonal(n64_bounds)
    if oot3d_diagonal is None or n64_diagonal is None or oot3d_diagonal <= 0.0 or n64_diagonal <= 0.0:
        return None
    extent_deltas = {
        axis: abs((float(oot3d_extent[axis]) / oot3d_diagonal) - (float(n64_extent[axis]) / n64_diagonal))
        for axis in ("x", "y", "z")
    }
    extent_ratios = {
        axis: (
            float(oot3d_extent[axis]) / float(n64_extent[axis])
            if float(n64_extent[axis]) != 0.0
            else None
        )
        for axis in ("x", "y", "z")
    }
    oot3d_center = bounds_center(oot3d_bounds)
    n64_center = bounds_center(n64_bounds)
    if oot3d_center is None or n64_center is None:
        return None
    center_delta_normalized = math.sqrt(
        sum(
            (
                (float(oot3d_center[axis]) / oot3d_diagonal)
                - (float(n64_center[axis]) / n64_diagonal)
            )
            ** 2
            for axis in ("x", "y", "z")
        )
    )
    n64_volume = aabb_volume(n64_bounds)
    oot3d_volume = aabb_volume(oot3d_bounds)
    volume_ratio = (
        float(oot3d_volume) / float(n64_volume)
        if isinstance(oot3d_volume, (int, float))
        and isinstance(n64_volume, (int, float))
        and float(n64_volume) != 0.0
        else None
    )
    return {
        "oot3d_extent": rounded_xyz(oot3d_extent),
        "n64_extent": rounded_xyz(n64_extent),
        "oot3d_diagonal": round_metric(oot3d_diagonal),
        "n64_diagonal": round_metric(n64_diagonal),
        "scale_factor_oot3d_per_n64": round_metric(oot3d_diagonal / n64_diagonal),
        "extent_ratio_oot3d_per_n64": {
            axis: round_metric(value) if value is not None else None
            for axis, value in extent_ratios.items()
        },
        "normalized_extent_delta": rounded_xyz(extent_deltas),
        "normalized_extent_mean_abs_delta": round_metric(sum(extent_deltas.values()) / 3.0),
        "normalized_extent_max_abs_delta": round_metric(max(extent_deltas.values())),
        "center_delta_normalized": round_metric(center_delta_normalized),
        "volume_ratio_oot3d_per_n64": round_metric(volume_ratio) if volume_ratio is not None else None,
    }


def bounds_from_points(points: list[dict[str, float]]) -> dict[str, dict[str, float]] | None:
    if not points:
        return None
    bounds: dict[str, dict[str, float]] = {}
    for axis in ("x", "y", "z"):
        values = [float(point[axis]) for point in points if axis in point]
        if not values:
            return None
        bounds[axis] = {"min": round_metric(min(values)), "max": round_metric(max(values))}
    return bounds


def bounds_center(bounds: object) -> dict[str, float] | None:
    if not isinstance(bounds, dict):
        return None
    center: dict[str, float] = {}
    for axis in ("x", "y", "z"):
        values = bounds.get(axis)
        if not isinstance(values, dict):
            return None
        min_value = values.get("min")
        max_value = values.get("max")
        if not isinstance(min_value, (int, float)) or not isinstance(max_value, (int, float)):
            return None
        center[axis] = (float(min_value) + float(max_value)) / 2.0
    return center


def bounds_diagonal(bounds: object) -> float | None:
    extent = position_extent_from_bounds(bounds)
    if extent is None:
        return None
    return math.sqrt(sum(float(extent[axis]) ** 2 for axis in ("x", "y", "z")))


def matrix_translate_rotate_zyx(
    translation: dict[str, object],
    rotation: dict[str, object],
) -> list[list[float]]:
    x = float(translation.get("x", 0.0) or 0.0)
    y = float(translation.get("y", 0.0) or 0.0)
    z = float(translation.get("z", 0.0) or 0.0)
    rx = angle_s16_to_radians(int(rotation.get("x", 0) or 0))
    ry = angle_s16_to_radians(int(rotation.get("y", 0) or 0))
    rz = angle_s16_to_radians(int(rotation.get("z", 0) or 0))
    matrix = matrix_translate(x, y, z)
    matrix = matrix_multiply(matrix, matrix_rotate_z(rz))
    matrix = matrix_multiply(matrix, matrix_rotate_y(ry))
    matrix = matrix_multiply(matrix, matrix_rotate_x(rx))
    return matrix


def angle_s16_to_radians(value: int) -> float:
    return (value & 0xFFFF) * (2.0 * math.pi / 65536.0)


def matrix_identity() -> list[list[float]]:
    return [
        [1.0, 0.0, 0.0, 0.0],
        [0.0, 1.0, 0.0, 0.0],
        [0.0, 0.0, 1.0, 0.0],
        [0.0, 0.0, 0.0, 1.0],
    ]


def matrix_translate(x: float, y: float, z: float) -> list[list[float]]:
    matrix = matrix_identity()
    matrix[0][3] = x
    matrix[1][3] = y
    matrix[2][3] = z
    return matrix


def matrix_rotate_x(angle: float) -> list[list[float]]:
    c = math.cos(angle)
    s = math.sin(angle)
    return [
        [1.0, 0.0, 0.0, 0.0],
        [0.0, c, -s, 0.0],
        [0.0, s, c, 0.0],
        [0.0, 0.0, 0.0, 1.0],
    ]


def matrix_rotate_y(angle: float) -> list[list[float]]:
    c = math.cos(angle)
    s = math.sin(angle)
    return [
        [c, 0.0, s, 0.0],
        [0.0, 1.0, 0.0, 0.0],
        [-s, 0.0, c, 0.0],
        [0.0, 0.0, 0.0, 1.0],
    ]


def matrix_rotate_z(angle: float) -> list[list[float]]:
    c = math.cos(angle)
    s = math.sin(angle)
    return [
        [c, -s, 0.0, 0.0],
        [s, c, 0.0, 0.0],
        [0.0, 0.0, 1.0, 0.0],
        [0.0, 0.0, 0.0, 1.0],
    ]


def matrix_multiply(a: list[list[float]], b: list[list[float]]) -> list[list[float]]:
    return [
        [
            sum(a[row][inner] * b[inner][col] for inner in range(4))
            for col in range(4)
        ]
        for row in range(4)
    ]


def matrix_translation(matrix: list[list[float]]) -> dict[str, float]:
    return {
        "x": round_metric(matrix[0][3]),
        "y": round_metric(matrix[1][3]),
        "z": round_metric(matrix[2][3]),
    }


def numeric_summary(values: list[float]) -> dict[str, object]:
    if not values:
        return {"count": 0, "min": None, "max": None, "avg": None}
    return {
        "count": len(values),
        "min": round_metric(min(values)),
        "max": round_metric(max(values)),
        "avg": round_metric(sum(values) / len(values)),
    }


def rounded_xyz(values: dict[str, object]) -> dict[str, float]:
    return {
        axis: round_metric(float(values.get(axis, 0.0) or 0.0))
        for axis in ("x", "y", "z")
    }


def round_metric(value: float | None) -> float | None:
    if value is None:
        return None
    return round(float(value), 6)


def validation_scope_status_for(
    player_animation_data_records: list[dict[str, object]] | None,
    payload_decode_summary: dict[str, object],
    pose_sample_comparison: dict[str, object] | None = None,
    limb_mapping: dict[str, object] | None = None,
    pose_error_metric: dict[str, object] | None = None,
) -> str:
    if (
        pose_error_metric is not None
        and pose_error_metric.get("status") == "measured"
        and pose_sample_comparison is not None
        and pose_sample_comparison.get("status") == "sampled"
        and limb_mapping is not None
        and limb_mapping.get("status") == "candidate_ready_for_metric"
    ):
        return "candidate_mapping_n64_frame_counts_payload_decode_time_samples_limb_mapping_and_pose_metric"
    if (
        pose_sample_comparison is not None
        and pose_sample_comparison.get("status") == "sampled"
        and limb_mapping is not None
        and limb_mapping.get("status") == "candidate_ready_for_metric"
    ):
        return "candidate_mapping_n64_frame_counts_payload_decode_time_samples_and_limb_mapping"
    if pose_sample_comparison is not None and pose_sample_comparison.get("status") == "sampled":
        return "candidate_mapping_n64_frame_counts_payload_decode_and_time_samples"
    if payload_decode_summary.get("status") == "decoded":
        return "candidate_mapping_n64_frame_counts_and_payload_decode"
    if player_animation_data_records is not None:
        return "candidate_mapping_and_n64_frame_counts"
    return "candidate_mapping_only"


def n64_link_skeleton_summary(xml_path: Path, requested_name: str | None) -> dict[str, object]:
    root = load_xml(xml_path)
    object_file = next((element for element in root.iter() if element.tag == "File"), None)
    object_file_name = object_file.attrib.get("Name") if object_file is not None else None
    skeletons = [element for element in root.iter() if element.tag == "Skeleton"]
    skeleton = None
    if requested_name:
        for candidate in skeletons:
            if candidate.attrib.get("Name") == requested_name:
                skeleton = candidate
                break
    elif len(skeletons) == 1:
        skeleton = skeletons[0]

    limbs = [element for element in root.iter() if element.tag == "Limb"]
    dlists = [element for element in root.iter() if element.tag == "DList"]
    textures = [element for element in root.iter() if element.tag == "Texture"]
    resource_prefix = f"objects/{object_file_name}" if object_file_name else None
    return {
        "status": "resolved" if skeleton is not None else "missing_or_ambiguous",
        "requested_name": requested_name,
        "object_file_name": object_file_name,
        "resource_prefix": resource_prefix,
        "name": skeleton.attrib.get("Name") if skeleton is not None else None,
        "type": skeleton.attrib.get("Type") if skeleton is not None else None,
        "limb_type": skeleton.attrib.get("LimbType") if skeleton is not None else None,
        "offset": parse_int(skeleton.attrib.get("Offset")) if skeleton is not None else None,
        "skeleton_count": len(skeletons),
        "limb_count": len(limbs),
        "dlist_count": len(dlists),
        "texture_count": len(textures),
        "sample_limbs": [
            {
                "name": limb.attrib.get("Name"),
                "limb_type": limb.attrib.get("LimbType"),
                "offset": parse_int(limb.attrib.get("Offset")),
            }
            for limb in limbs[:25]
        ],
    }


def map_animation_candidates(
    oot3d_records: list[dict[str, object]],
    player_animation_records: list[dict[str, object]],
    sample_limit: int,
) -> dict[str, object]:
    exact_by_stem: dict[str, list[dict[str, object]]] = {}
    normalized_by_key: dict[str, list[dict[str, object]]] = {}
    for record in player_animation_records:
        exact_by_stem.setdefault(str(record["stem"]).lower(), []).append(record)
        normalized_by_key.setdefault(str(record["normalized_key"]), []).append(record)

    status_counts: Counter[str] = Counter()
    timing_status_counts: Counter[str] = Counter()
    matched_records: list[dict[str, object]] = []
    unmatched_records: list[dict[str, object]] = []
    ambiguous_records: list[dict[str, object]] = []
    runtime_mapping_entries: list[dict[str, object]] = []
    for record in oot3d_records:
        exact_candidates = exact_by_stem.get(str(record["stem"]).lower(), [])
        normalized_candidates = normalized_by_key.get(str(record["normalized_key"]), [])
        if len(exact_candidates) == 1:
            status = "single_exact_stem_match"
            candidates = exact_candidates
        elif len(exact_candidates) > 1:
            status = "multiple_exact_stem_matches"
            candidates = exact_candidates
        elif len(normalized_candidates) == 1:
            status = "single_normalized_key_match"
            candidates = normalized_candidates
        elif len(normalized_candidates) > 1:
            status = "multiple_normalized_key_matches"
            candidates = normalized_candidates
        else:
            status = "unmatched"
            candidates = []

        timing_summary = player_animation_timing_summary(record, candidates)
        timing_status_counts[timing_summary["timing_status"]] += 1
        status_counts[status] += 1
        mapped = {
            "csab_name": record["csab_name"],
            "oot3d_stem": record["stem"],
            "normalized_key": record["normalized_key"],
            "oot3d_frame_slot_count": record.get("frame_slot_count"),
            "status": status,
            "candidate_count": len(candidates),
            **timing_summary,
            "n64_candidates": [
                player_animation_candidate_summary(candidate)
                for candidate in candidates[:sample_limit]
            ],
        }
        if candidates:
            matched_records.append(mapped)
            if len(candidates) == 1:
                runtime_mapping_entries.append(
                    runtime_animation_mapping_entry(record, candidates[0], status, timing_summary)
                )
        else:
            unmatched_records.append(mapped)
        if len(candidates) > 1:
            ambiguous_records.append(mapped)

    extend_runtime_animation_mapping_entries(runtime_mapping_entries, oot3d_records, player_animation_records)
    matched_count = sum(count for status, count in status_counts.items() if status != "unmatched")
    return {
        "oot3d_animation_count": len(oot3d_records),
        "n64_player_animation_count": len(player_animation_records),
        "matched_oot3d_count": matched_count,
        "unmatched_oot3d_count": status_counts.get("unmatched", 0),
        "ambiguous_oot3d_count": len(ambiguous_records),
        "status_counts": sorted_counter(status_counts),
        "timing_status_counts": sorted_counter(timing_status_counts),
        "runtime_mapping": {
            "format": "oot3d_n64_animation_runtime_mapping_v1",
            "policy": "only non-ambiguous exact-stem or normalized-key matches are routed at runtime",
            "entry_count": len(runtime_mapping_entries),
            "entries": runtime_mapping_entries,
        },
        "sample_matches": matched_records[:sample_limit],
        "sample_unmatched": unmatched_records[:sample_limit],
        "sample_ambiguous": ambiguous_records[:sample_limit],
    }


def runtime_animation_mapping_entry(
    oot3d_record: dict[str, object],
    candidate: dict[str, object],
    match_status: str,
    timing_summary: dict[str, object],
) -> dict[str, object]:
    data_name = str(candidate.get("data_name") or "")
    resource_path = f"misc/link_animetion/{data_name}" if data_name else None
    return {
        "csab_name": oot3d_record.get("csab_name"),
        "oot3d_stem": oot3d_record.get("stem"),
        "oot3d_normalized_key": oot3d_record.get("normalized_key"),
        "n64_name": candidate.get("name"),
        "n64_stem": candidate.get("stem"),
        "n64_normalized_key": candidate.get("normalized_key"),
        "n64_data_name": data_name or None,
        "n64_resource_path": resource_path,
        "match_status": match_status,
        "timing_status": timing_summary.get("timing_status"),
        "oot3d_frame_slot_count": oot3d_record.get("frame_slot_count"),
        "n64_frame_count": candidate.get("frame_count"),
    }


def extend_runtime_animation_mapping_entries(
    entries: list[dict[str, object]],
    oot3d_records: list[dict[str, object]],
    player_animation_records: list[dict[str, object]],
) -> None:
    existing_n64_names = {str(entry.get("n64_name") or "") for entry in entries}
    oot3d_by_stem: dict[str, list[dict[str, object]]] = {}
    for record in oot3d_records:
        oot3d_by_stem.setdefault(str(record.get("stem") or "").lower(), []).append(record)

    for candidate in player_animation_records:
        n64_name = str(candidate.get("name") or "")
        if not n64_name or n64_name in existing_n64_names:
            continue
        oot3d_record = single_oot3d_record_for_runtime_stem_candidates(candidate, oot3d_by_stem)
        if oot3d_record is None:
            continue
        timing_summary = player_animation_timing_summary(oot3d_record, [candidate])
        entries.append(
            runtime_animation_mapping_entry(
                oot3d_record,
                candidate,
                "single_runtime_stem_alias_match",
                timing_summary,
            )
        )
        existing_n64_names.add(n64_name)


def single_oot3d_record_for_runtime_stem_candidates(
    candidate: dict[str, object],
    oot3d_by_stem: dict[str, list[dict[str, object]]],
) -> dict[str, object] | None:
    for stem in runtime_stem_candidates_for_n64(str(candidate.get("stem") or "")):
        matches = oot3d_by_stem.get(stem.lower(), [])
        if len(matches) == 1:
            return matches[0]
    return None


def runtime_stem_candidates_for_n64(stem: str) -> list[str]:
    stem = strip_n64_player_anim_prefix(path_stem(stem))
    candidates: list[str] = []
    child_specific = stem.lower().startswith(("clink_", "child_"))
    add_runtime_nml_stem_aliases(candidates, stem, False)
    for prefix in ("link_", "clink_", "child_"):
        if stem.lower().startswith(prefix):
            add_runtime_nml_stem_aliases(candidates, stem[len(prefix) :], child_specific)
    return candidates


def add_runtime_stem_candidate(out: list[str], stem: str) -> None:
    key = stem.strip().lower()
    if key and key not in out:
        out.append(key)


def add_runtime_nml_stem_aliases(out: list[str], stem: str, add_child_prefix: bool) -> None:
    stem = stem.strip()
    add_runtime_stem_candidate(out, stem)
    lower = stem.lower()
    if lower.startswith("normal_"):
        nml_stem = "nml_" + stem[len("normal_") :]
        if add_child_prefix:
            add_runtime_stem_candidate(out, "cl_" + nml_stem)
        add_runtime_stem_candidate(out, nml_stem)
    elif lower == "normal":
        if add_child_prefix:
            add_runtime_stem_candidate(out, "cl_nml")
        add_runtime_stem_candidate(out, "nml")
    elif add_child_prefix:
        add_runtime_stem_candidate(out, "cl_" + stem)


def player_animation_candidate_summary(candidate: dict[str, object]) -> dict[str, object]:
    keys = (
        "index",
        "name",
        "offset",
        "stem",
        "normalized_key",
        "data_name",
        "data_offset",
        "frame_count",
        "bytes_per_frame",
        "s16_values_per_frame",
        "expected_payload_byte_count",
        "expected_limb_rot_s16_count",
        "next_data_offset_delta",
        "padding_to_next_data",
        "payload_decode",
    )
    return {key: candidate.get(key) for key in keys if key in candidate}


def player_animation_timing_summary(
    oot3d_record: dict[str, object],
    candidates: list[dict[str, object]],
) -> dict[str, object]:
    oot3d_frame_slot_count = int_or_none(oot3d_record.get("frame_slot_count"))
    if not candidates:
        return {
            "timing_status": "unmatched",
            "n64_frame_count": None,
            "frame_delta": None,
            "frame_ratio": None,
        }
    if len(candidates) != 1:
        return {
            "timing_status": "ambiguous_candidates",
            "n64_frame_count": None,
            "frame_delta": None,
            "frame_ratio": None,
        }
    n64_frame_count = int_or_none(candidates[0].get("frame_count"))
    if oot3d_frame_slot_count is None or n64_frame_count is None or n64_frame_count == 0:
        return {
            "timing_status": "missing_frame_count",
            "n64_frame_count": n64_frame_count,
            "frame_delta": None,
            "frame_ratio": None,
        }
    frame_delta = oot3d_frame_slot_count - n64_frame_count
    return {
        "timing_status": "same_frame_count" if frame_delta == 0 else "different_frame_count",
        "n64_frame_count": n64_frame_count,
        "frame_delta": frame_delta,
        "frame_ratio": round(oot3d_frame_slot_count / n64_frame_count, 6),
    }


def int_or_none(value: object) -> int | None:
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        return parse_int(value)
    return None


def reference_blockers(
    target: dict[str, object] | None,
    skeleton_summary: dict[str, object],
    player_animation_records: list[dict[str, object]],
    player_animation_data_records: list[dict[str, object]] | None,
    payload_decode_summary: dict[str, object],
    pose_sample_comparison: dict[str, object],
    limb_mapping: dict[str, object],
    pose_error_metric: dict[str, object],
    candidate_mapping: dict[str, object],
) -> Counter[str]:
    blockers: Counter[str] = Counter()
    if target is None:
        blockers["missing_oot3d_target"] += 1
    if skeleton_summary.get("status") != "resolved":
        blockers["missing_n64_skeleton"] += 1
    if not player_animation_records:
        blockers["missing_n64_player_animation_inventory"] += 1
    if player_animation_data_records is not None:
        if len(player_animation_records) != len(player_animation_data_records):
            blockers["n64_player_animation_data_count_mismatch"] += 1
        missing_frame_count = sum(
            1
            for record in player_animation_data_records
            if record.get("frame_count") is None
        )
        if missing_frame_count:
            blockers["missing_n64_player_animation_data_frame_count"] += missing_frame_count
    if payload_decode_summary.get("status") not in ("not_provided", "decoded"):
        blockers["n64_player_animation_payload_decode_incomplete"] += int(
            payload_decode_summary.get("issue_count", 1) or 1
        )
    if pose_sample_comparison.get("status") not in ("not_provided", "sampled"):
        blockers["n64_oot3d_time_normalized_pose_sample_pairing_incomplete"] += int(
            pose_sample_comparison.get("issue_count", 1) or 1
        )
    if limb_mapping.get("status") in ("missing_oot3d_target",):
        blockers["n64_oot3d_limb_mapping_missing_target"] += int(limb_mapping.get("issue_count", 1) or 1)
    if pose_error_metric.get("status") not in ("not_provided", "measured"):
        blockers["n64_oot3d_pose_error_metric_incomplete"] += int(
            pose_error_metric.get("issue_count", 1) or 1
        )
    if int(candidate_mapping.get("matched_oot3d_count", 0) or 0) == 0:
        blockers["no_name_based_n64_candidates"] += 1
    blockers["total"] = sum(blockers.values())
    return blockers


def path_stem(value: object) -> str:
    name = normalize_path(value).split("/")[-1]
    return name.rsplit(".", 1)[0].lower()


def strip_n64_player_anim_prefix(name: str) -> str:
    return re.sub(r"^gPlayerAnim_", "", name, flags=re.IGNORECASE)


def canonical_animation_key(value: str, strip_prefixes: list[str]) -> str:
    key = path_stem(value)
    key = strip_n64_player_anim_prefix(key).lower()
    changed = True
    prefixes = [prefix.lower() for prefix in strip_prefixes if prefix]
    while changed:
        changed = False
        for prefix in prefixes:
            if key.startswith(prefix):
                key = key[len(prefix) :]
                changed = True
    return key
