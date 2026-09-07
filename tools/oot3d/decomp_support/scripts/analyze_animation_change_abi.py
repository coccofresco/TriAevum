#!/usr/bin/env python3
"""Audit Animation_Change callsites that need OOT3D hard-float expansion."""

from __future__ import annotations

import argparse
import csv
import json
import re
import struct
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CODE_BIN = ROOT.parent / "work" / "extract" / "exefs" / "code.bin"
DEFAULT_CODE_BASE = 0x00100000

DECOMP_FILE_RE = re.compile(r"^\d+_([0-9a-fA-F]{8})_(.+)\.c$")
DISASM_HEADER_RE = re.compile(r"^//\s+(?P<name>\S+)\s+@\s+(?P<addr>[0-9a-fA-F]{8})\s*$")
DISASM_INSN_RE = re.compile(r"^(?P<addr>[0-9a-fA-F]{8}):\s+(?P<op>.+?)\s*$")
IMM_MOV_RE = re.compile(r"\bmov\s+(?P<reg>r[12]),#(?P<value>0x[0-9a-fA-F]+|\d+)\b")
SKEL_ADD_RE = re.compile(r"\badd\s+r0,\s*r\d+,#(?P<offset>0x[0-9a-fA-F]+|\d+)\b")
VFP_PC_LDR_RE = re.compile(r"\bvldr\.32\s+(?P<dst>s\d+),\s*\[pc,#(?P<imm>0x[0-9a-fA-F]+|\d+)\]")
VFP_STACK_LDR_RE = re.compile(r"\bvldr\.32\s+(?P<dst>s\d+),\s*\[(?P<base>sp|r\d+)(?:,#(?P<imm>0x[0-9a-fA-F]+|\d+))?\]")
VFP_MOV_RE = re.compile(r"\bvmov\.f32\s+(?P<dst>s\d+),\s*(?P<src>s\d+)\b")
VFP_REG_MOV_RE = re.compile(r"\bvmov\s+(?P<dst>s\d+),\s*(?P<src>r\d+)\b")
VFP_CVT_RE = re.compile(r"\bvcvt\.f32\.s32\s+(?P<dst>s\d+),\s*(?P<src>s\d+)\b")
VFP_BIN_RE = re.compile(r"\bv(?P<op>add|sub|mul)\.f32\s+(?P<dst>s\d+),\s*(?P<a>s\d+),\s*(?P<b>s\d+)\b")
CALL_RE = re.compile(r"\bbl\s+0x(?P<target>[0-9a-fA-F]{8})\b")
UNRESOLVED_EXPR_RE = re.compile(r"\b[rs]\d+\b|\?")
SOURCE_FUNC_RE = re.compile(
    r"^\s*(?:[A-Za-z_][A-Za-z0-9_]*\s+|static\s+|inline\s+|__attribute__\s*\(\([^)]*\)\)\s+|"
    r"OOT3D_FORCE_INLINE\s+|OOT3D_NAKED\s+)*"
    r"(?P<name>oot3d_[A-Za-z0-9_]+)\s*\([^;]*\)\s*\{"
)


@dataclass
class DisasmFunction:
    entry: str
    name: str
    instructions: list[tuple[str, str]]


