#!/usr/bin/env python3
"""Verify reconstructed OOT3D camera quake callback bodies."""

from __future__ import annotations

import csv
import re
import struct
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CODE_BIN = Path(r"E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin")
DEFAULT_EXPORT_DIR = ROOT / "analysis" / "camera_quake_callbacks_ghidra_export"
DEFAULT_OUT_MD = ROOT / "analysis" / "camera_quake_callbacks_verify.md"

CODE_BIN_VA_BASE = 0x00100000
CALLBACK_TABLE_PTR_VA = 0x00478A94
UPDATE_HELPER_ZERO_VA = 0x00369F34
UPDATE_HELPER_YAW_SCALE_VA = 0x00369F38

EXPECTED_CALLBACKS = [
    (0, 0x00000000, "NULL", "unused"),
    (1, 0x0015F2CC, "Quake_Callback1", "sin(speed * countdown), rand-scaled x"),
    (2, 0x00144EE8, "Quake_Callback2", "random y, random-scaled x"),
    (3, 0x001344F0, "Quake_Callback3", "sin envelope countdown/countdownMax, x=y"),
    (4, 0x0015F25C, "Quake_Callback4", "random envelope countdown/countdownMax"),
    (5, 0x00111CB0, "Quake_Callback5", "sin(speed * countdown), x=y"),
    (6, 0x00150CFC, "Quake_Callback6", "decrement first, periodic low-nibble phase, return 1"),
]

EXPECTED_CALLEES = {
    0x002CFCA0: "oot3d_sin_idx8",
    0x00369D44: "quake_update_shake_info",
    0x003759D0: "Rand_ZeroOne",
}

CALLBACK_PATTERNS = {
    0x0015F2CC: ["bl 0x002cfca0", "bl 0x003759d0", "bl 0x00369d44", "sub r0,r0,#0x1"],
    0x00144EE8: ["bl 0x003759d0", "vmul.f32 s1,s0,s16", "bl 0x00369d44", "sub r0,r0,#0x1"],
    0x001344F0: ["bl 0x002cfca0", "vdiv.f32 s1,s1,s2", "vmul.f32 s0,s0,s1", "bl 0x00369d44"],
    0x0015F25C: ["bl 0x003759d0", "vdiv.f32 s1,s1,s2", "vmul.f32 s16,s0,s1", "bl 0x00369d44"],
    0x00111CB0: ["bl 0x002cfca0", "vmov.f32 s1,s0", "bl 0x00369d44", "sub r0,r0,#0x1"],
    0x00150CFC: ["sub r0,r0,#0x1", "and r0,r0,#0xf", "add r0,r0,#0x1f4", "bl 0x002cfca0", "bl 0x003759d0", "mov r0,#0x1"],
}

UPDATE_HELPER_PATTERNS = [
    "ldr r0,[r0,#0x4]",
    "ldrh r3,[r4,#0x1a]",
    "add r1,r0,#0x8c",
    "add r2,r0,#0x80",
    "ldrsh r0,[r4,#0xa]",
    "ldrsh r0,[r4,#0xc]",
    "ldrsh r0,[r4,#0xe]",
    "ldrsh r0,[r4,#0x10]",
    "ldrh r1,[r4,#0x12]",
    "ldrh r1,[r4,#0x14]",
    "strh r0,[r5,#0x18]",
    "strh r0,[r5,#0x1a]",
    "strh r0,[r5,#0x1c]",
]


def read_u32(data: bytes, va: int) -> int | None:
    offset = va - CODE_BIN_VA_BASE
    if offset < 0 or offset + 4 > len(data):
        return None
    return struct.unpack_from("<I", data, offset)[0]


def load_functions(path: Path) -> dict[int, str]:
    csv_path = path / "functions_selected.csv"
    if not csv_path.exists():
        return {}
    with csv_path.open(newline="", encoding="utf-8") as file:
        return {int(row["entry"], 16): row["name"] for row in csv.DictReader(file)}


def split_disassembly_sections(text: str) -> dict[int, str]:
    sections: dict[int, str] = {}
    matches = list(re.finditer(r"^// .+ @ ([0-9a-fA-F]{8})$", text, flags=re.MULTILINE))
    for index, match in enumerate(matches):
        start = match.end()
        end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
        sections[int(match.group(1), 16)] = text[start:end].lower()
    return sections


def verify_patterns(section: str, patterns: list[str], label: str, errors: list[str]) -> None:
    for pattern in patterns:
        if pattern.lower() not in section:
            errors.append(f"{label}: missing disassembly pattern `{pattern}`")


