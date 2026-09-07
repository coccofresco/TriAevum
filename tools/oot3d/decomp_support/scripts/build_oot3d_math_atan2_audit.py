#!/usr/bin/env python3
"""Build the OOT3D native Math_Atan2S audit and source table."""

from __future__ import annotations

import csv
import json
import struct
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"

EXPORT = ANALYSIS / "title_intro_math_atan2_ghidra_export"
FUNCTIONS_CSV = EXPORT / "functions_selected.csv"
DISASSEMBLY = EXPORT / "disassembly_selected.txt"
GET_ATAN2_C = EXPORT / "decompiled" / "99000_002bc718_Math_GetAtan2Tbl.c"
ATAN2_EXPORT = ANALYSIS / "title_intro_player_action_motion_consumer_ghidra_export"
ATAN2_DISASSEMBLY = ATAN2_EXPORT / "disassembly_selected.txt"
ATAN2_C = ATAN2_EXPORT / "decompiled" / "99002_003758b0_Math_Atan2S.c"
CODE_BIN = Path(r"E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin")

OUT_JSON = ANALYSIS / "oot3d_math_atan2_audit.json"
OUT_MD = ANALYSIS / "oot3d_math_atan2_audit.md"
OUT_TABLE_CSV = ANALYSIS / "oot3d_math_atan2_table.csv"
OUT_HEADER = ROOT / "include" / "oot3d" / "math.h"
OUT_SOURCE = ROOT / "src" / "code" / "z_oot3d_math.c"

CODE_BASE = 0x00100000
MATH_GET_ATAN2_TBL_ENTRY = 0x002BC718
MATH_ATAN2S_ENTRY = 0x003758B0
MATH_ZERO_LITERAL_ADDR = 0x002BC758
MATH_TABLE_PTR_ADDR = 0x002BC75C
MATH_INDEX_SCALE_ADDR = 0x002BC760
MATH_INDEX_BIAS_ADDR = 0x002BC764
MATH_ATAN2_ZERO_ADDR = 0x003759CC
MATH_TABLE_COUNT = 0x401


