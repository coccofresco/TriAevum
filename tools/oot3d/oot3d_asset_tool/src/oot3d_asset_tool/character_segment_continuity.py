from __future__ import annotations

from collections import Counter
import json
import math
from pathlib import Path

from .binary import ParseError
from .character_conversion_package import bind_pose_skeleton_bones, selected_draw_world_transforms
from .cmb import Vec3
from .legacy_fast_resource import transform_position


CHARACTER_SEGMENT_CONTINUITY_AUDIT_FORMAT = "oot3d_character_segment_continuity_audit_v1"


def audit_character_segment_continuity(
    character_manifest_path: Path,
    output_path: Path,
    *,
    csab_names: list[str],
    root_motion_bone: int | None = None,
    tolerance: float = 0.001,
    sample_limit: int = 25,
) -> dict[str, object]:
    if not csab_names:
        raise ParseError("at least one CSAB name is required")
    if not math.isfinite(tolerance) or tolerance < 0.0:
        raise ParseError(f"tolerance must be finite and non-negative, got {tolerance!r}")

    character_manifest = load_json(character_manifest_path)
    if character_manifest.get("format") != "oot3d_character_conversion_manifest_v1":
        raise ParseError(f"{character_manifest_path}: unexpected character conversion manifest format")

    target = require_dict(character_manifest.get("target"), "character target")
    bind_pose = require_dict(target.get("bind_pose"), "character target bind_pose")
    bind_pose_export = bind_pose.get("export")
    if not bind_pose_export:
        raise ParseError("character manifest has no target bind_pose export")
    bind_pose_json = load_json(Path(str(bind_pose_export)))
    skeleton_bones = bind_pose_skeleton_bones(bind_pose_json)

    if root_motion_bone is None:
        reference = character_manifest.get("n64_reference_validation")
        if isinstance(reference, dict):
            candidate = reference.get("n64_limb_mapping_root_motion_bone")
            if isinstance(candidate, int) and not isinstance(candidate, bool):
                root_motion_bone = candidate
    if root_motion_bone is None:
        root_motion_bone = 0
    if root_motion_bone < 0 or root_motion_bone >= len(skeleton_bones):
        raise ParseError(f"root motion bone {root_motion_bone} is outside skeleton size {len(skeleton_bones)}")

    binding_manifest_path = require_dict(character_manifest.get("source_manifests"), "source manifests").get(
        "skinned_animation_binding"
    )
    if not binding_manifest_path:
        raise ParseError("character manifest has no skinned animation binding manifest")
    binding_manifest = load_json(Path(str(binding_manifest_path)))
    track_paths = character_track_paths(
        binding_manifest,
        archive_path=str(target.get("archive_path", "")),
        cmb_name=str(target.get("target_cmb_name", "")),
    )

    segment_records: list[dict[str, object]] = []
    for csab_name in csab_names:
        track_path = track_paths.get(normalize_path(csab_name))
        if track_path is None:
            raise ParseError(f"binding manifest has no track export for {csab_name}")
        track_json = load_json(track_path)
        frame_count = json_int(track_json.get("frame_count_candidate"))
        if frame_count < 0:
            raise ParseError(f"{track_path}: track has no valid frame_count_candidate")
        tracks = track_json.get("tracks")
        if not isinstance(tracks, list):
            raise ParseError(f"{track_path}: track has no track array")
        start = root_position(skeleton_bones, tracks, root_motion_bone, 0)
        end = root_position(skeleton_bones, tracks, root_motion_bone, frame_count)
        segment_records.append(
            {
                "csab_name": normalize_path(csab_name),
                "track_export": str(track_path),
                "frame_count_candidate": frame_count,
                "frame_slot_count": json_int(track_json.get("frame_slot_count"), frame_count + 1),
                "root_start": vec3_record(start),
                "root_end": vec3_record(end),
                "local_delta": vec3_record(sub_vec3(end, start)),
            }
        )

    normalized_offsets: list[Vec3] = [Vec3(0.0, 0.0, 0.0)]
    transition_records: list[dict[str, object]] = []
    counts = Counter()
    max_raw_delta = 0.0
    max_normalized_delta = 0.0
    max_raw_record: dict[str, object] | None = None
    for index in range(len(segment_records) - 1):
        left = segment_records[index]
        right = segment_records[index + 1]
        left_end = add_vec3(vec3_from_record(left["root_end"]), normalized_offsets[index])
        right_start_raw = vec3_from_record(right["root_start"])
        right_offset = sub_vec3(left_end, right_start_raw)
        normalized_offsets.append(right_offset)
        raw_delta = sub_vec3(right_start_raw, vec3_from_record(left["root_end"]))
        raw_distance = vec3_length(raw_delta)
        normalized_delta = sub_vec3(add_vec3(right_start_raw, right_offset), left_end)
        normalized_distance = vec3_length(normalized_delta)
        max_raw_delta, max_raw_record = update_max_record(
            max_raw_delta,
            max_raw_record,
            raw_distance,
            left["csab_name"],
            right["csab_name"],
            raw_delta,
        )
        max_normalized_delta = max(max_normalized_delta, normalized_distance)
        if raw_distance > tolerance:
            counts["raw_discontinuity_count"] += 1
        if normalized_distance > tolerance:
            counts["normalized_discontinuity_count"] += 1
        transition_records.append(
            {
                "from_csab_name": left["csab_name"],
                "to_csab_name": right["csab_name"],
                "raw_delta": vec3_record(raw_delta),
                "raw_distance": round_float(raw_distance),
                "normalization_offset": vec3_record(right_offset),
                "normalized_delta": vec3_record(normalized_delta),
                "normalized_distance": round_float(normalized_distance),
                "status": "continuous_after_offset" if normalized_distance <= tolerance else "not_continuous",
            }
        )

    normalized_segments: list[dict[str, object]] = []
    for record, offset in zip(segment_records, normalized_offsets):
        normalized_segments.append(
            {
                "csab_name": record["csab_name"],
                "normalization_offset": vec3_record(offset),
                "normalized_root_start": vec3_record(add_vec3(vec3_from_record(record["root_start"]), offset)),
                "normalized_root_end": vec3_record(add_vec3(vec3_from_record(record["root_end"]), offset)),
            }
        )

    status = "valid" if counts["normalized_discontinuity_count"] == 0 else "invalid"
    audit = {
        "format": CHARACTER_SEGMENT_CONTINUITY_AUDIT_FORMAT,
        "status": status,
        "character_manifest": str(character_manifest_path),
        "binding_manifest": str(binding_manifest_path),
        "root_motion_bone": root_motion_bone,
        "tolerance": tolerance,
        "segment_count": len(segment_records),
        "transition_count": len(transition_records),
        "raw_discontinuity_count": counts["raw_discontinuity_count"],
        "normalized_discontinuity_count": counts["normalized_discontinuity_count"],
        "max_raw_transition_delta": round_float(max_raw_delta),
        "max_raw_transition_delta_record": max_raw_record,
        "max_normalized_transition_delta": round_float(max_normalized_delta),
        "segments": segment_records[:sample_limit],
        "transitions": transition_records[:sample_limit],
        "normalization_plan": {
            "policy": "cumulative_segment_root_offset",
            "contract": (
                "Offsets are derived from the authored root-motion bone endpoints of adjacent CSAB segments. "
                "The first segment remains unshifted; each following segment is translated so its root start "
                "matches the previous normalized root end. This records a reusable data normalization plan "
                "without hardcoding individual animation names in runtime drawing code."
            ),
            "segments": normalized_segments[:sample_limit],
        },
    }
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8", newline="\n")
    return audit


