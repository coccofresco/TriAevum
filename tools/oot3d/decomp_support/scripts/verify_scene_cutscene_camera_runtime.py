#!/usr/bin/env python3
"""Verify the reconstructed OOT3D cutscene camera runtime attach layer.

The checked behavior is limited to `FUN_0033CB1C` and the matching actor-pose
copy path used by `Camera_SetCSParams`; final camera update/projection remains a
separate decompilation target.
"""

from __future__ import annotations

import json
import math
import struct
import sys
from pathlib import Path
from typing import Any

import verify_scene_cutscene_camera_cmad_apply as cmad_verify


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BLOB_TABLE = ROOT / "analysis" / "scene_cutscene_camera_blob_table.json"
DEFAULT_CMAD_TABLE = ROOT / "analysis" / "scene_cutscene_camera_cmad_table.json"
DEFAULT_KEYFRAME_TABLE = ROOT / "analysis" / "scene_cutscene_camera_keyframe_table.json"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_cutscene_camera_runtime_verify.md"
DEFAULT_CODE_BIN = Path(r"E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin")

CODE_BIN_VA_BASE = 0x00100000
DAT_0033CB8C = 0x0033CB8C
DAT_002D8C3C = 0x002D8C3C
CUTSCENE_CS_PARAMS_REF_TOKEN = 0x94


def as_dict(value: Any) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


def as_list(value: Any) -> list[Any]:
    return value if isinstance(value, list) else []


def f32_from_bits(bits: int) -> float:
    return struct.unpack("<f", struct.pack("<I", bits & 0xFFFFFFFF))[0]


def read_codebin_word(path: Path, va: int) -> int | None:
    if not path.exists():
        return None
    data = path.read_bytes()
    offset = va - CODE_BIN_VA_BASE
    if offset < 0 or offset + 4 > len(data):
        return None
    return struct.unpack_from("<I", data, offset)[0]


def runtime_attach_fields(csparams_ref: int, actor_ref: int, mode_flag: int, actor_pose: dict[str, Any] | None) -> dict[str, Any]:
    fields: dict[str, Any] = {
        "field16C": csparams_ref,
        "field170": 0,
        "field174": (mode_flag | 4) & 0xFFFF,
        "field1A6": 0,
        "fieldD8": 0,
        "fieldDCPose": None,
        "field120": 0.0,
        "field128": 0.0,
        "field19E": 0,
        "hasActorTrackingFields": False,
    }
    if mode_flag != 0:
        if actor_pose is None:
            raise ValueError("modeFlag != 0 requires actor pose")
        fields["fieldD8"] = actor_ref
        fields["fieldDCPose"] = dict(actor_pose)
        fields["field19E"] = -1
        fields["hasActorTrackingFields"] = True
    return fields


def deg_to_binang(degrees: float) -> int:
    raw = int(degrees * 182.04167 + 0.5) & 0xFFFF
    return raw - 0x10000 if raw & 0x8000 else raw


def sign16(value: int) -> int:
    value &= 0xFFFF
    return value - 0x10000 if value & 0x8000 else value


def rad_to_binang(radians: float) -> int:
    return deg_to_binang(radians * (180.0 / math.pi))


def binang_to_rad(angle: int) -> float:
    signed = ((angle + 0x8000) & 0xFFFF) - 0x8000
    return signed * (math.pi / 32768.0)


def sins(angle: int) -> float:
    return math.sin(binang_to_rad(angle))


def coss(angle: int) -> float:
    return math.cos(binang_to_rad(angle))


def vec3f_to_sph_geo(vec: dict[str, float]) -> dict[str, float | int]:
    dist_squared = vec["x"] * vec["x"] + vec["z"] * vec["z"]
    dist = math.sqrt(dist_squared)
    if dist == 0.0 and vec["y"] == 0.0:
        pitch = 0
    else:
        pitch = rad_to_binang(math.atan2(dist, vec["y"]))
    radius = math.sqrt(vec["y"] * vec["y"] + dist_squared)
    if vec["x"] == 0.0 and vec["z"] == 0.0:
        yaw = 0
    else:
        yaw = rad_to_binang(math.atan2(vec["x"], vec["z"]))
    pitch = ((0x3FFF - pitch + 0x8000) & 0xFFFF) - 0x8000
    return {"r": radius, "pitch": pitch, "yaw": yaw}


def diff_to_sph_geo(from_: dict[str, float], to: dict[str, float]) -> dict[str, float | int]:
    return vec3f_to_sph_geo(
        {
            "x": to["x"] - from_["x"],
            "y": to["y"] - from_["y"],
            "z": to["z"] - from_["z"],
        }
    )


