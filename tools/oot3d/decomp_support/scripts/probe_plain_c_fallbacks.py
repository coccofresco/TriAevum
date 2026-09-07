#!/usr/bin/env python3
"""Probe naked-asm helper fallbacks as standalone plain-C match candidates."""

from __future__ import annotations

import argparse
import csv
import json
import os
import re
import shutil
import subprocess
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from compare_runtime_objects import (
    apply_target_aliases,
    compare_ops,
    read_manual_symbol_aliases,
    read_objdump_functions,
    read_target_functions,
)


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCES = [
    ROOT / "src" / "runtime" / "runtime_helpers.c",
    ROOT / "src" / "runtime" / "matrix_helpers.c",
    ROOT / "src" / "startup" / "clear_bss.c",
]
DEFAULT_BUILD_DIR = ROOT / "build" / "plain_c_fallback_probe"
DEFAULT_OUT_JSON = ROOT / "analysis" / "plain_c_fallback_probe.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "plain_c_fallback_probe.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "plain_c_fallback_probe.md"
STANDARD_ARMCC_ROOTS = [
    Path("C:/Program Files (x86)/ARM_Compiler_5.06u7"),
    Path("C:/Program Files/ARM_Compiler_5.06u7"),
    Path("C:/Program Files (x86)/Arm Compiler 5.06u7"),
    Path("C:/Program Files/Arm Compiler 5.06u7"),
    Path("C:/Keil_v5/ARM/ARMCC"),
    Path("C:/Keil/ARM/ARMCC"),
    *( [Path(os.environ["LOCALAPPDATA"]) / "Keil_v5/ARM/ARMCC"] if os.environ.get("LOCALAPPDATA") else [] ),
]

FUNCTION_RE = re.compile(
    r"(?P<header>OOT3D_NAKED\s+"
    r"(?P<return_type>[A-Za-z_][A-Za-z0-9_\s\*]*?)\s+"
    r"(?P<name>oot3d_[A-Za-z0-9_]+)\s*\([^;{}]*\)\s*)\{",
    re.MULTILINE,
)


@dataclass
class Candidate:
    source: Path
    name: str
    header: str
    fallback_body: str
    start: int
    end: int


@dataclass
class CompilerConfig:
    name: str
    cc: str
    objdump: str
    flags: list[str]


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def find_tool(name: str, tool_prefix: str) -> str:
    executable = f"{tool_prefix}-{name}"
    found = shutil.which(executable)
    if found:
        return found

    devkit_arm = os.environ.get("DEVKITARM")
    if devkit_arm:
        candidate = Path(devkit_arm) / "bin" / f"{executable}.exe"
        if candidate.is_file():
            return str(candidate)

    candidates = [
        Path("C:/devkitPro/devkitARM/bin") / f"{executable}.exe",
        Path("C:/msys64/mingw64/bin") / f"{executable}.exe",
        Path("C:/msys64/ucrt64/bin") / f"{executable}.exe",
        Path("C:/msys64/clang64/bin") / f"{executable}.exe",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)

    raise FileNotFoundError(f"could not find {executable}")


def executable_names(name: str) -> list[str]:
    if name.lower().endswith(".exe"):
        return [name]
    return [name, f"{name}.exe"]


def find_unprefixed_tool(name: str, roots: list[Path] | None = None) -> str:
    roots = roots or []
    for root in roots:
        for base in (root, root / "bin"):
            for executable in executable_names(name):
                candidate = base / executable
                if candidate.is_file():
                    return str(candidate)
    for executable in executable_names(name):
        found = shutil.which(executable)
        if found:
            return found
    raise FileNotFoundError(f"could not find {name}")


def find_armcc_tool(name: str, armcc_path: str) -> str:
    roots = []
    for value in (armcc_path, os.environ.get("ARMCC_PATH", "")):
        if value:
            roots.append(Path(value))
    roots.extend(STANDARD_ARMCC_ROOTS)
    return find_unprefixed_tool(name, roots)


def armcc_license_ready(armcc: str) -> bool:
    completed = subprocess.run([armcc, "--vsn"], cwd=ROOT, text=True, capture_output=True)
    return completed.returncode == 0


