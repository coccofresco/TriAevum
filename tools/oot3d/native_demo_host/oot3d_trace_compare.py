from __future__ import annotations

import argparse
import json
import math
from pathlib import Path


COMPARE_FORMAT = "oot3d_native_demo_trace_compare_v1"
SUPPORTED_TRACE_FORMATS = {
    "oot3d_native_demo_host_trace_v1",
    "oot3d_reference_trace_v1",
}


def read_json(path: Path) -> dict[str, object]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, value: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2), encoding="utf-8", newline="\n")


def frame_map(trace: dict[str, object]) -> dict[int, dict[str, object]]:
    frames = trace.get("frames")
    if not isinstance(frames, list):
        frames = trace.get("frames_sample")
    if not isinstance(frames, list):
        return {}
    out: dict[int, dict[str, object]] = {}
    for item in frames:
        if isinstance(item, dict) and isinstance(item.get("frame"), int):
            out[int(item["frame"])] = item
    return out


def vec3(frame: dict[str, object], key: str) -> tuple[float, float, float] | None:
    value = frame.get(key)
    if not isinstance(value, list) or len(value) != 3:
        return None
    try:
        return (float(value[0]), float(value[1]), float(value[2]))
    except (TypeError, ValueError):
        return None


def yaw(frame: dict[str, object]) -> float | None:
    rotation = frame.get("rotation")
    if not isinstance(rotation, dict):
        return None
    try:
        return float(rotation.get("yaw_degrees"))
    except (TypeError, ValueError):
        return None


def distance(a: tuple[float, float, float], b: tuple[float, float, float]) -> float:
    return math.sqrt((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2)


def angular_delta(a: float, b: float) -> float:
    delta = abs((a - b + 180.0) % 360.0 - 180.0)
    return delta


def compare_traces(
    candidate_path: Path,
    reference_path: Path | None,
    output_path: Path,
    *,
    position_epsilon: float,
    velocity_epsilon: float,
    yaw_epsilon: float,
    require_reference: bool,
) -> dict[str, object]:
    issues: list[str] = []
    candidate = read_json(candidate_path)
    if candidate.get("format") not in SUPPORTED_TRACE_FORMATS:
        issues.append("candidate_trace_format_unsupported")
    candidate_frames = frame_map(candidate)
    if not candidate_frames:
        issues.append("candidate_trace_frames_missing")

    if reference_path is None or not reference_path.is_file():
        result = {
            "format": COMPARE_FORMAT,
            "status": "missing_reference" if not require_reference else "invalid",
            "gate_available": True,
            "candidate_trace": str(candidate_path),
            "reference_trace": str(reference_path) if reference_path is not None else "",
            "issue_count": len(issues) + (1 if require_reference else 0),
            "issues": issues + (["reference_trace_missing"] if require_reference else []),
            "thresholds": {
                "position_epsilon": position_epsilon,
                "velocity_epsilon": velocity_epsilon,
                "yaw_epsilon": yaw_epsilon,
            },
            "candidate_frame_count": len(candidate_frames),
            "reference_frame_count": 0,
            "compared_frame_count": 0,
            "missing_reference": True,
            "parity_passed": False,
        }
        write_json(output_path, result)
        return result

    reference = read_json(reference_path)
    if reference.get("format") not in SUPPORTED_TRACE_FORMATS:
        issues.append("reference_trace_format_unsupported")
    reference_frames = frame_map(reference)
    if not reference_frames:
        issues.append("reference_trace_frames_missing")

    common_frames = sorted(set(candidate_frames).intersection(reference_frames))
    if not common_frames:
        issues.append("no_common_frames")

    max_position_delta = 0.0
    max_velocity_delta = 0.0
    max_yaw_delta = 0.0
    position_failures = 0
    velocity_failures = 0
    yaw_failures = 0
    floor_hit_mismatches = 0

    for frame_index in common_frames:
        c = candidate_frames[frame_index]
        r = reference_frames[frame_index]
        c_pos = vec3(c, "position")
        r_pos = vec3(r, "position")
        if c_pos is not None and r_pos is not None:
            delta = distance(c_pos, r_pos)
            max_position_delta = max(max_position_delta, delta)
            if delta > position_epsilon:
                position_failures += 1
        c_vel = vec3(c, "velocity")
        r_vel = vec3(r, "velocity")
        if c_vel is not None and r_vel is not None:
            delta = distance(c_vel, r_vel)
            max_velocity_delta = max(max_velocity_delta, delta)
            if delta > velocity_epsilon:
                velocity_failures += 1
        c_yaw = yaw(c)
        r_yaw = yaw(r)
        if c_yaw is not None and r_yaw is not None:
            delta = angular_delta(c_yaw, r_yaw)
            max_yaw_delta = max(max_yaw_delta, delta)
            if delta > yaw_epsilon:
                yaw_failures += 1
        c_floor = c.get("floor")
        r_floor = r.get("floor")
        if isinstance(c_floor, dict) and isinstance(r_floor, dict):
            if bool(c_floor.get("hit")) != bool(r_floor.get("hit")):
                floor_hit_mismatches += 1

    if position_failures:
        issues.append("position_delta_exceeded")
    if velocity_failures:
        issues.append("velocity_delta_exceeded")
    if yaw_failures:
        issues.append("yaw_delta_exceeded")
    if floor_hit_mismatches:
        issues.append("floor_hit_mismatch")

    parity_passed = not issues
    result = {
        "format": COMPARE_FORMAT,
        "status": "passed" if parity_passed else "failed",
        "gate_available": True,
        "candidate_trace": str(candidate_path),
        "reference_trace": str(reference_path),
        "issue_count": len(issues),
        "issues": issues,
        "thresholds": {
            "position_epsilon": position_epsilon,
            "velocity_epsilon": velocity_epsilon,
            "yaw_epsilon": yaw_epsilon,
        },
        "candidate_frame_count": len(candidate_frames),
        "reference_frame_count": len(reference_frames),
        "compared_frame_count": len(common_frames),
        "missing_reference": False,
        "parity_passed": parity_passed,
        "metrics": {
            "max_position_delta": max_position_delta,
            "max_velocity_delta": max_velocity_delta,
            "max_yaw_delta": max_yaw_delta,
            "position_failure_count": position_failures,
            "velocity_failure_count": velocity_failures,
            "yaw_failure_count": yaw_failures,
            "floor_hit_mismatch_count": floor_hit_mismatches,
        },
    }
    write_json(output_path, result)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare an OOT3D native host trace against a reference trace.")
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--reference", type=Path, default=None)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--position-epsilon", type=float, default=0.5)
    parser.add_argument("--velocity-epsilon", type=float, default=0.5)
    parser.add_argument("--yaw-epsilon", type=float, default=1.0)
    parser.add_argument("--require-reference", action="store_true")
    args = parser.parse_args()
    result = compare_traces(
        args.candidate,
        args.reference,
        args.output,
        position_epsilon=args.position_epsilon,
        velocity_epsilon=args.velocity_epsilon,
        yaw_epsilon=args.yaw_epsilon,
        require_reference=args.require_reference,
    )
    print(json.dumps(result, indent=2))
    if result["status"] in {"passed", "missing_reference"} and not args.require_reference:
        return 0
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