def calc_up_from_pitch_yaw_roll(pitch: int, yaw: int, roll: int) -> dict[str, float]:
    sin_pitch = sins(pitch)
    cos_pitch = coss(pitch)
    sin_yaw = sins(yaw)
    cos_yaw = coss(yaw)
    neg_sin_pitch = -sin_pitch
    sin_neg_roll = sins(-roll)
    cos_neg_roll = coss(-roll)
    neg_sin_pitch_sin_yaw = neg_sin_pitch * sin_yaw
    one_minus_cos_neg_roll = 1.0 - cos_neg_roll
    cos_pitch_sin_yaw = cos_pitch * sin_yaw
    cos_pitch_sin_yaw_sq = cos_pitch_sin_yaw * cos_pitch_sin_yaw
    sp4c = (cos_pitch_sin_yaw * sin_pitch) * one_minus_cos_neg_roll
    cos_pitch_cos_yaw = cos_pitch * cos_yaw
    temp_f4 = ((1.0 - cos_pitch_sin_yaw_sq) * cos_neg_roll) + cos_pitch_sin_yaw_sq
    cos_pitch_cos_yaw_sin_roll = cos_pitch_cos_yaw * sin_neg_roll
    neg_sin_pitch_cos_yaw = neg_sin_pitch * cos_yaw
    temp_f6 = (cos_pitch_cos_yaw * cos_pitch_sin_yaw) * one_minus_cos_neg_roll
    temp_f10 = sin_pitch * sin_neg_roll
    sin_pitch_sq = sin_pitch * sin_pitch
    temp_f4b = (sin_pitch * cos_pitch_cos_yaw) * one_minus_cos_neg_roll
    temp_f8b = cos_pitch_sin_yaw * sin_neg_roll
    temp_f8 = sp4c + cos_pitch_cos_yaw_sin_roll
    temp_f8c = temp_f6 - temp_f10
    return {
        "x": ((neg_sin_pitch_sin_yaw * temp_f4) + (cos_pitch * (sp4c - cos_pitch_cos_yaw_sin_roll)))
        + (neg_sin_pitch_cos_yaw * (temp_f6 + temp_f10)),
        "y": (
            (neg_sin_pitch_sin_yaw * temp_f8)
            + (cos_pitch * (((1.0 - sin_pitch_sq) * cos_neg_roll) + sin_pitch_sq))
        )
        + (neg_sin_pitch_cos_yaw * (temp_f4b - temp_f8b)),
        "z": ((neg_sin_pitch_sin_yaw * temp_f8c) + (cos_pitch * (temp_f4b + temp_f8b)))
        + (
            neg_sin_pitch_cos_yaw
            * (((1.0 - (cos_pitch_cos_yaw * cos_pitch_cos_yaw)) * cos_neg_roll) + (cos_pitch_cos_yaw * cos_pitch_cos_yaw))
        ),
    }


def project_view(state: dict[str, Any], perturbation: dict[str, Any] | None = None) -> dict[str, Any]:
    at = {"x": float(state["csParam80"]), "y": float(state["csParam84"]), "z": float(state["csParam88"])}
    eye = {"x": float(state["csParam8C"]), "y": float(state["csParam90"]), "z": float(state["csParam94"])}
    fov = float(state["csParam144"])
    has_perturbation = bool(perturbation and perturbation.get("enabled"))
    perturbation_magnitude = 0.0
    if has_perturbation and perturbation is not None:
        at_offset = perturbation["atOffset"]
        eye_offset = perturbation["eyeOffset"]
        at = {axis: at[axis] + float(at_offset[axis]) for axis in ["x", "y", "z"]}
        eye = {axis: eye[axis] + float(eye_offset[axis]) for axis in ["x", "y", "z"]}
        fov += float(perturbation["fovOffsetBinang"]) * f32_from_bits(0x3BB400B9)
        perturbation_magnitude = float(perturbation["maxMagnitude"])
    eye_to_at = diff_to_sph_geo(eye, at)
    pitch = int(eye_to_at["pitch"])
    yaw = int(eye_to_at["yaw"])
    if has_perturbation and perturbation is not None:
        pitch = sign16(pitch + int(perturbation["pitchOffset"]))
        yaw = sign16(yaw + int(perturbation["yawOffset"]))
    up = calc_up_from_pitch_yaw_roll(pitch, yaw, int(state["csParam1A2"]))
    return {
        "eye": eye,
        "at": at,
        "eyeToAt": eye_to_at,
        "up": up,
        "fov": fov,
        "viewDistD0": float(state["csParamD0"]),
        "perturbationMagnitude": perturbation_magnitude,
        "roll": int(state["csParam1A2"]),
        "hasPerturbation": has_perturbation,
    }


