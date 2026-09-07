#!/usr/bin/env python3
"""Verify reconstructed OOT3D cutscene camera `cmad` application.

This mirrors the parts of `FUN_0033CB90` that seed camera state from a `caad`
segment and apply `cmad` curves through `FUN_003087A4`. Inputs are the decoded
OOT3D native asset tables only.
"""

from __future__ import annotations

import json
import math
import struct
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BLOB_TABLE = ROOT / "analysis" / "scene_cutscene_camera_blob_table.json"
DEFAULT_CMAD_TABLE = ROOT / "analysis" / "scene_cutscene_camera_cmad_table.json"
DEFAULT_KEYFRAME_TABLE = ROOT / "analysis" / "scene_cutscene_camera_keyframe_table.json"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_cutscene_camera_cmad_apply_verify.md"

ANGLE_S16_SCALE_BITS = 0x4622F983
DEGREE_SCALE_BITS = 0x42652EE1
POSITION_SCALE_BITS = 0x42200000


def as_dict(value: Any) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


def as_list(value: Any) -> list[Any]:
    return value if isinstance(value, list) else []


def f32_from_bits(bits: int) -> float:
    return struct.unpack("<f", struct.pack("<I", bits & 0xFFFFFFFF))[0]


def to_s16(value: float) -> int:
    raw = int(value) & 0xFFFF
    return raw - 0x10000 if raw & 0x8000 else raw


def sample_hermite(prev: dict[str, Any], next_: dict[str, Any], frame: float, start: float, end: float) -> float:
    frame_delta = frame - start
    frame_span = end - start
    t = frame_delta * (1.0 / frame_span)
    prev_value = f32_from_bits(int(prev["value_bits"]))
    next_value = f32_from_bits(int(next_["value_bits"]))
    prev_tangent = f32_from_bits(int(prev["tangent_out_bits"]))
    next_tangent = f32_from_bits(int(next_["tangent_in_bits"]))
    value_term = (prev_value - next_value) * ((2.0 * t) - 3.0)
    tangent_blend = ((t - 1.0) * prev_tangent) + (t * next_tangent)
    return prev_value + (value_term * t * t) + (frame_delta * (t - 1.0) * tangent_blend)


def sample_type2(curve: dict[str, Any], keyframes: list[dict[str, Any]], frame: float) -> float:
    if len(keyframes) == 0:
        raise ValueError("empty keyframe slice")
    if len(keyframes) == 1:
        return f32_from_bits(int(keyframes[0]["value_bits"]))

    point_index = 0
    while point_index < len(keyframes) and float(keyframes[point_index]["frame"]) <= frame:
        point_index += 1
    if point_index == 0:
        return 0.0
    if point_index == len(keyframes):
        return f32_from_bits(int(keyframes[-1]["value_bits"]))
    return sample_hermite(
        keyframes[point_index - 1],
        keyframes[point_index],
        frame,
        float(keyframes[point_index - 1]["frame"]),
        float(keyframes[point_index]["frame"]),
    )


def init_state(segment: dict[str, Any]) -> dict[str, Any]:
    return {
        "csParam8C": f32_from_bits(int(segment["base_csparam_8c_bits"])),
        "csParam90": f32_from_bits(int(segment["base_csparam_90_bits"])),
        "csParam94": f32_from_bits(int(segment["base_csparam_94_bits"])),
        "csParam80": f32_from_bits(int(segment["base_csparam_80_bits"])),
        "csParam84": f32_from_bits(int(segment["base_csparam_84_bits"])),
        "csParam88": f32_from_bits(int(segment["base_csparam_88_bits"])),
        "csParam1A2": to_s16(
            f32_from_bits(int(segment["base_csparam_1a2_source_bits"]))
            * f32_from_bits(ANGLE_S16_SCALE_BITS)
        ),
        "csParam144": f32_from_bits(int(segment["base_csparam_144_source_bits"]))
        * f32_from_bits(DEGREE_SCALE_BITS),
        "csParamD0": f32_from_bits(int(segment["base_csparam_d0_bits"])),
    }


def apply_curve(state: dict[str, Any], curve: dict[str, Any], value: float) -> None:
    output_field = int(curve["output_field_offset"])
    if output_field in {0x8C, 0x90, 0x94, 0x80, 0x84, 0x88}:
        scaled_value = value * f32_from_bits(POSITION_SCALE_BITS)
        field_name = {
            0x8C: "csParam8C",
            0x90: "csParam90",
            0x94: "csParam94",
            0x80: "csParam80",
            0x84: "csParam84",
            0x88: "csParam88",
        }[output_field]
        state[field_name] = scaled_value
    elif output_field == 0x1A2:
        state["csParam1A2"] = to_s16(value * f32_from_bits(ANGLE_S16_SCALE_BITS))
    elif output_field == 0x144:
        state["csParam144"] = value * f32_from_bits(DEGREE_SCALE_BITS)
    elif output_field == 0xD0:
        state["csParamD0"] = value
    else:
        raise ValueError(f"unsupported output field 0x{output_field:x}")


def ensure_state_finite(state: dict[str, Any], label: str) -> list[str]:
    errors: list[str] = []
    for key, value in state.items():
        if key == "csParam1A2":
            if not isinstance(value, int) or value < -32768 or value > 32767:
                errors.append(f"{label}: {key} outside s16 range: {value}")
        elif not math.isfinite(float(value)):
            errors.append(f"{label}: {key} is not finite: {value}")
    return errors