def rel(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def read_text(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def write_json(path: Path, data: Any) -> None:
    write_text(path, json.dumps(data, indent=2) + "\n")


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def code_bytes() -> bytes:
    return CODE_BIN.read_bytes()


def literal_u32(code: bytes, addr: int) -> int:
    offset = addr - CODE_BASE
    data = code[offset : offset + 4]
    if len(data) != 4:
        raise SystemExit(f"could not read literal at 0x{addr:08X}")
    return struct.unpack("<I", data)[0]


def literal(addr: int, code: bytes) -> dict[str, Any]:
    bits = literal_u32(code, addr)
    return {
        "addr": f"0x{addr:08X}",
        "file_offset": f"0x{addr - CODE_BASE:08X}",
        "bits": f"0x{bits:08X}",
        "s32": struct.unpack("<i", struct.pack("<I", bits))[0],
        "f32": struct.unpack("<f", struct.pack("<I", bits))[0],
    }


def table_rows(code: bytes, table_addr: int) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    offset = table_addr - CODE_BASE
    for index in range(MATH_TABLE_COUNT):
        raw = code[offset + index * 2 : offset + index * 2 + 2]
        if len(raw) != 2:
            raise SystemExit(f"atan2 table truncated at index {index}")
        value = struct.unpack("<H", raw)[0]
        rows.append(
            {
                "index": index,
                "addr": f"0x{table_addr + index * 2:08X}",
                "value": value,
                "value_hex": f"0x{value:04X}",
            }
        )
    return rows


def functions_by_entry() -> dict[str, dict[str, str]]:
    return {row.get("entry", "").lower(): row for row in read_csv(FUNCTIONS_CSV) if row.get("entry")}


def find_line(path: Path, needle: str) -> dict[str, Any]:
    for line_number, line in enumerate(read_text(path).splitlines(), start=1):
        if needle in line:
            return {
                "file": rel(path),
                "line": line_number,
                "text": line.strip(),
                "found": True,
            }
    return {"file": rel(path), "line": 0, "text": "", "found": False}


def c_u16(value: int) -> str:
    return f"0x{value & 0xFFFF:04X}u"


def build_report() -> dict[str, Any]:
    code = code_bytes()
    functions = functions_by_entry()
    table_ptr = literal_u32(code, MATH_TABLE_PTR_ADDR)
    table = table_rows(code, table_ptr)
    literals = {
        "get_atan2_zero": literal(MATH_ZERO_LITERAL_ADDR, code),
        "table_ptr": literal(MATH_TABLE_PTR_ADDR, code),
        "index_scale": literal(MATH_INDEX_SCALE_ADDR, code),
        "index_bias": literal(MATH_INDEX_BIAS_ADDR, code),
        "atan2_zero": literal(MATH_ATAN2_ZERO_ADDR, code),
    }
    signals = [
        {
            "id": "get_atan2_tbl_exported",
            "status": "ok" if "002bc718" in functions else "missing",
            "evidence": [find_line(FUNCTIONS_CSV, "Math_GetAtan2Tbl")],
            "meaning": "The table lookup helper is exported at 0x002BC718.",
        },
        {
            "id": "get_atan2_tbl_uses_native_table_pointer",
            "status": "ok" if find_line(DISASSEMBLY, "002bc71c: ldr r2,[0x2bc75c]")["found"] else "missing",
            "evidence": [find_line(DISASSEMBLY, "002bc71c: ldr r2,[0x2bc75c]")],
            "meaning": "The helper loads the atan2 table pointer from the native literal pool.",
        },
        {
            "id": "get_atan2_tbl_index_formula",
            "status": "ok"
            if find_line(DISASSEMBLY, "002bc730: vdiv.f32 s1,s0,s1")["found"]
            and find_line(DISASSEMBLY, "002bc73c: vmla.f32 s0,s1,s2")["found"]
            and find_line(DISASSEMBLY, "002bc740: vcvt.s32.f32 s0,s0")["found"]
            else "missing",
            "evidence": [
                find_line(DISASSEMBLY, "002bc730: vdiv.f32 s1,s0,s1"),
                find_line(DISASSEMBLY, "002bc73c: vmla.f32 s0,s1,s2"),
                find_line(DISASSEMBLY, "002bc740: vcvt.s32.f32 s0,s0"),
            ],
            "meaning": "Index is vcvt_s32((arg0 / arg1) * 1024.0f + 0.5f).",
        },
        {
            "id": "get_atan2_tbl_index_limit_0x400",
            "status": "ok" if find_line(DISASSEMBLY, "002bc748: cmp r1,#0x400")["found"] else "missing",
            "evidence": [find_line(DISASSEMBLY, "002bc748: cmp r1,#0x400")],
            "meaning": "Only indexes <= 0x400 read from the table.",
        },
        {
            "id": "atan2s_calls_get_atan2_tbl",
            "status": "ok" if find_line(ATAN2_DISASSEMBLY, "bl 0x002bc718")["found"] else "missing",
            "evidence": [find_line(ATAN2_DISASSEMBLY, "bl 0x002bc718")],
            "meaning": "Math_Atan2S uses the lookup helper for its quadrant branches.",
        },
        {
            "id": "atan2s_returns_s16",
            "status": "ok" if find_line(ATAN2_DISASSEMBLY, "003759c4: sxth r0,r0")["found"] else "missing",
            "evidence": [find_line(ATAN2_DISASSEMBLY, "003759c4: sxth r0,r0")],
            "meaning": "The final result is sign-extended to s16.",
        },
    ]
    checks = {
        "get_atan2_tbl_exported": "002bc718" in functions,
        "native_literals_match_expected": literals["get_atan2_zero"]["bits"] == "0x00000000"
        and literals["atan2_zero"]["bits"] == "0x00000000"
        and literals["index_scale"]["f32"] == 1024.0
        and literals["index_bias"]["f32"] == 0.5,
        "native_table_pointer_decoded": table_ptr == 0x0050C0E4,
        "native_table_count_decoded": len(table) == MATH_TABLE_COUNT,
        "native_table_endpoints_match": table[0]["value"] == 0 and table[-1]["value"] == 0x2000,
        "consumer_signals_found": all(signal["status"] == "ok" for signal in signals),
    }
    return {
        "format": "oot3d_math_atan2_audit_v1",
        "inputs": {
            "functions": rel(FUNCTIONS_CSV),
            "get_atan2_disassembly": rel(DISASSEMBLY),
            "get_atan2_decompile": rel(GET_ATAN2_C),
            "atan2_disassembly": rel(ATAN2_DISASSEMBLY),
            "atan2_decompile": rel(ATAN2_C),
            "code_bin": str(CODE_BIN),
        },
        "summary": {
            "ok": all(checks.values()),
            "checks": checks,
            "table_addr": f"0x{table_ptr:08X}",
            "table_count": len(table),
            "next_gate": "Use Oot3d_MathAtan2S as the default heading provider for the open-title player-action motion runtime.",
        },
        "identity": {
            "math_get_atan2_tbl_entry": f"0x{MATH_GET_ATAN2_TBL_ENTRY:08X}",
            "math_atan2s_entry": f"0x{MATH_ATAN2S_ENTRY:08X}",
            "index_formula": "vcvt_s32((arg0 / arg1) * 1024.0f + 0.5f), if arg1 != 0 and index <= 0x400",
            "table_pointer_literal": f"0x{MATH_TABLE_PTR_ADDR:08X}",
        },
        "literals": literals,
        "signals": signals,
        "table_rows": table,
        "unresolved": [
            "The exported OOT3D decompiler signature is still undefined because VFP arguments are implicit in Ghidra output; the generated C names them as float arg0/arg1 according to the VFP register use in disassembly.",
        ],
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    lines = [
        "# OOT3D Math Atan2S",
        "",
        "This audit promotes `Math_GetAtan2Tbl` and `Math_Atan2S` from OOT3D code.bin evidence. The generated runtime uses the native 0x401-entry atan2 table extracted from `code.bin`, not host trig functions.",
        "",
        "## Summary",
        "",
        f"- OK: {data['summary']['ok']}",
        f"- `Math_GetAtan2Tbl`: `{data['identity']['math_get_atan2_tbl_entry']}`",
        f"- `Math_Atan2S`: `{data['identity']['math_atan2s_entry']}`",
        f"- Table: `{data['summary']['table_addr']}` entries `{data['summary']['table_count']}`",
        f"- Index formula: `{data['identity']['index_formula']}`",
        f"- Next gate: {data['summary']['next_gate']}",
        "",
        "## Checks",
        "",
        "| Check | Status |",
        "| --- | --- |",
    ]
    for key, value in data["summary"]["checks"].items():
        lines.append(f"| `{key}` | `{value}` |")
    lines.extend(["", "## Literals", "", "| Name | Addr | Bits | f32 |", "| --- | --- | --- | ---: |"])
    for name, row in data["literals"].items():
        lines.append(f"| `{name}` | `{row['addr']}` | `{row['bits']}` | {row['f32']:.9g} |")
    lines.extend(["", "## Signals", "", "| Signal | Status | Evidence | Meaning |", "| --- | --- | --- | --- |"])
    for signal in data["signals"]:
        evidence = []
        for item in signal["evidence"]:
            if item.get("found"):
                evidence.append(f"`{item['file']}:{item['line']}`")
        lines.append(f"| `{signal['id']}` | `{signal['status']}` | {'; '.join(evidence)} | {signal['meaning']} |")
    lines.extend(["", "## Table Samples", "", "| Index | Value |", "| ---: | ---: |"])
    for index in [0, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 768, 1024]:
        row = data["table_rows"][index]
        lines.append(f"| {row['index']} | `{row['value_hex']}` |")
    lines.extend(["", "## Unresolved", ""])
    for item in data["unresolved"]:
        lines.append(f"- {item}")
    write_text(path, "\n".join(lines) + "\n")


def write_header(path: Path, data: dict[str, Any]) -> None:
    lines = [
        "#ifndef OOT3D_MATH_H",
        "#define OOT3D_MATH_H",
        "",
        "#include \"oot3d/types.h\"",
        "",
        "#define OOT3D_MATH_GET_ATAN2_TBL_ENTRY 0x002BC718u",
        "#define OOT3D_MATH_ATAN2S_ENTRY 0x003758B0u",
        "#define OOT3D_MATH_ATAN2_TABLE_ADDR 0x0050C0E4u",
        f"#define OOT3D_MATH_ATAN2_TABLE_COUNT {len(data['table_rows'])}u",
        "#define OOT3D_MATH_ATAN2_INDEX_SCALE 1024.0f",
        "#define OOT3D_MATH_ATAN2_INDEX_BIAS 0.5f",
        "",
        "extern const u16 gOot3dMathAtan2Table[OOT3D_MATH_ATAN2_TABLE_COUNT];",
        "",
        "u16 Oot3d_MathGetAtan2Tbl(float arg0, float arg1);",
        "s16 Oot3d_MathAtan2S(float arg0, float arg1);",
        "",
        "#endif",
        "",
    ]
    write_text(path, "\n".join(lines))


def write_source(path: Path, data: dict[str, Any]) -> None:
    values = [row["value"] for row in data["table_rows"]]
    lines = [
        "#include \"oot3d/math.h\"",
        "",
        "const u16 gOot3dMathAtan2Table[OOT3D_MATH_ATAN2_TABLE_COUNT] = {",
    ]
    for index in range(0, len(values), 8):
        lines.append("    " + ", ".join(c_u16(value) for value in values[index : index + 8]) + ",")
    lines.extend(
        [
            "};",
            "",
            "u16 Oot3d_MathGetAtan2Tbl(float arg0, float arg1) {",
            "    s32 index;",
            "    if (arg1 == 0.0f) {",
            "        return gOot3dMathAtan2Table[0];",
            "    }",
            "    index = (s32)(((arg0 / arg1) * OOT3D_MATH_ATAN2_INDEX_SCALE) + OOT3D_MATH_ATAN2_INDEX_BIAS);",
            "    if (index <= 0x400) {",
            "        return gOot3dMathAtan2Table[index];",
            "    }",
            "    return gOot3dMathAtan2Table[0];",
            "}",
            "",
            "s16 Oot3d_MathAtan2S(float arg0, float arg1) {",
            "    s32 angle;",
            "    const float zero = 0.0f;",
            "    float negArg0 = -arg0;",
            "",
            "    if (arg1 < zero) {",
            "        arg1 = -arg1;",
            "        if (arg0 >= zero) {",
            "            if (arg0 < arg1) {",
            "                angle = (s32)Oot3d_MathGetAtan2Tbl(arg0, arg1) + 0xC000;",
            "            } else {",
            "                angle = -(s32)Oot3d_MathGetAtan2Tbl(arg1, arg0);",
            "            }",
            "        } else if (negArg0 < arg1) {",
            "            angle = 0xC000 - (s32)Oot3d_MathGetAtan2Tbl(negArg0, arg1);",
            "        } else {",
            "            angle = (s32)Oot3d_MathGetAtan2Tbl(arg1, negArg0) + 0x8000;",
            "        }",
            "    } else if (arg0 < zero) {",
            "        if (negArg0 >= arg1) {",
            "            angle = 0x8000 - (s32)Oot3d_MathGetAtan2Tbl(arg1, negArg0);",
            "        } else {",
            "            angle = (s32)Oot3d_MathGetAtan2Tbl(negArg0, arg1) + 0x4000;",
            "        }",
            "    } else if (arg0 < arg1) {",
            "        angle = 0x4000 - (s32)Oot3d_MathGetAtan2Tbl(arg0, arg1);",
            "    } else {",
            "        angle = (s32)Oot3d_MathGetAtan2Tbl(arg1, arg0);",
            "    }",
            "    return (s16)angle;",
            "}",
            "",
        ]
    )
    write_text(path, "\n".join(lines))


def main() -> None:
    data = build_report()
    write_json(OUT_JSON, data)
    write_markdown(OUT_MD, data)
    write_csv(OUT_TABLE_CSV, data["table_rows"])
    write_header(OUT_HEADER, data)
    write_source(OUT_SOURCE, data)
    if not data["summary"]["ok"]:
        raise SystemExit("OOT3D Math_Atan2S audit failed")


if __name__ == "__main__":
    main()
