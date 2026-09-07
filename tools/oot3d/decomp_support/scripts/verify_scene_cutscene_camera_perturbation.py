#!/usr/bin/env python3
"""Verify OOT3D camera quake perturbation globals and output layout."""

from __future__ import annotations

import csv
import math
import struct
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CODE_BIN = Path(r"E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin")
DEFAULT_EXPORT_DIR = ROOT / "analysis" / "camera_view_perturbation_ghidra_export"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_cutscene_camera_perturbation_verify.md"

CODE_BIN_VA_BASE = 0x00100000

FUN_VEC3F_DIST = 0x00338A90
FUN_CAMERA_QUAKE_CALC = 0x004787E8

DAT_ZERO_FLOAT = 0x00478A80
DAT_GLOBAL_STATE_PTR = 0x00478A84
DAT_SPEED_SCALE = 0x00478A88
DAT_ROT_ZOOM_SCALE = 0x00478A8C
DAT_REQUEST_ARRAY_PTR = 0x00478A90
DAT_CALLBACK_TABLE_PTR = 0x00478A94

EXPECTED_CALLBACKS = [
    0x00000000,
    0x0015F2CC,
    0x00144EE8,
    0x001344F0,
    0x0015F25C,
    0x00111CB0,
    0x00150CFC,
]


def f32_from_bits(bits: int) -> float:
    return struct.unpack("<f", struct.pack("<I", bits & 0xFFFFFFFF))[0]


def va_to_offset(va: int) -> int:
    return va - CODE_BIN_VA_BASE


def read_u32(data: bytes, va: int) -> int | None:
    offset = va_to_offset(va)
    if offset < 0 or offset + 4 > len(data):
        return None
    return struct.unpack_from("<I", data, offset)[0]


def read_u16(data: bytes, va: int) -> int | None:
    offset = va_to_offset(va)
    if offset < 0 or offset + 2 > len(data):
        return None
    return struct.unpack_from("<H", data, offset)[0]


def load_exported_entries(path: Path) -> dict[int, str]:
    csv_path = path / "functions_selected.csv"
    if not csv_path.exists():
        return {}
    with csv_path.open(newline="", encoding="utf-8") as file:
        return {int(row["entry"], 16): row["name"] for row in csv.DictReader(file)}