@dataclass
class CodeImage:
    path: Path
    base: int
    data: bytes

    @property
    def available(self) -> bool:
        return bool(self.data)

    def read_u32(self, address: int) -> int | None:
        offset = address - self.base
        if offset < 0 or offset + 4 > len(self.data):
            return None
        return struct.unpack_from("<I", self.data, offset)[0]

    def read_f32(self, address: int) -> float | None:
        offset = address - self.base
        if offset < 0 or offset + 4 > len(self.data):
            return None
        return struct.unpack_from("<f", self.data, offset)[0]


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def read_text(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def parse_int(value: str) -> int:
    return int(value, 0)


def load_code_image(path: Path, base: int) -> CodeImage | None:
    if not path.is_file():
        return None
    return CodeImage(path=path, base=base, data=path.read_bytes())


def format_float(value: float) -> str:
    if value == 0.0:
        return "0.0f"
    if value == int(value) and abs(value) < 1000000:
        return f"{int(value)}.0f"
    return f"{value:.9g}f"


def literal_expr(address: int, code_image: CodeImage | None) -> tuple[str, str]:
    if code_image is None:
        return f"LIT_{address:08x}", f"{address:08x}=unresolved"
    raw = code_image.read_u32(address)
    value = code_image.read_f32(address)
    if raw is None or value is None:
        return f"LIT_{address:08x}", f"{address:08x}=out_of_range"
    expr = format_float(value)
    return expr, f"{address:08x}={expr}/0x{raw:08x}"


def expr_bin(left: str, symbol: str, right: str) -> str:
    if not left:
        left = "?"
    if not right:
        right = "?"
    if symbol == "+" and right == "0.0f":
        return left
    if symbol == "+" and left == "0.0f":
        return right
    if symbol == "-" and right == "0.0f":
        return left
    if symbol == "*" and right == "1.0f":
        return left
    if symbol == "*" and left == "1.0f":
        return right
    if symbol == "*" and (right == "0.0f" or left == "0.0f"):
        return "0.0f"
    return f"({left} {symbol} {right})"


def expression_is_resolved(expr: str) -> bool:
    return bool(expr) and not UNRESOLVED_EXPR_RE.search(expr)


def used_rand_temp_names(expressions: list[str], rand_count: int) -> list[str]:
    joined = " ".join(expressions)
    return [f"rand{index}" for index in range(1, rand_count + 1) if re.search(rf"\brand{index}\b", joined)]


def split_args(arg_text: str) -> list[str]:
    args: list[str] = []
    start = 0
    depth = 0
    in_string: str | None = None
    escape = False
    for index, char in enumerate(arg_text):
        if in_string:
            if escape:
                escape = False
            elif char == "\\":
                escape = True
            elif char == in_string:
                in_string = None
            continue
        if char in {'"', "'"}:
            in_string = char
            continue
        if char in "([{":
            depth += 1
        elif char in ")]}":
            depth = max(0, depth - 1)
        elif char == "," and depth == 0:
            args.append(arg_text[start:index].strip())
            start = index + 1
    tail = arg_text[start:].strip()
    if tail:
        args.append(tail)
    return args


def extract_calls(text: str, callee: str) -> list[dict[str, Any]]:
    calls: list[dict[str, Any]] = []
    token = f"{callee}("
    offset = 0
    while True:
        start = text.find(token, offset)
        if start == -1:
            break
        arg_start = start + len(token)
        depth = 1
        index = arg_start
        in_string: str | None = None
        escape = False
        while index < len(text) and depth:
            char = text[index]
            if in_string:
                if escape:
                    escape = False
                elif char == "\\":
                    escape = True
                elif char == in_string:
                    in_string = None
            elif char in {'"', "'"}:
                in_string = char
            elif char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
            index += 1
        if depth == 0:
            arg_text = text[arg_start : index - 1]
            calls.append(
                {
                    "offset": start,
                    "line": text.count("\n", 0, start) + 1,
                    "callee": callee,
                    "args": split_args(arg_text),
                    "call": " ".join(text[start:index].split()),
                }
            )
        offset = max(index, start + len(token))
    return calls


def parse_decompiled_calls(decompiled_dir: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for path in sorted(decompiled_dir.glob("*.c")):
        text = read_text(path)
        if "FUN_00375c08" not in text:
            continue
        match = DECOMP_FILE_RE.match(path.name)
        entry = match.group(1).lower() if match else ""
        name = match.group(2) if match else path.stem
        lines = text.splitlines()
        for call in extract_calls(text, "FUN_00375c08"):
            line_no = int(call["line"])
            window = "\n".join(lines[max(0, line_no - 8) : min(len(lines), line_no + 3)])
            args = call["args"]
            rows.append(
                {
                    "entry": entry,
                    "function": name,
                    "file": rel(path),
                    "line": line_no,
                    "arg_count": len(args),
                    "skel_arg": args[0] if len(args) > 0 else "",
                    "anim_arg": args[1] if len(args) > 1 else "",
                    "mode_arg": args[2] if len(args) > 2 else "",
                    "preceded_by_last_frame": "FUN_0036ae14" in window,
                    "preceded_by_vector_signed_to_float": "VectorSignedToFloat" in window,
                    "call": call["call"],
                }
            )
    return rows


def parse_disassembly(path: Path) -> dict[str, DisasmFunction]:
    functions: dict[str, DisasmFunction] = {}
    current_entry = ""
    current_name = ""
    instructions: list[tuple[str, str]] = []

    def flush() -> None:
        if current_entry and current_name:
            functions[current_entry] = DisasmFunction(current_entry, current_name, list(instructions))

    for raw in read_text(path).splitlines():
        header = DISASM_HEADER_RE.match(raw)
        if header:
            flush()
            current_entry = header.group("addr").lower()
            current_name = header.group("name")
            instructions = []
            continue
        if not raw.strip():
            flush()
            current_entry = ""
            current_name = ""
            instructions = []
            continue
        if not current_entry:
            continue
        insn = DISASM_INSN_RE.match(raw)
        if insn:
            instructions.append((insn.group("addr").lower(), insn.group("op").strip().lower()))
    flush()
    return functions


def last_immediate(window: list[str], reg: str) -> str:
    for op in reversed(window):
        match = IMM_MOV_RE.search(op)
        if match and match.group("reg") == reg:
            return hex(parse_int(match.group("value")))
    return ""


def last_skel_offset(window: list[str]) -> str:
    for op in reversed(window):
        match = SKEL_ADD_RE.search(op)
        if match:
            return hex(parse_int(match.group("offset")))
    return ""


def last_assignment(window: list[str], sreg: str) -> str:
    # Keep this textual: it is evidence for the person doing the port.
    patterns = (
        f" {sreg},",
        f".f32 {sreg},",
        f" {sreg},",
    )
    for op in reversed(window):
        if any(pattern in op for pattern in patterns):
            if re.search(rf"\b{re.escape(sreg)}\b", op):
                return op
    return ""


def classify_float_window(window: list[str], anim: str, mode: str) -> str:
    joined = "\n".join(window)
    last_frame_indexes = [index for index, op in enumerate(window) if op == "bl 0x0036ae14"]
    has_last_frame = bool(last_frame_indexes)
    last_frame_tail = window[last_frame_indexes[-1] + 1 :] if last_frame_indexes else window
    tail_joined = "\n".join(last_frame_tail)
    rand_after_last_frame = "bl 0x003759d0" in "\n".join(last_frame_tail)
    has_frame_end = "vcvt.f32.s32 s2,s0" in tail_joined or "vmov.f32 s2," in tail_joined
    has_minus_one_start = "vsub.f32 s1,s2,s0" in tail_joined
    has_zero_start = "vmov.f32 s1," in tail_joined or "vldr.32 s1," in tail_joined
    has_constant_speed = "vldr.32 s0," in tail_joined or "vmov.f32 s0," in tail_joined

    if has_last_frame and has_minus_one_start and not rand_after_last_frame:
        return "last_frame_minus_one"
    if has_last_frame and rand_after_last_frame:
        return "randomized_window"
    if has_last_frame and has_frame_end and has_zero_start and has_constant_speed:
        return "last_frame_constant_window"
    if has_last_frame and has_frame_end:
        return "last_frame_manual_window"
    if anim and mode:
        return "literal_or_table_window"
    return "manual_float_window"


def suggestion_for(pattern: str) -> str:
    if pattern == "last_frame_minus_one":
        return (
            "lastFrame=FUN_0036ae14(skel,anim); "
            "oot3d_anim_change_full(skel,anim,mode,1.0f,(float)lastFrame-1.0f,(float)lastFrame,0.0f)"
        )
    if pattern == "last_frame_constant_window":
        return (
            "lastFrame=FUN_0036ae14(skel,anim); "
            "oot3d_anim_change_full(skel,anim,mode,CONST_SPEED,CONST_START,(float)lastFrame,CONST_MORPH)"
        )
    if pattern == "randomized_window":
        return "expand manually: one or more s0/s1/s3 values are Rand_ZeroOne/literal derived"
    if pattern == "last_frame_manual_window":
        return "expand manually: target uses last frame but float window is not a known template yet"
    return "inspect target VFP window before replacing the 3-arg decompiler call"


def reconstruct_abi_args(raw_window: list[tuple[str, str]], code_image: CodeImage | None) -> dict[str, Any]:
    sregs: dict[str, str] = {}
    rregs: dict[str, str] = {}
    literal_refs: list[str] = []
    rand_count = 0

    for addr_text, op in raw_window:
        mov_imm = IMM_MOV_RE.search(op)
        if mov_imm:
            rregs[mov_imm.group("reg")] = hex(parse_int(mov_imm.group("value")))

        skel_add = SKEL_ADD_RE.search(op)
        if skel_add:
            rregs["r0"] = f"skel+{hex(parse_int(skel_add.group('offset')))}"

        vldr = VFP_PC_LDR_RE.search(op)
        if vldr:
            literal_address = int(addr_text, 16) + 8 + parse_int(vldr.group("imm"))
            expr, literal_ref = literal_expr(literal_address, code_image)
            sregs[vldr.group("dst")] = expr
            literal_refs.append(literal_ref)
            continue

        stack_vldr = VFP_STACK_LDR_RE.search(op)
        if stack_vldr:
            base = stack_vldr.group("base")
            imm = stack_vldr.group("imm")
            offset = f"+{hex(parse_int(imm))}" if imm else ""
            sregs[stack_vldr.group("dst")] = f"{base}{offset}"
            continue

        vmov = VFP_MOV_RE.search(op)
        if vmov:
            sregs[vmov.group("dst")] = sregs.get(vmov.group("src"), vmov.group("src"))
            continue

        reg_mov = VFP_REG_MOV_RE.search(op)
        if reg_mov:
            sregs[reg_mov.group("dst")] = rregs.get(reg_mov.group("src"), reg_mov.group("src"))
            continue

        vcvt = VFP_CVT_RE.search(op)
        if vcvt:
            source = sregs.get(vcvt.group("src"), vcvt.group("src"))
            if source == "lastFrame":
                sregs[vcvt.group("dst")] = "(float)lastFrame"
            else:
                sregs[vcvt.group("dst")] = f"(float){source}"
            continue

        vbin = VFP_BIN_RE.search(op)
        if vbin:
            left = sregs.get(vbin.group("a"), vbin.group("a"))
            right = sregs.get(vbin.group("b"), vbin.group("b"))
            symbol = {"add": "+", "sub": "-", "mul": "*"}[vbin.group("op")]
            sregs[vbin.group("dst")] = expr_bin(left, symbol, right)
            continue

        call = CALL_RE.search(op)
        if call:
            target = call.group("target").lower()
            if target == "0036ae14":
                rregs["r0"] = "lastFrame"
            elif target == "003759d0":
                rand_count += 1
                sregs["s0"] = f"rand{rand_count}"

    expressions = {
        "play_speed_expr": sregs.get("s0", ""),
        "start_frame_expr": sregs.get("s1", ""),
        "end_frame_expr": sregs.get("s2", ""),
        "morph_expr": sregs.get("s3", ""),
        "rand_temps": "",
        "literal_refs": " ".join(dict.fromkeys(literal_refs)),
    }
    used_rand_names = used_rand_temp_names(
        [str(expressions[key]) for key in ("play_speed_expr", "start_frame_expr", "end_frame_expr", "morph_expr")],
        rand_count,
    )
    expressions["rand_temps"] = "; ".join(f"{name}=FUN_003759d0()" for name in used_rand_names)
    expressions["resolved_abi"] = all(
        expression_is_resolved(str(expressions[key]))
        for key in ("play_speed_expr", "start_frame_expr", "end_frame_expr", "morph_expr")
    )
    return expressions


def full_suggestion(pattern: str, anim: str, mode: str, abi: dict[str, Any]) -> str:
    if not abi.get("resolved_abi"):
        return suggestion_for(pattern)
    temps = []
    if abi.get("rand_temps"):
        temps.append(str(abi["rand_temps"]))
    temps.append(
        "oot3d_anim_change_full("
        f"skel,{anim or 'anim'},{mode or 'mode'},"
        f"{abi['play_speed_expr']},{abi['start_frame_expr']},{abi['end_frame_expr']},{abi['morph_expr']})"
    )
    if "lastFrame" in " ".join(str(abi.get(key, "")) for key in ("play_speed_expr", "start_frame_expr", "end_frame_expr", "morph_expr")):
        temps.insert(0, "lastFrame=FUN_0036ae14(skel,anim)")
    return "; ".join(temps)


def parse_disasm_calls(functions: dict[str, DisasmFunction], code_image: CodeImage | None) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for function in functions.values():
        for index, (addr, op) in enumerate(function.instructions):
            if op != "bl 0x00375c08":
                continue
            raw_window = function.instructions[max(0, index - 48) : index]
            window = [entry[1] for entry in raw_window]
            anim = last_immediate(window, "r1")
            mode = last_immediate(window, "r2")
            skel_offset = last_skel_offset(window)
            pattern = classify_float_window(window, anim, mode)
            abi = reconstruct_abi_args(raw_window, code_image)
            rows.append(
                {
                    "entry": function.entry,
                    "function": function.name,
                    "call_addr": addr,
                    "anim": anim,
                    "mode": mode,
                    "skel_offset": skel_offset,
                    "pattern": pattern,
                    "suggestion": full_suggestion(pattern, anim, mode, abi),
                    "has_last_frame": "bl 0x0036ae14" in "\n".join(window),
                    "rand_calls": "\n".join(window).count("bl 0x003759d0"),
                    **abi,
                    "s0": last_assignment(window, "s0"),
                    "s1": last_assignment(window, "s1"),
                    "s2": last_assignment(window, "s2"),
                    "s3": last_assignment(window, "s3"),
                    "window": " | ".join(f"{a}: {o}" for a, o in raw_window[-12:]),
                }
            )
    return rows


def parse_source_calls(paths: list[Path]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for path in sorted(set(paths)):
        text = read_text(path)
        if "FUN_00375c08" not in text and "oot3d_anim_change_full" not in text:
            continue
        lines = text.splitlines()
        line_offsets: list[int] = []
        offset = 0
        for line in lines:
            line_offsets.append(offset)
            offset += len(line) + 1

        function_by_line: dict[int, str] = {}
        current = ""
        depth = 0
        for line_no, line in enumerate(lines, start=1):
            if depth == 0:
                match = SOURCE_FUNC_RE.match(line)
                if match:
                    current = match.group("name")
            depth += line.count("{") - line.count("}")
            if current:
                function_by_line[line_no] = current
            if depth <= 0:
                depth = 0
                current = ""

        for callee in ("FUN_00375c08", "oot3d_anim_change_full"):
            for call in extract_calls(text, callee):
                line_no = int(call["line"])
                function = function_by_line.get(line_no, "")
                if not function:
                    continue
                rows.append(
                    {
                        "file": rel(path),
                        "line": line_no,
                        "function": function,
                        "callee": callee,
                        "arg_count": len(call["args"]),
                        "call": call["call"],
                    }
                )
    return rows


def load_port_map(path: Path) -> tuple[dict[str, dict[str, str]], dict[str, list[dict[str, str]]]]:
    rows = read_csv(path)
    by_entry = {row.get("oot3d_entry", "").lower(): row for row in rows}
    by_source: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        port_file = row.get("port_file", "")
        if port_file:
            by_source[port_file.replace("\\", "/")].append(row)
    return by_entry, by_source


def build_joined_rows(
    disasm_rows: list[dict[str, Any]],
    decomp_rows: list[dict[str, Any]],
    source_rows: list[dict[str, Any]],
    port_map: dict[str, dict[str, str]],
) -> list[dict[str, Any]]:
    decomp_by_entry: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in decomp_rows:
        decomp_by_entry[str(row["entry"])].append(row)

    source_by_function: Counter[tuple[str, str, str]] = Counter()
    source_full_by_function: Counter[tuple[str, str, str]] = Counter()
    source_by_file: Counter[tuple[str, str]] = Counter()
    source_full_by_file: Counter[tuple[str, str]] = Counter()
    for row in source_rows:
        key = (str(row["file"]), str(row["function"]), str(row["callee"]))
        file_key = (str(row["file"]), str(row["callee"]))
        if row["callee"] == "FUN_00375c08":
            source_by_function[key] += 1
            source_by_file[file_key] += 1
        elif row["callee"] == "oot3d_anim_change_full":
            source_full_by_function[key] += 1
            source_full_by_file[file_key] += 1

    rows: list[dict[str, Any]] = []
    for row in disasm_rows:
        entry = str(row["entry"])
        mapped = port_map.get(entry, {})
        port_file = mapped.get("port_file", "").replace("\\", "/")
        oot3d_name = mapped.get("oot3d_name", "")
        source_fun_3arg = source_by_function[(port_file, oot3d_name, "FUN_00375c08")]
        source_fun_full = source_full_by_function[(port_file, oot3d_name, "oot3d_anim_change_full")]
        rows.append(
            {
                **row,
                "mapped_status": mapped.get("status", ""),
                "n64_name": mapped.get("n64_name", ""),
                "port_file": port_file,
                "source_function_3arg_calls": source_fun_3arg,
                "source_function_full_calls": source_fun_full,
                "source_file_3arg_calls": source_by_file[(port_file, "FUN_00375c08")] if port_file else 0,
                "source_file_full_calls": source_full_by_file[(port_file, "oot3d_anim_change_full")] if port_file else 0,
                "decompiler_arg_counts": " ".join(
                    str(item["arg_count"]) for item in decomp_by_entry.get(entry, []) if item.get("arg_count")
                ),
                "decompiler_vector_evidence": sum(
                    1 for item in decomp_by_entry.get(entry, []) if item.get("preceded_by_vector_signed_to_float")
                ),
            }
        )
    return rows


def write_markdown(path: Path, payload: dict[str, Any], joined_rows: list[dict[str, Any]], source_rows: list[dict[str, Any]]) -> None:
    summary = payload["summary"]
    pattern_counts = summary["target_patterns"]
    signature_counts = Counter(
        (
            str(row.get("play_speed_expr", "")),
            str(row.get("start_frame_expr", "")),
            str(row.get("end_frame_expr", "")),
            str(row.get("morph_expr", "")),
        )
        for row in joined_rows
        if row.get("resolved_abi")
    )
    lines = [
        "# Animation Change ABI Audit",
        "",
        "This report identifies OOT3D `FUN_00375c08` callsites where Ghidra collapses the hard-float arguments.",
        f"Literal pool values are resolved from `{summary['code_bin']}` when available.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Target `FUN_00375c08` callsites | {summary['target_calls']} |",
        f"| Target functions with callsites | {summary['target_functions']} |",
        f"| Mapped target callsites | {summary['mapped_target_calls']} |",
        f"| Target callsites with resolved ABI float args | {summary['resolved_abi_calls']} |",
        f"| Mapped callsites with resolved ABI float args | {summary['resolved_mapped_calls']} |",
        f"| Decompiled callsites with `VectorSignedToFloat` evidence | {summary['decompiler_vector_evidence']} |",
        f"| Source 3-arg `FUN_00375c08` callsites | {summary['source_3arg_calls']} |",
        f"| Source full ABI wrapper callsites | {summary['source_full_calls']} |",
        "",
        "## Replicable Structures",
        "",
        "| Pattern | Target calls | Conversion rule |",
        "| --- | ---: | --- |",
    ]

    descriptions = {
        "last_frame_minus_one": "`endFrame = Animation_GetLastFrame(anim)`, `startFrame = endFrame - 1.0f`.",
        "last_frame_constant_window": "`endFrame = Animation_GetLastFrame(anim)` with constant speed/start/morph literals.",
        "randomized_window": "`Animation_Change` float args are derived from `Rand_ZeroOne` and literals; convert when the resolved ABI columns are complete.",
        "last_frame_manual_window": "`Animation_GetLastFrame` is present but the VFP register window does not match a promoted template.",
        "literal_or_table_window": "Animation/mode are available but float args must be read from VFP setup or data tables.",
        "manual_float_window": "Manual inspection required.",
    }
    for pattern, count in sorted(pattern_counts.items(), key=lambda item: (-item[1], item[0])):
        lines.append(f"| `{pattern}` | {count} | {descriptions.get(pattern, 'Manual inspection required.')} |")

    lines.extend(
        [
            "",
            "## Resolved ABI Signatures",
            "",
            "| Count | playSpeed | startFrame | endFrame | morphFrames |",
            "| ---: | --- | --- | --- | --- |",
        ]
    )
    for (play_speed, start_frame, end_frame, morph), count in signature_counts.most_common(12):
        lines.append(f"| {count} | `{play_speed}` | `{start_frame}` | `{end_frame}` | `{morph}` |")
    if not signature_counts:
        lines.append("| 0 | - | - | - | - |")

    lines.extend(
        [
            "",
            "## Mapped Calls",
            "",
            "| Entry | Function | Anim | Mode | Pattern | ABI | Source 3-arg | Full | Suggested action |",
            "| --- | --- | ---: | ---: | --- | --- | ---: | ---: | --- |",
        ]
    )
    mapped_rows = [row for row in joined_rows if row.get("mapped_status")]
    for row in mapped_rows[:80]:
        abi = (
            f"{row.get('play_speed_expr', '')}, {row.get('start_frame_expr', '')}, "
            f"{row.get('end_frame_expr', '')}, {row.get('morph_expr', '')}"
            if row.get("resolved_abi")
            else ""
        )
        lines.append(
            f"| `{row['entry']}` | `{row['function']}` | `{row['anim']}` | `{row['mode']}` | "
            f"`{row['pattern']}` | `{abi}` | {row['source_file_3arg_calls']} | {row['source_file_full_calls']} | "
            f"{row['suggestion']} |"
        )
    if not mapped_rows:
        lines.append("| - | - | - | - | - | - | 0 | 0 | - |")

    unresolved = [row for row in source_rows if row["callee"] == "FUN_00375c08"]
    lines.extend(
        [
            "",
            "## Source Calls Still Using Collapsed Prototype",
            "",
            "| File | Line | Function | Args | Call |",
            "| --- | ---: | --- | ---: | --- |",
        ]
    )
    for row in unresolved[:80]:
        lines.append(
            f"| `{row['file']}` | {row['line']} | `{row['function']}` | {row['arg_count']} | `{row['call']}` |"
        )
    if not unresolved:
        lines.append("| - | 0 | - | 0 | - |")

    lines.extend(
        [
            "",
            "## Next Batch Rule",
            "",
            "Prefer source conversion when `resolved_abi` is true. For unresolved rows, mine the shown "
            "literal refs and remaining raw `sN` registers before replacing the collapsed decompiler call.",
            "",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def build_payload(joined_rows: list[dict[str, Any]], decomp_rows: list[dict[str, Any]], source_rows: list[dict[str, Any]]) -> dict[str, Any]:
    target_patterns = Counter(str(row["pattern"]) for row in joined_rows)
    source_3arg = [row for row in source_rows if row["callee"] == "FUN_00375c08"]
    source_full = [row for row in source_rows if row["callee"] == "oot3d_anim_change_full"]
    return {
        "summary": {
            "target_calls": len(joined_rows),
            "target_functions": len({row["entry"] for row in joined_rows}),
            "mapped_target_calls": sum(1 for row in joined_rows if row.get("mapped_status")),
            "resolved_abi_calls": sum(1 for row in joined_rows if row.get("resolved_abi")),
            "resolved_mapped_calls": sum(1 for row in joined_rows if row.get("mapped_status") and row.get("resolved_abi")),
            "target_patterns": dict(sorted(target_patterns.items())),
            "decompiled_calls": len(decomp_rows),
            "decompiler_vector_evidence": sum(1 for row in decomp_rows if row.get("preceded_by_vector_signed_to_float")),
            "source_3arg_calls": len(source_3arg),
            "source_full_calls": len(source_full),
            "code_bin": "",
        },
        "target_calls": joined_rows,
        "decompiled_calls": decomp_rows,
        "source_calls": source_rows,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-json", type=Path, default=ROOT / "analysis" / "animation_change_abi.json")
    parser.add_argument("--out-md", type=Path, default=ROOT / "analysis" / "animation_change_abi.md")
    parser.add_argument("--out-csv", type=Path, default=ROOT / "analysis" / "animation_change_abi_calls.csv")
    parser.add_argument("--out-source-csv", type=Path, default=ROOT / "analysis" / "animation_change_source_calls.csv")
    parser.add_argument("--code-bin", type=Path, default=DEFAULT_CODE_BIN)
    parser.add_argument("--code-base", type=lambda value: int(value, 0), default=DEFAULT_CODE_BASE)
    args = parser.parse_args()

    port_map, by_source = load_port_map(ROOT / "metadata" / "n64_port_map.csv")
    code_image = load_code_image(args.code_bin, args.code_base)
    decomp_rows = parse_decompiled_calls(ROOT / "ghidra_export" / "decompiled")
    disasm_rows = parse_disasm_calls(parse_disassembly(ROOT / "ghidra_export" / "disassembly.txt"), code_image)
    source_paths = [ROOT / path for path in by_source if (ROOT / path).is_file()]
    source_rows = parse_source_calls(source_paths)
    joined_rows = build_joined_rows(disasm_rows, decomp_rows, source_rows, port_map)
    payload = build_payload(joined_rows, decomp_rows, source_rows)
    payload["summary"]["code_bin"] = rel(args.code_bin) if code_image else "missing"

    write_json(args.out_json, payload)
    write_csv(
        args.out_csv,
        joined_rows,
        [
            "entry",
            "function",
            "call_addr",
            "mapped_status",
            "n64_name",
            "port_file",
            "anim",
            "mode",
            "skel_offset",
            "pattern",
            "has_last_frame",
            "rand_calls",
            "resolved_abi",
            "play_speed_expr",
            "start_frame_expr",
            "end_frame_expr",
            "morph_expr",
            "rand_temps",
            "literal_refs",
            "source_function_3arg_calls",
            "source_function_full_calls",
            "source_file_3arg_calls",
            "source_file_full_calls",
            "decompiler_arg_counts",
            "decompiler_vector_evidence",
            "s0",
            "s1",
            "s2",
            "s3",
            "suggestion",
            "window",
        ],
    )
    write_csv(args.out_source_csv, source_rows, ["file", "line", "function", "callee", "arg_count", "call"])
    write_markdown(args.out_md, payload, joined_rows, source_rows)

    summary = payload["summary"]
    patterns = ", ".join(f"{key}:{value}" for key, value in summary["target_patterns"].items())
    print(
        f"animation calls: {summary['target_calls']} target, {summary['mapped_target_calls']} mapped; "
        f"source collapsed/full: {summary['source_3arg_calls']}/{summary['source_full_calls']}; "
        f"patterns: {patterns}"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