def ensure_view_finite(view: dict[str, Any], label: str) -> list[str]:
    errors: list[str] = []
    for vec_name in ["eye", "at", "up"]:
        for axis in ["x", "y", "z"]:
            if not math.isfinite(float(view[vec_name][axis])):
                errors.append(f"{label}: {vec_name}.{axis} is not finite")
    for scalar_name in ["fov", "viewDistD0"]:
        if not math.isfinite(float(view[scalar_name])):
            errors.append(f"{label}: {scalar_name} is not finite")
    up_len = math.sqrt(sum(float(view["up"][axis]) ** 2 for axis in ["x", "y", "z"]))
    if not 0.98 <= up_len <= 1.02:
        errors.append(f"{label}: up vector length outside tolerance: {up_len}")
    return errors


def build_segment_state(
    segment: dict[str, Any],
    frame: int,
    cmads_by_segment: dict[int, list[dict[str, Any]]],
    curves: list[dict[str, Any]],
    refs: list[dict[str, Any]],
    keyframes: list[dict[str, Any]],
) -> dict[str, Any]:
    state = cmad_verify.init_state(segment)
    segment_index = int(segment["camera_blob_segment_source_index"])
    for cmad in cmads_by_segment.get(segment_index, []):
        start = int(cmad["curve_ref_start"])
        count = int(cmad["curve_ref_count"])
        for curve in curves[start : start + count]:
            curve_index = int(curve["camera_curve_source_index"])
            ref = refs[curve_index]
            keyframe_start = int(ref["keyframe_ref_start"])
            keyframe_count = int(ref["keyframe_ref_count"])
            value = cmad_verify.sample_type2(
                curve,
                keyframes[keyframe_start : keyframe_start + keyframe_count],
                float(frame),
            )
            cmad_verify.apply_curve(state, curve, value)
    return state


