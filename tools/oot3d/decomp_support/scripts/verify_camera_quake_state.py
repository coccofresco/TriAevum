#!/usr/bin/env python3
"""Verify reconstructed OOT3D camera quake request-state APIs."""

from __future__ import annotations

import csv
import re
import struct
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CODE_BIN = Path(r"E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin")
DEFAULT_EXPORT_DIR = ROOT / "analysis" / "camera_quake_state_ghidra_export"
DEFAULT_OUT_MD = ROOT / "analysis" / "camera_quake_state_verify.md"
DEFAULT_HEADER = ROOT / "include" / "oot3d" / "camera_quake.h"
DEFAULT_SOURCE = ROOT / "src" / "code" / "z_camera_quake.c"

CODE_BIN_VA_BASE = 0x00100000

EXPECTED_FUNCTIONS = {
    0x002C41E4: "Quake_RemoveFromIdx",
    0x0036F628: "Quake_SetCountdown",
    0x0036F6B0: "Quake_SetQuakeValues",
    0x0036F7C0: "Quake_SetSpeed",
    0x0036F848: "Quake_Add",
    0x003759D0: "Rand_ZeroOne",
    0x004B8D4C: "Quake_GetCountdown",
}

EXPECTED_LITERALS = {
    0x002C4248: (0x005A543C, "Quake_RemoveFromIdx request array pointer"),
    0x002C424C: (0x0053CAE4, "Quake_RemoveFromIdx runtime state pointer"),
    0x0036F6A4: (0x005A543C, "Quake_SetCountdown request array pointer"),
    0x0036F6A8: (0x40400000, "Quake_SetCountdown countdown scale 3.0"),
    0x0036F6AC: (0x3F000000, "Quake_SetCountdown signed bias 0.5"),
    0x0036F7B0: (0x005A543C, "Quake_SetQuakeValues request array pointer"),
    0x0036F7B4: (0x40000000, "Quake_SetQuakeValues param scale numerator 2.0"),
    0x0036F7B8: (0x3EAAAAAB, "Quake_SetQuakeValues param scale divisor reciprocal 1/3"),
    0x0036F7BC: (0x3F000000, "Quake_SetQuakeValues signed bias 0.5"),
    0x0036F838: (0x005A543C, "Quake_SetSpeed request array pointer"),
    0x0036F83C: (0x40000000, "Quake_SetSpeed param scale numerator 2.0"),
    0x0036F840: (0x3EAAAAAB, "Quake_SetSpeed param scale divisor reciprocal 1/3"),
    0x0036F844: (0x3F000000, "Quake_SetSpeed signed bias 0.5"),
    0x0036F950: (0x005A543C, "Quake_Add request array pointer"),
    0x0036F954: (0x47800000, "Quake_Add request-id random scale 65536.0"),
    0x0036F958: (0x0053CAE4, "Quake_Add runtime state pointer"),
    0x004B8D88: (0x005A543C, "Quake_GetCountdown request array pointer"),
}

COMMON_VALIDATION_PATTERNS = [
    "and r",
    ",#0x3",
    "add r",
    "lsl #0x3",
    "lsl #0x2",
    "ldrb",
    "[",
    "#0x8]",
    "ldrsh",
    "#0x0]",
]