def write_markdown(path: Path, summary: dict[str, Any], intro_samples: list[dict[str, Any]]) -> None:
    lines = [
        "# Scene Cutscene Camera CMAD Apply Verification",
        "",
        "`FUN_0033CB90` reconstruction check over decoded OOT3D-native `caad/mads/cmad` camera data.",
        "",
        "## Summary",
        "",
        f"- Segments checked: {summary['segments_checked']}",
        f"- Intro segments checked: {summary['intro_segments_checked']}",
        f"- CMAD records applied: {summary['cmad_records_applied']}",
        f"- Curves applied: {summary['curves_applied']}",
        f"- Output fields: `{json.dumps(summary['output_field_counts'], sort_keys=True)}`",
        f"- Channel roles: `{json.dumps(summary['curve_role_counts'], sort_keys=True)}`",
        f"- Status: `{summary['status']}`",
        "",
        "## Native Constants",
        "",
        f"- Position scale `DAT_0033CE70`: `{f32_from_bits(POSITION_SCALE_BITS)}`",
        f"- Angle-to-s16 scale `DAT_0033CE68`: `{f32_from_bits(ANGLE_S16_SCALE_BITS)}`",
        f"- Degree scale `DAT_0033CE6C`: `{f32_from_bits(DEGREE_SCALE_BITS)}`",
        "",
        "## Intro Samples",
        "",
        "| segment | scene | setup | frame | cs80 | cs84 | cs88 | cs8c | cs90 | cs94 | cs1a2 | cs144 | csd0 |",
        "| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for sample in intro_samples[:16]:
        state = sample["state"]
        lines.append(
            f"| {sample['segment']} | `{sample['scene']}` | {sample['setup']} | {sample['frame']} | "
            f"{state['csParam80']:.6g} | {state['csParam84']:.6g} | {state['csParam88']:.6g} | "
            f"{state['csParam8C']:.6g} | {state['csParam90']:.6g} | {state['csParam94']:.6g} | "
            f"{state['csParam1A2']} | {state['csParam144']:.6g} | {state['csParamD0']:.6g} |"
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

    errors: list[str] = []
    output_field_counts: dict[str, int] = {}
    curve_role_counts: dict[str, int] = {}
    cmad_records_applied = 0
    curves_applied = 0
    intro_segments_checked = 0
    intro_samples: list[dict[str, Any]] = []

    for segment in segments:
        segment_index = int(segment["camera_blob_segment_source_index"])
        frame = int(segment["start_frame"]) + 1
        if frame >= int(segment["end_frame"]):
            frame = int(segment["start_frame"])
        state = init_state(segment)
        errors.extend(ensure_state_finite(state, f"segment {segment_index} base"))

        for cmad in cmads_by_segment.get(segment_index, []):
            cmad_records_applied += 1
            start = int(cmad["curve_ref_start"])
            count = int(cmad["curve_ref_count"])
            if start + count > len(curves):
                errors.append(f"cmad {cmad['cmad_record_source_index']}: curve slice outside table")
                continue
            for curve in curves[start : start + count]:
                curve_index = int(curve["camera_curve_source_index"])
                if int(curve["cmad_record_source_index"]) != int(cmad["cmad_record_source_index"]):
                    errors.append(f"cmad {cmad['cmad_record_source_index']}: curve owner mismatch")
                    continue
                if curve_index >= len(refs):
                    errors.append(f"curve {curve_index}: missing keyframe ref")
                    continue
                ref = refs[curve_index]
                keyframe_start = int(ref["keyframe_ref_start"])
                keyframe_count = int(ref["keyframe_ref_count"])
                curve_keyframes = keyframes[keyframe_start : keyframe_start + keyframe_count]
                if keyframe_count != int(curve["point_count"]):
                    errors.append(f"curve {curve_index}: keyframe count mismatch")
                    continue
                value = sample_type2(curve, curve_keyframes, float(frame))
                if not math.isfinite(value):
                    errors.append(f"curve {curve_index}: sampled value is not finite")
                    continue
                apply_curve(state, curve, value)
                curves_applied += 1
                output_field_counts[curve["output_field_offset_hex"]] = (
                    output_field_counts.get(curve["output_field_offset_hex"], 0) + 1
                )
                curve_role_counts[curve["curve_role"]] = curve_role_counts.get(curve["curve_role"], 0) + 1

        errors.extend(ensure_state_finite(state, f"segment {segment_index} applied"))
        if segment.get("scene_path") in {"link_info.zsi", "spot04_info.zsi"}:
            intro_segments_checked += 1
            intro_samples.append(
                {
                    "segment": segment_index,
                    "scene": segment.get("scene_path"),
                    "setup": segment.get("setup_index"),
                    "frame": frame,
                    "state": dict(state),
                }
            )

    summary = {
        "segments_checked": len(segments),
        "intro_segments_checked": intro_segments_checked,
        "cmad_records_applied": cmad_records_applied,
        "curves_applied": curves_applied,
        "output_field_counts": dict(sorted(output_field_counts.items())),
        "curve_role_counts": dict(sorted(curve_role_counts.items())),
        "status": "pass" if not errors else "fail",
    }
    write_markdown(DEFAULT_OUT_MD, summary, intro_samples)
    if errors:
        for error in errors[:50]:
            print(f"error: {error}", file=sys.stderr)
        if len(errors) > 50:
            print(f"error: {len(errors) - 50} additional errors omitted", file=sys.stderr)
        return 1
    print(
        "verified scene cutscene camera cmad apply: "
        f"{len(segments)} segments, {cmad_records_applied} cmads, {curves_applied} curves"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