def character_track_paths(
    binding_manifest: dict[str, object],
    *,
    archive_path: str,
    cmb_name: str,
) -> dict[str, Path]:
    archive = normalize_path(archive_path)
    cmb = normalize_path(cmb_name)
    targets = binding_manifest.get("targets")
    if not isinstance(targets, list):
        raise ParseError("skinned animation binding manifest has no target array")
    for target in targets:
        if not isinstance(target, dict):
            continue
        if normalize_path(target.get("archive_path")) != archive:
            continue
        if normalize_path(target.get("target_cmb_name")) != cmb:
            continue
        out: dict[str, Path] = {}
        animations = target.get("animations")
        if not isinstance(animations, list):
            return out
        for animation in animations:
            if not isinstance(animation, dict):
                continue
            csab_name = normalize_path(animation.get("csab_name"))
            track_path = animation.get("track_export_file")
            if csab_name and track_path:
                out[csab_name] = Path(str(track_path))
        return out
    raise ParseError(f"binding manifest has no target for {archive}!{cmb}")


def root_position(
    skeleton_bones: tuple[tuple[int, Vec3, Vec3, Vec3, object], ...],
    tracks: list[object],
    root_motion_bone: int,
    frame: int,
) -> Vec3:
    world = selected_draw_world_transforms(skeleton_bones, tracks, frame)
    return transform_position(world[root_motion_bone], Vec3(0.0, 0.0, 0.0))


def update_max_record(
    current_delta: float,
    current_record: dict[str, object] | None,
    candidate_delta: float,
    from_csab: object,
    to_csab: object,
    delta: Vec3,
) -> tuple[float, dict[str, object] | None]:
    if candidate_delta <= current_delta:
        return current_delta, current_record
    return candidate_delta, {
        "from_csab_name": from_csab,
        "to_csab_name": to_csab,
        "delta": vec3_record(delta),
        "distance": round_float(candidate_delta),
    }


def require_dict(value: object, label: str) -> dict[str, object]:
    if not isinstance(value, dict):
        raise ParseError(f"{label} must be an object")
    return value


def load_json(path: Path) -> dict[str, object]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except OSError as exc:
        raise ParseError(f"{path}: failed to read JSON") from exc
    except json.JSONDecodeError as exc:
        raise ParseError(f"{path}: invalid JSON") from exc
    if not isinstance(value, dict):
        raise ParseError(f"{path}: JSON root must be an object")
    return value


def normalize_path(value: object) -> str:
    return str(value or "").replace("\\", "/").strip("/")


def json_int(value: object, fallback: int = -1) -> int:
    if isinstance(value, int) and not isinstance(value, bool):
        return value
    return fallback


def vec3_record(value: Vec3) -> list[float]:
    return [round_float(value.x), round_float(value.y), round_float(value.z)]


def vec3_from_record(value: object) -> Vec3:
    if not isinstance(value, list) or len(value) != 3:
        raise ParseError("expected a 3-component vector record")
    return Vec3(float(value[0]), float(value[1]), float(value[2]))


def add_vec3(left: Vec3, right: Vec3) -> Vec3:
    return Vec3(left.x + right.x, left.y + right.y, left.z + right.z)


def sub_vec3(left: Vec3, right: Vec3) -> Vec3:
    return Vec3(left.x - right.x, left.y - right.y, left.z - right.z)


def vec3_length(value: Vec3) -> float:
    return math.sqrt(value.x * value.x + value.y * value.y + value.z * value.z)


def round_float(value: float) -> float:
    return round(float(value), 6)