FUNCTION_PATTERNS = {
    0x002C41E4: [
        "strb r2,[r1,#0x8]",
        "strh r0,[r1,#0x1c]",
        "ldr r0,[0x2c424c]",
        "sub r1,r1,#0x1",
        "strh r1,[r0,#0x2]",
    ],
    0x0036F628: [
        "vldr.32 s2,[pc,#0x3c]",
        "vldr.32 s0,[pc,#0x3c]",
        "cmp r1,#0x0",
        "vnmlsle.f32",
        "vmlagt.f32",
        "vcvt.s32.f32",
        "strh r0,[r2,#0x1c]",
        "strh r0,[r2,#0x2]",
    ],
    0x0036F6B0: [
        "vldr.32 s0,[pc,#0xb8]",
        "vldr.32 s2,[pc,#0xbc]",
        "vldr.32 s1,[pc,#0xb4]",
        "strh r0,[r12,#0xa]",
        "strh r0,[r12,#0xc]",
        "strh r0,[r12,#0xe]",
        "strh r0,[r12,#0x10]",
    ],
    0x0036F7C0: [
        "vldr.32 s2,[pc,#0x38]",
        "vldr.32 s1,[pc,#0x38]",
        "vldr.32 s0,[pc,#0x38]",
        "cmp r1,#0x0",
        "strh r0,[r2,#0x18]",
    ],
    0x0036F848: [
        "mov r2,#0x10000",
        "ldrb r3,[r12,#0x8]",
        "ldrb r3,[r12,#0x2c]",
        "ldrb r3,[r12,#0x50]",
        "ldrb r3,[r12,#0x74]",
        "ldrsh r3,[r12,#0x1c]",
        "ldrsh r3,[r12,#0x40]",
        "ldrsh r3,[r12,#0x64]",
        "ldrsh r3,[r12,#0x88]",
        "stmia r4!,{r1,r2,r3,r6,r12}",
        "stmia r4,{r1,r2,r3,r6}",
        "str r0,[r4,#0x4]",
        "add r0,r0,#0x100",
        "ldrh r0,[r0,#0xac]",
        "strh r0,[r4,#0x1e]",
        "strb lr,[r4,#0x8]",
        "strh r0,[r4,#0x1a]",
        "bl 0x003759d0",
        "bic r0,r0,#0x3",
        "strh r0,[r4,#0x0]",
        "strh r2,[r1,#0x2]",
    ],
    0x004B8D4C: [
        "adds r0,r1,#0x0",
        "ldrshne r0,[r1,#0x1c]",
    ],
}

SOURCE_PATTERNS = [
    "typedef struct {\n    s16 globalFlag;\n    s16 activeRequestCount;",
    "Oot3dCameraQuakeRequest requests[OOT3D_CAMERA_QUAKE_REQUEST_SLOT_COUNT];",
    "OOT3D_CAMERA_QUAKE_COUNTDOWN_SCALE = 3.0f * 0.5f",
    "OOT3D_CAMERA_QUAKE_PARAM_SCALE = 2.0f * 0.3333333432674408f",
    "OOT3D_CAMERA_QUAKE_REQUEST_ID_SCALE = 65536.0f",
    "Oot3d_CameraQuakeAdd(",
    "Oot3d_CameraQuakeRemoveFromIdx(",
    "Oot3d_CameraQuakeSetCountdown(",
    "Oot3d_CameraQuakeSetQuakeValues(",
    "Oot3d_CameraQuakeSetSpeed(",
    "Oot3d_CameraQuakeGetCountdown(",
]