def armcc_target_ready(armcc: str) -> bool:
    completed = subprocess.run([armcc, "--cpu=list"], cwd=ROOT, text=True, capture_output=True)
    return completed.returncode == 0 and "MPCore" in (completed.stdout + completed.stderr)


def add_tool_dirs_to_path(*tools: str) -> None:
    existing = os.environ.get("Path") or os.environ.get("PATH") or ""
    parts = [part for part in existing.split(os.pathsep) if part]
    lowered = {part.lower() for part in parts}
    for tool in tools:
        directory = str(Path(tool).parent)
        if directory and directory.lower() not in lowered:
            parts.insert(0, directory)
            lowered.add(directory.lower())
    os.environ["Path"] = os.pathsep.join(parts)
    os.environ["PATH"] = os.environ["Path"]


def gcc_compile_flags(optimization: str) -> list[str]:
    return [
        "-std=gnu99",
        optimization,
        "-g",
        "-Wall",
        "-ffreestanding",
        "-fno-builtin",
        "-fno-common",
        "-mword-relocations",
        "-ffunction-sections",
        "-fdata-sections",
        "-D__3DS__",
        "-I",
        str(ROOT / "include"),
        "-march=armv6k",
        "-mtune=mpcore",
        "-mfpu=vfp",
        "-mfloat-abi=hard",
        "-mtp=soft",
    ]


def armcc_compile_flags(optimization: str) -> list[str]:
    flags = [
        "--apcs=//interwork",
        "--cpu=MPCore",
        "--fpmode=fast",
        "--c99",
        "--arm",
        "--signed_chars",
        "--multibyte-chars",
        "--locale=japanese",
        "--data-reorder",
        "--split_sections",
        "-D__3DS__",
        "-I",
        str(ROOT / "include"),
    ]
    if optimization:
        flags.extend(optimization.split())
    return flags


def configure_compiler(args: argparse.Namespace) -> CompilerConfig:
    objdump = find_tool("objdump", args.tool_prefix)
    requested = args.compiler
    if requested in {"auto", "armcc"}:
        try:
            armcc = find_armcc_tool("armcc", args.armcc_path)
        except FileNotFoundError:
            if requested == "armcc":
                raise SystemExit(
                    "ARMCC not found. Install a licensed Arm Compiler 5/RVCT and set "
                    "ARMCC_PATH to the directory containing bin/armcc.exe, or pass --armcc-path."
                ) from None
        else:
            ready = armcc_license_ready(armcc) and armcc_target_ready(armcc)
            if requested == "armcc" and not ready:
                raise SystemExit(f"ARMCC is installed but not OOT3D-ready: {armcc} does not support --cpu=MPCore.")
            if ready:
                add_tool_dirs_to_path(armcc, objdump)
                return CompilerConfig("armcc", armcc, objdump, armcc_compile_flags(args.armcc_optimization))

    gcc = find_tool("gcc", args.tool_prefix)
    add_tool_dirs_to_path(gcc, objdump)
    return CompilerConfig("gcc", gcc, objdump, gcc_compile_flags(args.optimization))


def find_matching_brace(text: str, open_index: int) -> int:
    depth = 0
    state = "code"
    i = open_index
    while i < len(text):
        char = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if state == "code":
            if char == "/" and nxt == "/":
                state = "line-comment"
                i += 2
                continue
            if char == "/" and nxt == "*":
                state = "block-comment"
                i += 2
                continue
            if char == '"':
                state = "string"
            elif char == "'":
                state = "char"
            elif char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    return i
        elif state == "line-comment":
            if char == "\n":
                state = "code"
        elif state == "block-comment":
            if char == "*" and nxt == "/":
                state = "code"
                i += 2
                continue
        elif state in {"string", "char"}:
            if char == "\\":
                i += 2
                continue
            if state == "string" and char == '"':
                state = "code"
            elif state == "char" and char == "'":
                state = "code"
        i += 1
    raise ValueError(f"unmatched brace at {open_index}")


def source_preamble(text: str) -> str:
    match = re.search(r"(?m)^(?://\s+Ghidra:|OOT3D_NAKED\s+)", text)
    if not match:
        return text
    return text[: match.start()].rstrip() + "\n\n"