def write_markdown(path: Path, summary: dict[str, object], errors: list[str]) -> None:
    callbacks = summary["callbacks"]
    assert isinstance(callbacks, list)
    lines = [
        "# Scene Cutscene Camera Perturbation Verification",
        "",
        "Verification for `FUN_004787E8`, the OOT3D camera quake/perturbation aggregation path called by `Camera_Update`.",
        "",
        "## Summary",
        "",
        f"- `FUN_004787E8` exported: {summary['has_quake_calc']}",
        f"- `FUN_00338A90` exported as Vec3f distance helper: {summary['has_vec3f_dist']}",
        f"- Zero literal bits: `{summary['zero_bits']}`",
        f"- Speed scale bits/float: `{summary['speed_scale_bits']}` / `{summary['speed_scale_float']}`",
        f"- Rotation and zoom scale bits/float: `{summary['rot_zoom_scale_bits']}` / `{summary['rot_zoom_scale_float']}`",
        f"- Global state pointer: `{summary['global_state_ptr']}`",
        f"- Static active request count: `{summary['static_active_count']}`",
        f"- Request array pointer: `{summary['request_array_ptr']}` ({summary['request_array_storage']})",
        f"- Callback table pointer: `{summary['callback_table_ptr']}`",
        f"- Status: `{'pass' if not errors else 'fail'}`",
        "",
        "## Native Output Layout",
        "",
        "| offset | field | evidence |",
        "| ---: | --- | --- |",
        "| `+0x00` | `Vec3f atOffset` | zeroed by `FUN_004787E8`; `Camera_Update` adds it to `camera+0x80/84/88` (`at`) |",
        "| `+0x0C` | `Vec3f eyeOffset` | zeroed by `FUN_004787E8`; `Camera_Update` adds it to `camera+0x8C/90/94` (`eye`) |",
        "| `+0x18` | `s16 pitchOffset` | added to the computed pitch before `Camera_CalcUpFromPitchYawRoll` |",
        "| `+0x1A` | `s16 yawOffset` | added to the computed yaw before `Camera_CalcUpFromPitchYawRoll` |",
        "| `+0x1C` | `s16 fovOffsetBinang` | multiplied by `DAT_002D8C3C` in `Camera_Update` for FOV degrees |",
        "| `+0x20` | `f32 maxMagnitude` | max of offset/angle/zoom magnitudes scaled by request speed |",
        "",
        "## Callback Table",
        "",
        "| index | function |",
        "| ---: | ---: |",
    ]
    for index, callback in enumerate(callbacks):
        lines.append(f"| {index} | `0x{callback:08X}` |")
    lines.extend(
        [
            "",
            "## Correlation Notes",
            "",
            "- OOT3D keeps the same high-level quake aggregation structure visible in the N64 source: four request slots, active-count short, callback table, per-camera filtering, and max-absolute aggregation.",
            "- The request array pointer resolves outside `code.bin`, so live request contents are runtime/BSS state; static verification is limited to the pointer, active count initializer, and callback table.",
            "- The six callback bodies are covered by `verify_camera_quake_callbacks.py`; those callbacks derive the actual shake vectors from request parameters before this aggregation layer combines them.",
            "",
        ]
    )
    if errors:
        lines.extend(["## Errors", ""])
        lines.extend(f"- {error}" for error in errors)
        lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def main() -> int:
    errors: list[str] = []
    if not DEFAULT_CODE_BIN.exists():
        errors.append(f"missing code.bin: {DEFAULT_CODE_BIN}")
        write_markdown(
            DEFAULT_OUT_MD,
            {
                "has_quake_calc": False,
                "has_vec3f_dist": False,
                "zero_bits": "unavailable",
                "speed_scale_bits": "unavailable",
                "speed_scale_float": "unavailable",
                "rot_zoom_scale_bits": "unavailable",
                "rot_zoom_scale_float": "unavailable",
                "global_state_ptr": "unavailable",
                "static_active_count": "unavailable",
                "request_array_ptr": "unavailable",
                "request_array_storage": "unavailable",
                "callback_table_ptr": "unavailable",
                "callbacks": [],
            },
            errors,
        )
        return 1

    data = DEFAULT_CODE_BIN.read_bytes()
    exported_entries = load_exported_entries(DEFAULT_EXPORT_DIR)
    has_quake_calc = FUN_CAMERA_QUAKE_CALC in exported_entries
    has_vec3f_dist = FUN_VEC3F_DIST in exported_entries
    if not has_quake_calc:
        errors.append("FUN_004787E8 is missing from the selected Ghidra export")
    if not has_vec3f_dist:
        errors.append("FUN_00338A90 Vec3f distance helper is missing from the selected Ghidra export")

    zero_bits = read_u32(data, DAT_ZERO_FLOAT)
    speed_scale_bits = read_u32(data, DAT_SPEED_SCALE)
    rot_zoom_scale_bits = read_u32(data, DAT_ROT_ZOOM_SCALE)
    global_state_ptr = read_u32(data, DAT_GLOBAL_STATE_PTR)
    request_array_ptr = read_u32(data, DAT_REQUEST_ARRAY_PTR)
    callback_table_ptr = read_u32(data, DAT_CALLBACK_TABLE_PTR)

    if zero_bits != 0x00000000:
        errors.append(f"DAT_00478A80 expected 0x00000000, got {zero_bits!r}")
    if speed_scale_bits != 0x38000000:
        errors.append(f"DAT_00478A88 expected 0x38000000, got {speed_scale_bits!r}")
    if rot_zoom_scale_bits != 0x3BA3D70A:
        errors.append(f"DAT_00478A8C expected 0x3BA3D70A, got {rot_zoom_scale_bits!r}")
    if global_state_ptr != 0x0053CAE4:
        errors.append(f"DAT_00478A84 expected 0x0053CAE4, got {global_state_ptr!r}")
    if request_array_ptr != 0x005A543C:
        errors.append(f"DAT_00478A90 expected 0x005A543C, got {request_array_ptr!r}")
    if callback_table_ptr != 0x0053CAE8:
        errors.append(f"DAT_00478A94 expected 0x0053CAE8, got {callback_table_ptr!r}")

    static_active_count = None
    if global_state_ptr is not None:
        static_active_count = read_u16(data, global_state_ptr + 2)
        if static_active_count != 0:
            errors.append(f"static active request count expected 0, got {static_active_count!r}")

    callbacks: list[int] = []
    if callback_table_ptr is not None:
        for index in range(len(EXPECTED_CALLBACKS)):
            value = read_u32(data, callback_table_ptr + index * 4)
            callbacks.append(0 if value is None else value)
        if callbacks != EXPECTED_CALLBACKS:
            errors.append(
                "callback table mismatch: "
                + ", ".join(f"0x{value:08X}" for value in callbacks)
            )

    request_array_storage = "outside code.bin; runtime/BSS"
    if request_array_ptr is not None and read_u32(data, request_array_ptr) is not None:
        request_array_storage = "inside code.bin"

    if speed_scale_bits is None:
        speed_scale_float = "unavailable"
    else:
        speed_scale_float = f32_from_bits(speed_scale_bits)
        if not math.isclose(speed_scale_float, 1.0 / 32768.0, rel_tol=0.0, abs_tol=1e-12):
            errors.append(f"speed scale float mismatch: {speed_scale_float}")

    if rot_zoom_scale_bits is None:
        rot_zoom_scale_float = "unavailable"
    else:
        rot_zoom_scale_float = f32_from_bits(rot_zoom_scale_bits)
        if not math.isclose(rot_zoom_scale_float, 0.005, rel_tol=0.0, abs_tol=1e-9):
            errors.append(f"rotation/zoom scale float mismatch: {rot_zoom_scale_float}")

    summary: dict[str, object] = {
        "has_quake_calc": has_quake_calc,
        "has_vec3f_dist": has_vec3f_dist,
        "zero_bits": "unavailable" if zero_bits is None else f"0x{zero_bits:08X}",
        "speed_scale_bits": "unavailable" if speed_scale_bits is None else f"0x{speed_scale_bits:08X}",
        "speed_scale_float": speed_scale_float,
        "rot_zoom_scale_bits": "unavailable" if rot_zoom_scale_bits is None else f"0x{rot_zoom_scale_bits:08X}",
        "rot_zoom_scale_float": rot_zoom_scale_float,
        "global_state_ptr": "unavailable" if global_state_ptr is None else f"0x{global_state_ptr:08X}",
        "static_active_count": "unavailable" if static_active_count is None else static_active_count,
        "request_array_ptr": "unavailable" if request_array_ptr is None else f"0x{request_array_ptr:08X}",
        "request_array_storage": request_array_storage,
        "callback_table_ptr": "unavailable" if callback_table_ptr is None else f"0x{callback_table_ptr:08X}",
        "callbacks": callbacks,
    }
    write_markdown(DEFAULT_OUT_MD, summary, errors)
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1

    print(
        "verified scene cutscene camera perturbation: "
        f"{len(callbacks)} callbacks, request array {summary['request_array_storage']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