def f32_from_bits(bits: int) -> float:
    return struct.unpack("<f", struct.pack("<I", bits & 0xFFFFFFFF))[0]


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
        "# Camera Quake Runtime State Verification",
        "",
        "Verification for the OOT3D camera quake request-state API reconstructed from `code.bin`.",
        "",
        "## Summary",
        "",
        f"- Exported functions checked: {summary['exported_function_count']}",
        f"- Native literal slots checked: {summary['literal_count']}",
        f"- Disassembly sections checked: {summary['section_count']}",
        f"- Source integration patterns checked: {summary['source_pattern_count']}",
        f"- Status: `{'pass' if not errors else 'fail'}`",
        "",
        "## Native Storage",
        "",
        "- Request array pointer: `0x005A543C`.",
        "- Runtime state pointer: `0x0053CAE4`; `+0x02` is the active request count.",
        "- Request slot count: 4; slot size: `0x24`; request id selects slot with `requestId & 3`.",
        "- A request id is valid only when `request+0x08 callbackIndex != 0` and `request+0x00 randIdx == requestId`.",
        "",
        "## API Semantics",
        "",
        "| native API | reconstructed entry point | recovered behavior |",
        "| --- | --- | --- |",
        "| `Quake_Add` | `Oot3d_CameraQuakeAdd` | chooses first free slot or lowest-countdown slot, zeroes `0x24` bytes, writes camera ref, callback index, camera slot, relative-camera flag, random request id, then increments active count |",
        "| `Quake_RemoveFromIdx` | `Oot3d_CameraQuakeRemoveFromIdx` | validates id, clears callback index, writes countdown `-1`, decrements active count |",
        "| `Quake_SetCountdown` | `Oot3d_CameraQuakeSetCountdown` | scales input by `3.0 * 0.5` with signed `0.5` bias, writes `+0x1C` and `+0x02` |",
        "| `Quake_SetQuakeValues` | `Oot3d_CameraQuakeSetQuakeValues` | scales y/x/fov/pitch by `2.0 * 0.3333333432674408` with signed `0.5` bias, writes `+0x0A/+0x0C/+0x0E/+0x10` |",
        "| `Quake_SetSpeed` | `Oot3d_CameraQuakeSetSpeed` | scales speed by `2.0 * 0.3333333432674408` with signed `0.5` bias, writes `+0x18` |",
        "| `Quake_GetCountdown` | `Oot3d_CameraQuakeGetCountdown` | returns `+0x1C` for valid ids and `0` otherwise |",
        "",
        "## Native Literals",
        "",
        "| address | expected | value | meaning |",
        "| ---: | ---: | ---: | --- |",
    ]
    literal_rows = summary["literal_rows"]
    assert isinstance(literal_rows, list)
    for address, expected, value, label in literal_rows:
        if isinstance(value, int):
            value_text = f"`0x{value:08X}`"
        else:
            value_text = "`unavailable`"
        float_note = ""
        if isinstance(expected, int) and expected in {0x40400000, 0x3F000000, 0x40000000, 0x3EAAAAAB, 0x47800000}:
            float_note = f" ({f32_from_bits(expected)})"
        lines.append(f"| `0x{address:08X}` | `0x{expected:08X}`{float_note} | {value_text} | {label} |")
    lines.extend(
        [
            "",
            "## Porting Note",
            "",
            "`Quake_Add` natively derives `cameraSlot` by reading `cameraRef + 0x1AC` (`cameraRef + 0x100 + 0xAC`). The reconstructed helper keeps the native request layout but accepts the camera slot explicitly, because the decomp-support module should not dereference a live 3DS camera object pointer.",
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
    literal_rows: list[tuple[int, int, int | None, str]] = []

    if DEFAULT_CODE_BIN.exists():
        data = DEFAULT_CODE_BIN.read_bytes()
        for va, (expected, label) in EXPECTED_LITERALS.items():
            value = read_u32(data, va)
            literal_rows.append((va, expected, value, label))
            if value != expected:
                errors.append(f"literal 0x{va:08X} expected 0x{expected:08X}, got {value!r}")
    else:
        errors.append(f"missing code.bin: {DEFAULT_CODE_BIN}")
        for va, (expected, label) in EXPECTED_LITERALS.items():
            literal_rows.append((va, expected, None, label))

    functions = load_functions(DEFAULT_EXPORT_DIR)
    for entry, name in EXPECTED_FUNCTIONS.items():
        if entry not in functions:
            errors.append(f"missing exported function {name} at 0x{entry:08X}")

    sections: dict[int, str] = {}
    disassembly_path = DEFAULT_EXPORT_DIR / "disassembly_selected.txt"
    if disassembly_path.exists():
        sections = split_disassembly_sections(disassembly_path.read_text(encoding="utf-8"))
        for entry, name in EXPECTED_FUNCTIONS.items():
            if entry not in sections:
                errors.append(f"missing disassembly section for {name} at 0x{entry:08X}")
        for entry in [
            0x002C41E4,
            0x0036F628,
            0x0036F6B0,
            0x0036F7C0,
            0x004B8D4C,
        ]:
            section = sections.get(entry)
            if section is not None:
                verify_patterns(section, COMMON_VALIDATION_PATTERNS, f"0x{entry:08X}", errors)
        for entry, patterns in FUNCTION_PATTERNS.items():
            section = sections.get(entry)
            if section is not None:
                verify_patterns(section, patterns, f"0x{entry:08X}", errors)
    else:
        errors.append(f"missing disassembly export: {disassembly_path}")

    source_text = ""
    if DEFAULT_HEADER.exists():
        source_text += DEFAULT_HEADER.read_text(encoding="utf-8")
    else:
        errors.append(f"missing source header: {DEFAULT_HEADER}")
    if DEFAULT_SOURCE.exists():
        source_text += "\n" + DEFAULT_SOURCE.read_text(encoding="utf-8")
    else:
        errors.append(f"missing source file: {DEFAULT_SOURCE}")
    for pattern in SOURCE_PATTERNS:
        if pattern not in source_text:
            errors.append(f"source missing pattern `{pattern}`")

    summary: dict[str, object] = {
        "exported_function_count": len(functions),
        "literal_count": len(EXPECTED_LITERALS),
        "section_count": len(sections),
        "source_pattern_count": len(SOURCE_PATTERNS),
        "literal_rows": literal_rows,
    }
    write_markdown(DEFAULT_OUT_MD, summary, errors)
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1

    print(
        "verified camera quake runtime state: "
        f"{len(EXPECTED_FUNCTIONS)} functions, {len(EXPECTED_LITERALS)} native literals"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