def fallback_body(body: str) -> str | None:
    lines = body.splitlines(keepends=True)
    start = else_index = end = None
    depth = 0
    for index, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith("#if"):
            if start is None and re.fullmatch(r"#if\s+defined\(__arm__\)", stripped):
                start = index
                depth = 1
                continue
            if start is not None:
                depth += 1
                continue
        if start is None:
            continue
        if stripped.startswith("#else") and depth == 1:
            else_index = index
            continue
        if stripped.startswith("#endif"):
            depth -= 1
            if depth == 0:
                end = index
                break
    if start is None or else_index is None or end is None:
        return None
    return "".join(lines[else_index + 1 : end]).strip("\n") + "\n"


def discover_candidates(source: Path) -> list[Candidate]:
    text = source.read_text(encoding="utf-8", errors="replace")
    candidates = []
    for match in FUNCTION_RE.finditer(text):
        open_index = text.find("{", match.start())
        close_index = find_matching_brace(text, open_index)
        body = text[open_index + 1 : close_index]
        fallback = fallback_body(body)
        if fallback is None:
            continue
        header = match.group("header").replace("OOT3D_NAKED ", "", 1).rstrip()
        candidates.append(
            Candidate(
                source=source,
                name=match.group("name"),
                header=header,
                fallback_body=fallback,
                start=match.start(),
                end=close_index + 1,
            )
        )
    return candidates


def safe_stem(*parts: str) -> str:
    text = "__".join(parts)
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", text).strip("_")


def write_probe_source(candidate: Candidate, build_dir: Path) -> Path:
    text = candidate.source.read_text(encoding="utf-8", errors="replace")
    source_dir = build_dir / "sources"
    source_dir.mkdir(parents=True, exist_ok=True)
    out = source_dir / f"{safe_stem(candidate.source.stem, candidate.name)}.c"
    body = "\n".join("    " + line if line.strip() else line for line in candidate.fallback_body.splitlines())
    content = (
        source_preamble(text)
        + "/* Plain-C fallback probe generated by scripts/probe_plain_c_fallbacks.py. */\n"
        + f"{candidate.header} {{\n"
        + body.rstrip()
        + "\n}\n"
    )
    out.write_text(content, encoding="utf-8")
    return out


def first_difference_text(row: dict[str, Any]) -> str:
    diff = row.get("first_difference")
    if not isinstance(diff, dict):
        return ""
    return f"{diff.get('index')}: {diff.get('target')} vs {diff.get('compiled')}"


def run_text(row: dict[str, Any]) -> str:
    run = row.get("longest_common_run")
    if not isinstance(run, dict):
        return ""
    return f"{run.get('length')}@{run.get('target_index')}/{run.get('compiled_index')}"


def classify(row: dict[str, Any] | None, compiled: bool) -> str:
    if not compiled:
        return "compile-blocked"
    if not row:
        return "compare-blocked"
    if row.get("exact_match"):
        return "promote-now"
    target_count = int(row.get("target_instruction_count", 0) or 0)
    compiled_count = int(row.get("compiled_instruction_count", 0) or 0)
    lcs_ratio = float(row.get("lcs_target_ratio", 0.0) or 0.0)
    prefix = int(row.get("matching_prefix", 0) or 0)
    suffix = int(row.get("matching_suffix", 0) or 0)
    run = row.get("longest_common_run")
    run_length = int(run.get("length", 0) or 0) if isinstance(run, dict) else 0
    if abs(target_count - compiled_count) <= 2 and (prefix >= 3 or suffix >= 2 or lcs_ratio >= 0.75):
        return "codegen-near"
    if lcs_ratio >= 0.35 or run_length >= 4:
        return "structural-near"
    if lcs_ratio >= 0.15 or run_length >= 2:
        return "semantic-started"
    return "semantic-gap"