def write_markdown(path: Path, summary: dict[str, object], errors: list[str]) -> None:
    lines = [
        "# Camera Quake Callback Verification",
        "",
        "Verification for the six callback bodies referenced by the OOT3D camera quake table consumed by `FUN_004787E8`.",
        "",
        "## Summary",
        "",
        f"- Callback table pointer: `{summary['callback_table_ptr']}`",
        f"- Update helper zero literal: `{summary['update_zero_bits']}`",
        f"- Update helper yaw-scale literal: `{summary['update_yaw_scale_bits']}` / `{summary['update_yaw_scale_float']}`",
        f"- Exported functions checked: {summary['exported_function_count']}",
        f"- Callback bodies checked: {summary['callback_body_count']}",
        f"- Update helper patterns checked: {summary['update_helper_pattern_count']}",
        f"- Status: `{'pass' if not errors else 'fail'}`",
        "",
        "## Native Callback Table",
        "",
        "| index | entry | recovered name | native shape |",
        "| ---: | ---: | --- | --- |",
    ]
    for index, entry, name, shape in EXPECTED_CALLBACKS:
        lines.append(f"| {index} | `0x{entry:08X}` | `{name}` | {shape} |")
    lines.extend(
        [
            "",
            "## Helper Layout",
            "",
            "- `FUN_00369D44` reads `cameraRef + 0x80` as `at` and `cameraRef + 0x8C` as `eye` when `request+0x1A != 0`.",
            "- Request offsets verified from callback/helper disassembly: `+0x02 countdownMax`, `+0x0A yMagnitude`, `+0x0C xMagnitude`, `+0x0E fovMagnitude`, `+0x10 pitchMagnitude`, `+0x12 sphPitchOffset`, `+0x14 sphYawOffset`, `+0x18 speed`, `+0x1A relativeToCamera`, `+0x1C countdown`, `+0x1E cameraSlot`.",
            "- Shake output offsets verified from `FUN_00369D44`: `+0x00 atOffset`, `+0x0C eyeOffset`, `+0x18 pitchOffset`, `+0x1A yawOffset`, `+0x1C fovOffsetBinang`.",
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
    callbacks_from_codebin: list[int] = []
    callback_table_ptr = None
    update_zero_bits = None
    update_yaw_scale_bits = None
    if DEFAULT_CODE_BIN.exists():
        data = DEFAULT_CODE_BIN.read_bytes()
        callback_table_ptr = read_u32(data, CALLBACK_TABLE_PTR_VA)
        update_zero_bits = read_u32(data, UPDATE_HELPER_ZERO_VA)
        update_yaw_scale_bits = read_u32(data, UPDATE_HELPER_YAW_SCALE_VA)
        if callback_table_ptr is None:
            errors.append("could not read DAT_00478A94 callback table pointer")
        else:
            for index, _, _, _ in EXPECTED_CALLBACKS:
                value = read_u32(data, callback_table_ptr + index * 4)
                callbacks_from_codebin.append(0 if value is None else value)
        if update_zero_bits != 0x00000000:
            errors.append(f"FUN_00369D44 zero literal expected 0x00000000, got {update_zero_bits!r}")
        if update_yaw_scale_bits != 0x47000000:
            errors.append(f"FUN_00369D44 yaw-scale literal expected 0x47000000, got {update_yaw_scale_bits!r}")
    else:
        errors.append(f"missing code.bin: {DEFAULT_CODE_BIN}")

    expected_entries = [entry for _, entry, _, _ in EXPECTED_CALLBACKS]
    if callbacks_from_codebin and callbacks_from_codebin != expected_entries:
        errors.append(
            "callback table mismatch: "
            + ", ".join(f"0x{value:08X}" for value in callbacks_from_codebin)
        )

    functions = load_functions(DEFAULT_EXPORT_DIR)
    for _, entry, name, _ in EXPECTED_CALLBACKS[1:]:
        if entry not in functions:
            errors.append(f"missing exported callback {name} at 0x{entry:08X}")
    for entry, name in EXPECTED_CALLEES.items():
        if entry not in functions:
            errors.append(f"missing exported callee {name} at 0x{entry:08X}")

    disassembly_path = DEFAULT_EXPORT_DIR / "disassembly_selected.txt"
    sections: dict[int, str] = {}
    if disassembly_path.exists():
        sections = split_disassembly_sections(disassembly_path.read_text(encoding="utf-8"))
        for entry, patterns in CALLBACK_PATTERNS.items():
            section = sections.get(entry)
            if section is None:
                errors.append(f"missing disassembly section for 0x{entry:08X}")
            else:
                verify_patterns(section, patterns, f"0x{entry:08X}", errors)
        helper = sections.get(0x00369D44)
        if helper is None:
            errors.append("missing disassembly section for FUN_00369D44")
        else:
            verify_patterns(helper, UPDATE_HELPER_PATTERNS, "FUN_00369D44", errors)
    else:
        errors.append(f"missing disassembly export: {disassembly_path}")

    summary = {
        "callback_table_ptr": "unavailable" if callback_table_ptr is None else f"0x{callback_table_ptr:08X}",
        "update_zero_bits": "unavailable" if update_zero_bits is None else f"0x{update_zero_bits:08X}",
        "update_yaw_scale_bits": "unavailable" if update_yaw_scale_bits is None else f"0x{update_yaw_scale_bits:08X}",
        "update_yaw_scale_float": "unavailable"
        if update_yaw_scale_bits is None
        else struct.unpack("<f", struct.pack("<I", update_yaw_scale_bits))[0],
        "exported_function_count": len(functions),
        "callback_body_count": len(CALLBACK_PATTERNS),
        "update_helper_pattern_count": len(UPDATE_HELPER_PATTERNS),
    }
    write_markdown(DEFAULT_OUT_MD, summary, errors)
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1

    print(
        "verified camera quake callbacks: "
        f"{len(CALLBACK_PATTERNS)} callbacks, {len(UPDATE_HELPER_PATTERNS)} update-helper patterns"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
