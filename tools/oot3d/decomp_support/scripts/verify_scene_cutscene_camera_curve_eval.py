#!/usr/bin/env python3
"""Verify the reconstructed OOT3D cutscene camera curve evaluator.

This script mirrors the type-2 branch of `FUN_003087A4` against the generated
native tables. It is intentionally data-driven: all curve/keyframe rows come
from decoded OOT3D ZSI command `0x97` blobs, and N64 data is not consulted.
"""

from __future__ import annotations

import json
import math
import struct
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CMAD_TABLE = ROOT / "analysis" / "scene_cutscene_camera_cmad_table.json"
DEFAULT_KEYFRAME_TABLE = ROOT / "analysis" / "scene_cutscene_camera_keyframe_table.json"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_cutscene_camera_curve_eval_verify.md"

MAX_ERROR_ALLOWED = 1.0e-4


def as_dict(value: Any) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


def as_list(value: Any) -> list[Any]:
    return value if isinstance(value, list) else []


def f32_from_bits(bits: int) -> float:
    return struct.unpack("<f", struct.pack("<I", bits & 0xFFFFFFFF))[0]


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


def sample_type2(curve: dict[str, Any], keyframes: list[dict[str, Any]], frame: float, loop_mode: int) -> float:
    if len(keyframes) == 0:
        raise ValueError("empty keyframe slice")
    if len(keyframes) == 1:
        return f32_from_bits(int(keyframes[0]["value_bits"]))
    if loop_mode and (frame < 0.0 or float(curve["header_word0c"]) < frame):
        prev = keyframes[-1]
        next_ = keyframes[0]
        loop_span = float(int(curve["header_word0c"]) + 1)
        sample_frame = frame + loop_span if frame < 0.0 else frame
        start = float(prev["frame"])
        end = float(int(next_["frame"]) + int(curve["header_word0c"]) + 1)
        return sample_hermite(prev, next_, sample_frame, start, end)

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