def probe_candidate(
    candidate: Candidate,
    args: argparse.Namespace,
    compiler: CompilerConfig,
    target_functions: dict[str, dict[str, Any]],
) -> dict[str, Any]:
    probe_source = write_probe_source(candidate, args.build_dir)
    object_dir = args.build_dir / "objects"
    dump_dir = args.build_dir / "dumps"
    log_dir = args.build_dir / "logs"
    object_dir.mkdir(parents=True, exist_ok=True)
    dump_dir.mkdir(parents=True, exist_ok=True)
    log_dir.mkdir(parents=True, exist_ok=True)

    stem = probe_source.stem
    object_path = object_dir / f"{stem}.o"
    dump_path = dump_dir / f"{stem}.dump"
    stderr_path = log_dir / f"{stem}.stderr.txt"
    objdump_stderr_path = log_dir / f"{stem}.objdump.stderr.txt"

    compile_cmd = [compiler.cc, *compiler.flags, "-c", str(probe_source), "-o", str(object_path)]
    completed = subprocess.run(compile_cmd, cwd=ROOT, text=True, capture_output=True)
    stderr_path.write_text(completed.stderr, encoding="utf-8")
    compare_row: dict[str, Any] | None = None
    compare_error = ""
    compiled = completed.returncode == 0 and object_path.is_file()

    if compiled:
        objdump_completed = subprocess.run(
            [compiler.objdump, "-dr", str(object_path)],
            cwd=ROOT,
            text=True,
            capture_output=True,
        )
        objdump_stderr_path.write_text(objdump_completed.stderr, encoding="utf-8")
        if objdump_completed.returncode == 0:
            dump_path.write_text(objdump_completed.stdout, encoding="ascii", errors="ignore")
            compiled_functions = read_objdump_functions(dump_dir)
            compiled_function = compiled_functions.get(candidate.name)
            target = target_functions.get(candidate.name)
            if compiled_function and target:
                compare_row = {
                    "target_entry": target.get("entry", ""),
                    "compiled_dump": rel(Path(str(compiled_function.get("dump", "")))),
                    **compare_ops(target["ops"], compiled_function["ops"]),
                }
        else:
            compare_error = (objdump_completed.stderr.splitlines() or ["objdump failed"])[0][:160]

    row = {
        "category": classify(compare_row, compiled),
        "compiler": compiler.name,
        "source": rel(candidate.source),
        "function": candidate.name,
        "probe_source": rel(probe_source),
        "object": rel(object_path),
        "dump": rel(dump_path),
        "stderr_log": rel(stderr_path),
        "target_instruction_count": 0,
        "compiled_instruction_count": 0,
        "matching_prefix": 0,
        "matching_suffix": 0,
        "lcs_instruction_count": 0,
        "lcs_target_ratio": 0.0,
        "longest_common_run": "",
        "first_difference": "",
    }
    if compare_row:
        for key in (
            "target_entry",
            "target_instruction_count",
            "compiled_instruction_count",
            "matching_prefix",
            "matching_suffix",
            "lcs_instruction_count",
            "lcs_target_ratio",
        ):
            row[key] = compare_row.get(key, row.get(key))
        row["longest_common_run"] = run_text(compare_row)
        row["first_difference"] = first_difference_text(compare_row)
    elif not compiled:
        row["first_difference"] = (completed.stderr.splitlines() or ["compile failed"])[0][:160]
    elif compare_error:
        row["first_difference"] = compare_error
    return row


def selected_candidates(args: argparse.Namespace) -> list[Candidate]:
    wanted_functions = set(args.function)
    candidates = []
    for source in args.source:
        path = source if source.is_absolute() else ROOT / source
        for candidate in discover_candidates(path):
            if wanted_functions and candidate.name not in wanted_functions:
                continue
            candidates.append(candidate)
    if args.limit:
        candidates = candidates[: args.limit]
    return candidates


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "category",
        "compiler",
        "source",
        "function",
        "target_entry",
        "target_instruction_count",
        "compiled_instruction_count",
        "matching_prefix",
        "matching_suffix",
        "lcs_instruction_count",
        "lcs_target_ratio",
        "longest_common_run",
        "first_difference",
        "probe_source",
        "object",
        "dump",
        "stderr_log",
    ]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: row.get(key, "") for key in fieldnames})