def write_markdown(path: Path, summary: dict[str, Any], samples: list[dict[str, Any]]) -> None:
    lines = [
        "# Scene Cutscene Camera Runtime Verification",
        "",
        "Verification for the runtime attach layer reconstructed from `FUN_0033CB1C`, `Camera_SetCSParams`, `Actor_GetWorldPosShapeRot`, and `FUN_00371738`.",
        "",
        "## Summary",
        "",
        f"- Segments checked through runtime attach wrapper: {summary['segments_checked']}",
        f"- Intro segments checked: {summary['intro_segments_checked']}",
        f"- `DAT_0033CB8C` bits from `code.bin`: `{summary['dat_0033cb8c_bits']}`",
        f"- `DAT_0033CB8C` float: `{summary['dat_0033cb8c_float']}`",
        f"- `DAT_002D8C3C` FOV binang-to-degree scale: `{summary['dat_002d8c3c_float']}`",
        f"- Mode 0 attach fields checked: {summary['mode0_checks']}",
        f"- Actor-tracking branch checks: {summary['actor_branch_checks']}",
        f"- Synthetic perturbation checks: {summary['perturbation_checks']}",
        f"- View projections checked: {summary['view_projection_checks']}",
        f"- Status: `{summary['status']}`",
        "",
        "## Native Evidence",
        "",
        "- `FUN_0033CB1C` writes `camera+0x16C = param_2`, `camera+0x170 = 0`, `camera+0x174 = param_4 | 4`, and `camera+0x1A6 = 0`.",
        "- When `param_4 != 0`, it stores the actor ref at `camera+0xD8`, copies 0x12 bytes from `Actor_GetWorldPosShapeRot` into `camera+0xDC`, writes `DAT_0033CB8C` to `camera+0x120/+0x128`, and writes `-1` to `camera+0x19E`.",
        "- `Actor_GetWorldPosShapeRot` sources actor `world.pos` from `actor+0x28` and shape rotation halfwords from `actor+0xBC/+0xBE/+0xC0`; `FUN_00371738` is the byte copy helper, so the copied camera payload is exactly `Vec3f + Vec3s` (0x12 bytes).",
        "- Observed `Cutscene_ProcessCommands`, `EnZl4_Update`, and `FUN_00347AEC` camera-cmad callsites pass `param_4 == 0`, so actor tracking fields are not native writes for those flows.",
        "- `Camera_Update` maps `camera+0x80/0x84/0x88` to view `at`, `camera+0x8C/0x90/0x94` to view `eye`, `camera+0x98/0x9C/0xA0` to computed `up`, `camera+0x144` to FOV, and `camera+0xD0` to the view-distance input later clamped by collision/distortion logic.",
        "- The optional `FUN_004787E8` perturbation output starts with `atOffset` at `+0x00`, then `eyeOffset` at `+0x0C`, then pitch/yaw/FOV offsets and max magnitude; `Camera_Update` adds those vectors to `camera+0x80` (`at`) and `camera+0x8C` (`eye`) respectively.",
        "",
        "## Intro Samples",
        "",
        "| segment | scene | setup | frame | field16c token | at | eye | fov | roll | actor fields |",
        "| ---: | --- | ---: | ---: | ---: | --- | --- | ---: | ---: | --- |",
    ]
    for sample in samples[:16]:
        fields = sample["fields"]
        view = sample["view"]
        lines.append(
            f"| {sample['segment']} | `{sample['scene']}` | {sample['setup']} | {sample['frame']} | "
            f"`0x{fields['field16C']:08x}` | "
            f"({view['at']['x']:.3g}, {view['at']['y']:.3g}, {view['at']['z']:.3g}) | "
            f"({view['eye']['x']:.3g}, {view['eye']['y']:.3g}, {view['eye']['z']:.3g}) | "
            f"{view['fov']:.3g} | {view['roll']} | "
            f"{'written' if fields['hasActorTrackingFields'] else 'not written'} |"
        )
    lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def main() -> int:
    blob_table = json.loads(DEFAULT_BLOB_TABLE.read_text(encoding="utf-8"))
    cmad_table = json.loads(DEFAULT_CMAD_TABLE.read_text(encoding="utf-8"))
    keyframe_table = json.loads(DEFAULT_KEYFRAME_TABLE.read_text(encoding="utf-8"))
    segments = [as_dict(row) for row in as_list(blob_table.get("camera_blob_segment_rows"))]
    cmads = [as_dict(row) for row in as_list(cmad_table.get("cmad_record_rows"))]
    curves = [as_dict(row) for row in as_list(cmad_table.get("camera_curve_rows"))]
    keyframes = [as_dict(row) for row in as_list(keyframe_table.get("keyframe_rows"))]
    refs = [as_dict(row) for row in as_list(keyframe_table.get("curve_keyframe_ref_rows"))]
    cmads_by_segment: dict[int, list[dict[str, Any]]] = {}
    for cmad in cmads:
        cmads_by_segment.setdefault(int(cmad["camera_blob_segment_source_index"]), []).append(cmad)

    literal_bits = read_codebin_word(DEFAULT_CODE_BIN, DAT_0033CB8C)
    errors: list[str] = []
    if literal_bits is None:
        literal_bits_text = "unavailable"
        literal_float_text = "unavailable"
    else:
        literal_float = f32_from_bits(literal_bits)
        literal_bits_text = f"0x{literal_bits:08x}"
        literal_float_text = f"{literal_float}"
        if literal_bits != 0:
            errors.append(f"DAT_0033CB8C expected 0x00000000, got 0x{literal_bits:08x}")
        if not math.isfinite(literal_float):
            errors.append(f"DAT_0033CB8C is not finite: {literal_float}")
    fov_scale_bits = read_codebin_word(DEFAULT_CODE_BIN, DAT_002D8C3C)
    fov_scale_float_text = "unavailable"
    if fov_scale_bits is not None:
        fov_scale_float = f32_from_bits(fov_scale_bits)
        fov_scale_float_text = f"{fov_scale_float}"
        if fov_scale_bits != 0x3BB400B9:
            errors.append(f"DAT_002D8C3C expected 0x3bb400b9, got 0x{fov_scale_bits:08x}")

    mode0_checks = 0
    view_projection_checks = 0
    intro_segments_checked = 0
    samples: list[dict[str, Any]] = []
    for segment in segments:
        segment_index = int(segment["camera_blob_segment_source_index"])
        frame = int(segment["start_frame"]) + 1
        if frame >= int(segment["end_frame"]):
            frame = int(segment["start_frame"])
        fields = runtime_attach_fields(CUTSCENE_CS_PARAMS_REF_TOKEN, 0, 0, None)
        mode0_checks += 1
        if fields["field170"] != 0:
            errors.append(f"segment {segment_index}: field170 not zero")
        if fields["field174"] != 4:
            errors.append(f"segment {segment_index}: mode0 field174 expected 4, got {fields['field174']}")
        if fields["field1A6"] != 0:
            errors.append(f"segment {segment_index}: field1A6 not zero")
        if fields["hasActorTrackingFields"]:
            errors.append(f"segment {segment_index}: mode0 unexpectedly wrote actor tracking fields")
        state = build_segment_state(segment, frame, cmads_by_segment, curves, refs, keyframes)
        view = project_view(state)
        view_projection_checks += 1
        errors.extend(ensure_view_finite(view, f"segment {segment_index} view"))
        if segment.get("scene_path") in {"link_info.zsi", "spot04_info.zsi"}:
            intro_segments_checked += 1
            samples.append(
                {
                    "segment": segment_index,
                    "scene": segment.get("scene_path"),
                    "setup": segment.get("setup_index"),
                    "frame": frame,
                    "fields": fields,
                    "view": view,
                }
            )

    actor_pose = {
        "worldPosX": 1.25,
        "worldPosY": -2.5,
        "worldPosZ": 3.75,
        "shapeRotX": -0x1234,
        "shapeRotY": 0x2345,
        "shapeRotZ": -0x3456,
    }
    actor_fields = runtime_attach_fields(0x94, 0x20AC, 1, actor_pose)
    actor_branch_checks = 1
    if actor_fields["field174"] != 5:
        errors.append(f"actor branch field174 expected 5, got {actor_fields['field174']}")
    if actor_fields["fieldD8"] != 0x20AC:
        errors.append("actor branch did not retain actor ref")
    if actor_fields["fieldDCPose"] != actor_pose:
        errors.append("actor branch did not copy 0x12-byte pose payload fields")
    if actor_fields["field120"] != 0.0 or actor_fields["field128"] != 0.0:
        errors.append("actor branch field120/field128 reset mismatch")
    if actor_fields["field19E"] != -1:
        errors.append(f"actor branch field19E expected -1, got {actor_fields['field19E']}")
    try:
        runtime_attach_fields(0x94, 0x20AC, 2, None)
        errors.append("missing actor pose branch did not raise")
    except ValueError:
        actor_branch_checks += 1

    synthetic_state = {
        "csParam80": 10.0,
        "csParam84": 20.0,
        "csParam88": 30.0,
        "csParam8C": -10.0,
        "csParam90": -20.0,
        "csParam94": -30.0,
        "csParam144": 45.0,
        "csParamD0": 100.0,
        "csParam1A2": 0x123,
    }
    synthetic_perturbation = {
        "atOffset": {"x": 1.0, "y": 2.0, "z": 3.0},
        "eyeOffset": {"x": -4.0, "y": -5.0, "z": -6.0},
        "pitchOffset": 0x10,
        "yawOffset": -0x20,
        "fovOffsetBinang": 2,
        "maxMagnitude": 7.5,
        "enabled": True,
    }
    synthetic_view = project_view(synthetic_state, synthetic_perturbation)
    perturbation_checks = 1
    expected_at = {"x": 11.0, "y": 22.0, "z": 33.0}
    expected_eye = {"x": -14.0, "y": -25.0, "z": -36.0}
    if synthetic_view["at"] != expected_at:
        errors.append(f"synthetic perturbation atOffset order mismatch: {synthetic_view['at']}")
    if synthetic_view["eye"] != expected_eye:
        errors.append(f"synthetic perturbation eyeOffset order mismatch: {synthetic_view['eye']}")
    expected_fov = 45.0 + (2.0 * f32_from_bits(0x3BB400B9))
    if not math.isclose(float(synthetic_view["fov"]), expected_fov, rel_tol=0.0, abs_tol=1e-7):
        errors.append(f"synthetic perturbation fov mismatch: {synthetic_view['fov']}")
    if synthetic_view["perturbationMagnitude"] != 7.5:
        errors.append("synthetic perturbation max magnitude mismatch")

    summary = {
        "segments_checked": len(segments),
        "intro_segments_checked": intro_segments_checked,
        "dat_0033cb8c_bits": literal_bits_text,
        "dat_0033cb8c_float": literal_float_text,
        "dat_002d8c3c_float": fov_scale_float_text,
        "mode0_checks": mode0_checks,
        "actor_branch_checks": actor_branch_checks,
        "perturbation_checks": perturbation_checks,
        "view_projection_checks": view_projection_checks,
        "status": "pass" if not errors else "fail",
    }
    write_markdown(DEFAULT_OUT_MD, summary, samples)
    if errors:
        for error in errors[:50]:
            print(f"error: {error}", file=sys.stderr)
        if len(errors) > 50:
            print(f"error: {len(errors) - 50} additional errors omitted", file=sys.stderr)
        return 1

    print(
        "verified scene cutscene camera runtime attach/view: "
        f"{len(segments)} segments, {intro_segments_checked} intro segments, literal {literal_bits_text}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