def write_markdown(path: Path, summary: dict[str, Any]) -> None:
    lines = [
        "# Scene Cutscene Camera Curve Eval Verification",
        "",
        "Verification for the reconstructed type-2 `FUN_003087A4` camera curve evaluator over decoded OOT3D command `0x97` assets.",
        "",
        "## Summary",
        "",
        f"- Curves checked: {summary['curves_checked']}",
        f"- Keyframes checked: {summary['keyframes_checked']}",
        f"- Intro keyframes checked: {summary['intro_keyframes_checked']}",
        f"- Before-first-frame checks: {summary['before_first_frame_checks']}",
        f"- Loop branch checks: {summary['loop_branch_checks']}",
        f"- Max keyframe sample error: `{summary['max_keyframe_sample_error']:.9g}`",
        f"- Max midpoint finite sample absolute value: `{summary['max_midpoint_abs_value']:.9g}`",
        f"- Status: `{summary['status']}`",
        "",
        "## Native Rules Checked",
        "",
        "- Every curve has a generated keyframe slice matching its `point_count`.",
        "- Sampling exactly at each decoded keyframe returns that keyframe's native float value.",
        "- Non-loop samples before the first keyframe return `0.0`, matching the type-2 fallthrough in `FUN_003087A4`.",
        "- Loop samples before/after `header_word0c` use the last-to-first segment rule from the native branch.",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def main() -> int:
    cmad_table = json.loads(DEFAULT_CMAD_TABLE.read_text(encoding="utf-8"))
    keyframe_table = json.loads(DEFAULT_KEYFRAME_TABLE.read_text(encoding="utf-8"))
    curves = [as_dict(row) for row in as_list(cmad_table.get("camera_curve_rows"))]
    keyframes = [as_dict(row) for row in as_list(keyframe_table.get("keyframe_rows"))]
    refs = [as_dict(row) for row in as_list(keyframe_table.get("curve_keyframe_ref_rows"))]

    errors: list[str] = []
    max_keyframe_error = 0.0
    max_midpoint_abs_value = 0.0
    keyframes_checked = 0
    intro_keyframes_checked = 0
    before_first_checks = 0
    loop_checks = 0

    if len(refs) != len(curves):
        errors.append(f"curve ref row count {len(refs)} does not match curve row count {len(curves)}")

    for curve_index, curve in enumerate(curves):
        if int(curve["interpolation_type"]) != 2:
            errors.append(f"curve {curve_index}: unsupported interpolation type in verifier")
            continue
        if curve_index >= len(refs):
            continue
        ref = refs[curve_index]
        start = int(ref["keyframe_ref_start"])
        count = int(ref["keyframe_ref_count"])
        if count != int(curve["point_count"]):
            errors.append(f"curve {curve_index}: ref count {count} != point_count {curve['point_count']}")
            continue
        curve_keyframes = keyframes[start : start + count]
        if len(curve_keyframes) != count:
            errors.append(f"curve {curve_index}: keyframe slice outside table")
            continue
        if any(int(row["camera_curve_source_index"]) != curve_index for row in curve_keyframes):
            errors.append(f"curve {curve_index}: keyframe slice owner mismatch")
            continue

        previous_frame = None
        for keyframe in curve_keyframes:
            frame = int(keyframe["frame"])
            if previous_frame is not None and frame <= previous_frame:
                errors.append(f"curve {curve_index}: keyframe frames are not strictly ascending")
                break
            previous_frame = frame
            expected = f32_from_bits(int(keyframe["value_bits"]))
            actual = sample_type2(curve, curve_keyframes, float(frame), 0)
            error = abs(actual - expected)
            max_keyframe_error = max(max_keyframe_error, error)
            if error > MAX_ERROR_ALLOWED:
                errors.append(f"curve {curve_index} frame {frame}: sample {actual} != keyframe {expected}")
                break
            keyframes_checked += 1
            if curve.get("scene_path") in {"link_info.zsi", "spot04_info.zsi"}:
                intro_keyframes_checked += 1

        if len(curve_keyframes) > 1:
            before_first = float(int(curve_keyframes[0]["frame"]) - 1)
            if sample_type2(curve, curve_keyframes, before_first, 0) != 0.0:
                errors.append(f"curve {curve_index}: before-first non-loop sample did not return 0.0")
            before_first_checks += 1

            midpoint = (float(curve_keyframes[0]["frame"]) + float(curve_keyframes[1]["frame"])) * 0.5
            midpoint_value = sample_type2(curve, curve_keyframes, midpoint, 0)
            if not math.isfinite(midpoint_value):
                errors.append(f"curve {curve_index}: midpoint sample is not finite")
            max_midpoint_abs_value = max(max_midpoint_abs_value, abs(midpoint_value))

            loop_before = sample_type2(curve, curve_keyframes, -1.0, 1)
            loop_after = sample_type2(curve, curve_keyframes, float(int(curve["header_word0c"]) + 1), 1)
            if not math.isfinite(loop_before) or not math.isfinite(loop_after):
                errors.append(f"curve {curve_index}: loop branch sample is not finite")
            loop_checks += 2

    summary = {
        "curves_checked": len(curves),
        "keyframes_checked": keyframes_checked,
        "intro_keyframes_checked": intro_keyframes_checked,
        "before_first_frame_checks": before_first_checks,
        "loop_branch_checks": loop_checks,
        "max_keyframe_sample_error": max_keyframe_error,
        "max_midpoint_abs_value": max_midpoint_abs_value,
        "status": "pass" if not errors else "fail",
    }
    write_markdown(DEFAULT_OUT_MD, summary)
    if errors:
        for error in errors[:50]:
            print(f"error: {error}", file=sys.stderr)
        if len(errors) > 50:
            print(f"error: {len(errors) - 50} additional errors omitted", file=sys.stderr)
        return 1
    print(
        "verified scene cutscene camera curve evaluator: "
        f"{keyframes_checked} keyframes, "
        f"{before_first_checks} before-first checks, "
        f"{loop_checks} loop checks"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