def write_markdown(path: Path, rows: list[dict[str, Any]], summary: dict[str, Any]) -> None:
    lines = [
        "# Plain-C Fallback Probe",
        "",
        "This report compiles naked-asm helper fallback bodies as standalone plain-C candidates and compares them with the OOT3D target.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Candidates | {summary['candidates']} |",
        f"| Promote-now exact candidates | {summary['promote_now']} |",
        f"| Codegen-near candidates | {summary['codegen_near']} |",
        f"| Structural-near candidates | {summary['structural_near']} |",
        f"| Semantic-started candidates | {summary['semantic_started']} |",
        f"| Semantic-gap candidates | {summary['semantic_gap']} |",
        f"| Compile-blocked candidates | {summary['compile_blocked']} |",
        f"| Compare-blocked candidates | {summary['compare_blocked']} |",
        f"| Compiler used | {summary['compiler']} |",
        "",
        "## Queue",
        "",
        "| Category | Compiler | Source | Function | Target | Compiled | Prefix | Suffix | LCS | Ratio | Run | First difference |",
        "| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |",
    ]
    order = {
        "promote-now": 0,
        "codegen-near": 1,
        "structural-near": 2,
        "semantic-started": 3,
        "semantic-gap": 4,
        "compare-blocked": 5,
        "compile-blocked": 6,
    }
    for row in sorted(rows, key=lambda item: (order.get(item["category"], 99), item["source"], item["function"])):
        lines.append(
            f"| `{row['category']}` | `{row['compiler']}` | `{row['source']}` | `{row['function']}` | "
            f"{row['target_instruction_count']} | {row['compiled_instruction_count']} | "
            f"{row['matching_prefix']} | {row['matching_suffix']} | {row['lcs_instruction_count']} | "
            f"{float(row['lcs_target_ratio']):.4f} | {row['longest_common_run']} | `{row['first_difference']}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_json(path: Path, rows: list[dict[str, Any]], summary: dict[str, Any], args: argparse.Namespace) -> None:
    data = {
        "summary": summary,
        "sources": [rel(source if source.is_absolute() else ROOT / source) for source in args.source],
        "compiler": {
            "requested": args.compiler,
            "effective": summary["compiler"],
            "armcc_path": args.armcc_path or os.environ.get("ARMCC_PATH", ""),
            "gcc_optimization": args.optimization,
            "armcc_optimization": args.armcc_optimization,
        },
        "optimization": args.optimization,
        "rows": rows,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, action="append", default=DEFAULT_SOURCES)
    parser.add_argument("--function", action="append", default=[], help="Limit to one function name; repeatable.")
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--compiler", choices=["auto", "gcc", "armcc"], default="auto")
    parser.add_argument("--optimization", default="-O2")
    parser.add_argument("--armcc-optimization", default="-Otime")
    parser.add_argument("--armcc-path", default="")
    parser.add_argument("--tool-prefix", default="arm-none-eabi")
    parser.add_argument("--target-disassembly", type=Path, default=ROOT / "ghidra_export" / "disassembly.txt")
    parser.add_argument("--manual-symbols", type=Path, default=ROOT / "symbols" / "manual_symbols.csv")
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    args = parser.parse_args()

    if args.source == DEFAULT_SOURCES:
        args.source = list(DEFAULT_SOURCES)

    candidates = selected_candidates(args)
    compiler = configure_compiler(args)

    if args.build_dir.exists():
        shutil.rmtree(args.build_dir)
    args.build_dir.mkdir(parents=True, exist_ok=True)

    if candidates:
        aliases = read_manual_symbol_aliases(args.manual_symbols)
        target_functions = apply_target_aliases(read_target_functions(args.target_disassembly), aliases)
        rows = [probe_candidate(candidate, args, compiler, target_functions) for candidate in candidates]
    else:
        rows = []

    counts = Counter(row["category"] for row in rows)
    summary = {
        "candidates": len(rows),
        "compiler": compiler.name,
        "compiler_executable": compiler.cc,
        "objdump_executable": compiler.objdump,
        "promote_now": counts["promote-now"],
        "codegen_near": counts["codegen-near"],
        "structural_near": counts["structural-near"],
        "semantic_started": counts["semantic-started"],
        "semantic_gap": counts["semantic-gap"],
        "compile_blocked": counts["compile-blocked"],
        "compare_blocked": counts["compare-blocked"],
    }
    write_json(args.out_json, rows, summary, args)
    write_csv(args.out_csv, rows)
    write_markdown(args.out_md, rows, summary)

    print(
        "plain-C fallback probe: "
        f"compiler={summary['compiler']}, "
        f"{summary['promote_now']}/{summary['candidates']} promote-now, "
        f"{summary['codegen_near']} codegen-near, {summary['compile_blocked']} compile-blocked"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
