#!/usr/bin/env python3
"""Probe real C++ bodies from gamestabled/oot3d_before_public.

This is a diagnostic lane: it compiles public-reference C++ files with the
selected ARM GNU C++ toolchain, compares mapped functions against the current
OOT3D target disassembly, and reports which bodies are exact or close enough to
prioritize for ARMCC/target-shaped lowering.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import shutil
import subprocess
from collections import Counter, defaultdict
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

from compare_runtime_objects import compare_ops, read_objdump_functions, read_target_functions


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_REFERENCE = ROOT.parent / "external" / "oot3d_before_public"
DEFAULT_BUILD_DIR = ROOT / "build" / "oot3d_public_cpp_probe"
DEFAULT_OUT_JSON = ROOT / "analysis" / "oot3d_public_cpp_probe.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "oot3d_public_cpp_probe.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "oot3d_public_cpp_probe.md"

LINKER_SYMBOL_RE = re.compile(r"^\s*(?P<name>\S+)\s+0x(?P<entry>[0-9a-fA-F]{8})\s*$")
FUNCTION_RE = re.compile(
    r"(?P<header>"
    r"(?:(?:static|inline|extern\s+\"C\"|const|volatile)\s+)*"
    r"(?:[A-Za-z_][A-Za-z0-9_:<>]*[\s*&]+)+"
    r"(?P<name>[A-Za-z_~][A-Za-z0-9_:~]*)"
    r"\s*\([^;{}]*\)\s*(?:const\s*)?)\{",
    re.MULTILINE | re.DOTALL,
)


@dataclass
class FunctionDef:
    source: str
    line: int
    name: str
    object_name: str
    entry: str


@dataclass
class ProbeResult:
    entry: str
    name: str
    object_name: str
    source: str
    line: int
    category: str
    target_name: str = ""
    target_instruction_count: int = 0
    compiled_instruction_count: int = 0
    matching_prefix: int = 0
    matching_suffix: int = 0
    lcs_instruction_count: int = 0
    lcs_target_ratio: float = 0.0
    longest_common_run: str = ""
    first_difference: str = ""
    object: str = ""
    dump: str = ""
    stderr_log: str = ""


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def ref_rel(path: Path, reference: Path) -> str:
    try:
        return str(path.relative_to(reference)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def read_linker_symbols(path: Path) -> dict[str, str]:
    symbols: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = LINKER_SYMBOL_RE.match(line)
        if match:
            symbols[match.group("name")] = match.group("entry").lower()
    return symbols


def find_tool(name: str, tool_prefix: str, tool_root: Path | None = None) -> str:
    executable = f"{tool_prefix}-{name}"
    if tool_root:
        candidate = tool_root / "bin" / f"{executable}.exe"
        if candidate.is_file():
            return str(candidate)
        candidate = tool_root / "bin" / executable
        if candidate.is_file():
            return str(candidate)

    found = shutil.which(executable)
    if found:
        return found
    candidate = Path("C:/devkitPro/devkitARM/bin") / f"{executable}.exe"
    if candidate.is_file():
        return str(candidate)
    raise FileNotFoundError(f"could not find {executable}")


def normalize_object_name(name: str) -> str:
    return name.split("::")[-1]


def parse_function_defs(path: Path, reference: Path, linker_symbols: dict[str, str]) -> list[FunctionDef]:
    text = path.read_text(encoding="utf-8", errors="replace")
    rows: list[FunctionDef] = []
    for match in FUNCTION_RE.finditer(text):
        name = match.group("name")
        if name in {"if", "for", "while", "switch", "return", "sizeof"}:
            continue
        candidates = [name, normalize_object_name(name)]
        entry = next((linker_symbols[candidate] for candidate in candidates if candidate in linker_symbols), "")
        if not entry:
            continue
        rows.append(
            FunctionDef(
                source=ref_rel(path, reference),
                line=text[: match.start()].count("\n") + 1,
                name=name,
                object_name=normalize_object_name(name),
                entry=entry,
            )
        )
    return rows


def discover_functions(reference: Path, linker_symbols: dict[str, str]) -> list[FunctionDef]:
    functions: list[FunctionDef] = []
    for path in sorted((reference / "src").rglob("*.cpp")):
        functions.extend(parse_function_defs(path, reference, linker_symbols))
    return functions


def gcc_cxx_flags(reference: Path, optimization: str, extra_flags: list[str]) -> list[str]:
    return [
        "-x",
        "c++",
        "-std=gnu++98",
        optimization,
        "-g",
        "-Wall",
        "-fno-builtin",
        "-fno-common",
        "-fno-exceptions",
        "-fno-rtti",
        "-fno-threadsafe-statics",
        "-fno-use-cxa-atexit",
        "-mword-relocations",
        "-ffunction-sections",
        "-fdata-sections",
        "-D__3DS__",
        "-D__arm__",
        "-D__weak=__attribute__((weak))",
        "-D__sqrtf=__builtin_sqrtf",
        "-D__fabsf=__builtin_fabsf",
        "-I",
        str(reference / "include"),
        "-I",
        str(reference / "library" / "3ds_sdk" / "include"),
        "-I",
        str(reference / "src" / "game"),
        "-march=armv6k",
        "-mtune=mpcore",
        "-mfpu=vfp",
        "-mfloat-abi=hard",
        "-mtp=soft",
    ] + extra_flags


def sanitize_stem(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value)


def classify(comparison: dict[str, Any]) -> str:
    if comparison["exact_match"]:
        return "exact"
    if (
        comparison["lcs_target_ratio"] >= 0.5
        and comparison["target_instruction_count"] == comparison["compiled_instruction_count"]
    ):
        return "codegen-near"
    if comparison["lcs_target_ratio"] >= 0.3:
        return "structural-near"
    if comparison["lcs_instruction_count"] > 0:
        return "semantic-started"
    return "semantic-gap"


def format_run(run: dict[str, Any]) -> str:
    return f"{run.get('length', 0)}@{run.get('target_index', 0)}/{run.get('compiled_index', 0)}"


def format_diff(diff: dict[str, Any] | None) -> str:
    if not diff:
        return ""
    return f"{diff.get('index')}: {diff.get('target')} vs {diff.get('compiled')}"


def target_indices(target_functions: dict[str, dict[str, Any]]) -> tuple[dict[str, dict[str, Any]], dict[str, dict[str, Any]]]:
    by_name = target_functions
    by_entry = {str(function["entry"]).lower(): {"name": name, **function} for name, function in target_functions.items()}
    return by_name, by_entry


def compile_sources(
    args: argparse.Namespace,
    functions: list[FunctionDef],
    cxx: str,
    objdump: str,
) -> tuple[dict[str, dict[str, Any]], dict[str, str], dict[str, str], set[str]]:
    if args.build_dir.exists():
        shutil.rmtree(args.build_dir)
    object_dir = args.build_dir / "objects"
    dump_dir = args.build_dir / "dumps"
    log_dir = args.build_dir / "logs"
    object_dir.mkdir(parents=True)
    dump_dir.mkdir()
    log_dir.mkdir()

    sources = sorted({function.source for function in functions})
    if args.limit_files > 0:
        sources = sources[: args.limit_files]
    selected = {source for source in sources}
    flags = gcc_cxx_flags(args.reference, args.optimization, args.extra_cxx_flag)

    failed_logs: dict[str, str] = {}
    object_paths: dict[str, str] = {}
    for index, source in enumerate(sources, start=1):
        source_path = args.reference / source
        stem = sanitize_stem(Path(source).with_suffix("").as_posix())
        object_path = object_dir / f"{stem}.o"
        dump_path = dump_dir / f"{stem}.dump"
        stderr_path = log_dir / f"{stem}.stderr.txt"

        completed = subprocess.run(
            [cxx, *flags, "-c", str(source_path), "-o", str(object_path)],
            cwd=ROOT,
            text=True,
            capture_output=True,
        )
        stderr_path.write_text((completed.stdout or "") + (completed.stderr or ""), encoding="utf-8")
        if completed.returncode != 0:
            failed_logs[source] = rel(stderr_path)
            continue

        dumped = subprocess.run([objdump, "-dr", str(object_path)], cwd=ROOT, text=True, capture_output=True)
        dump_path.write_text(dumped.stdout + dumped.stderr, encoding="utf-8")
        if dumped.returncode != 0:
            failed_logs[source] = rel(dump_path)
            continue
        object_paths[source] = rel(object_path)

        if args.progress and index % args.progress == 0:
            print(f"compiled {index}/{len(sources)} public C++ files")

    return read_objdump_functions(dump_dir), failed_logs, object_paths, selected


def run_probe(args: argparse.Namespace) -> tuple[dict[str, Any], list[ProbeResult]]:
    linker_symbols = read_linker_symbols(args.reference / "data" / "oot3d.ld")
    functions = discover_functions(args.reference, linker_symbols)
    if args.limit_functions > 0:
        functions = functions[: args.limit_functions]

    cxx = find_tool("g++", args.tool_prefix, args.tool_root)
    objdump = find_tool("objdump", args.tool_prefix, args.tool_root)
    compiled_functions, failed_logs, object_paths, selected_sources = compile_sources(args, functions, cxx, objdump)
    functions = [function for function in functions if function.source in selected_sources]

    target_by_name, target_by_entry = target_indices(read_target_functions(args.target_disassembly))
    counts = Counter()
    by_source = Counter(function.source for function in functions)
    compiled_sources = set(object_paths)
    source_compile_failures = set(failed_logs)
    results: list[ProbeResult] = []

    for function in functions:
        if function.source in source_compile_failures:
            counts["compile-fail"] += 1
            results.append(
                ProbeResult(
                    entry=function.entry,
                    name=function.name,
                    object_name=function.object_name,
                    source=function.source,
                    line=function.line,
                    category="compile-fail",
                    stderr_log=failed_logs.get(function.source, ""),
                )
            )
            continue

        compiled = compiled_functions.get(function.object_name)
        target = target_by_name.get(function.object_name)
        target_name = function.object_name
        if target is None:
            target = target_by_entry.get(function.entry)
            target_name = str(target.get("name", "")) if target else ""

        if compiled is None or target is None:
            counts["compare-blocked"] += 1
            missing = []
            if compiled is None:
                missing.append("compiled symbol")
            if target is None:
                missing.append("target function")
            results.append(
                ProbeResult(
                    entry=function.entry,
                    name=function.name,
                    object_name=function.object_name,
                    source=function.source,
                    line=function.line,
                    category="compare-blocked",
                    target_name=target_name,
                    first_difference="missing " + " and ".join(missing),
                    object=object_paths.get(function.source, ""),
                )
            )
            continue

        comparison = compare_ops(target["ops"], compiled["ops"])
        category = classify(comparison)
        counts[category] += 1
        results.append(
            ProbeResult(
                entry=function.entry,
                name=function.name,
                object_name=function.object_name,
                source=function.source,
                line=function.line,
                category=category,
                target_name=target_name,
                target_instruction_count=int(comparison["target_instruction_count"]),
                compiled_instruction_count=int(comparison["compiled_instruction_count"]),
                matching_prefix=int(comparison["matching_prefix"]),
                matching_suffix=int(comparison["matching_suffix"]),
                lcs_instruction_count=int(comparison["lcs_instruction_count"]),
                lcs_target_ratio=float(comparison["lcs_target_ratio"]),
                longest_common_run=format_run(comparison["longest_common_run"]),
                first_difference=format_diff(comparison["first_difference"]),
                object=object_paths.get(function.source, ""),
                dump=compiled.get("dump", ""),
            )
        )

    summary = {
        "reference": rel(args.reference),
        "compiler": cxx,
        "objdump": objdump,
        "optimization": args.optimization,
        "extra_cxx_flags": args.extra_cxx_flag,
        "tool_root": rel(args.tool_root) if args.tool_root else "",
        "linker_symbols": len(linker_symbols),
        "mapped_function_bodies": len(functions),
        "mapped_source_files": len(by_source),
        "compiled_source_files": len(compiled_sources),
        "compile_failed_source_files": len(source_compile_failures),
        "compiled_symbols": len(compiled_functions),
        "status_counts": dict(sorted(counts.items())),
    }
    return summary, results


def write_json(path: Path, summary: dict[str, Any], results: list[ProbeResult]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps({"summary": summary, "results": [asdict(result) for result in results]}, indent=2) + "\n",
        encoding="utf-8",
    )


def write_csv(path: Path, results: list[ProbeResult]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = list(ProbeResult.__dataclass_fields__)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for result in results:
            writer.writerow(asdict(result))


def write_markdown(path: Path, summary: dict[str, Any], results: list[ProbeResult]) -> None:
    counts = Counter(result.category for result in results)
    ranked = sorted(
        results,
        key=lambda row: (
            row.category != "exact",
            row.category != "codegen-near",
            row.category != "structural-near",
            -row.lcs_instruction_count,
            -row.target_instruction_count,
            row.source,
            row.line,
        ),
    )
    by_file: dict[str, Counter[str]] = defaultdict(Counter)
    for result in results:
        by_file[result.source][result.category] += 1

    lines = [
        "# oot3d_before_public C++ Probe",
        "",
        "Generated by compiling real C++ bodies from `external/oot3d_before_public` with an ARM GNU C++ toolchain and comparing mapped functions against the current OOT3D target disassembly.",
        "",
        "This is a probe lane: it identifies public-reference bodies worth promoting into maintained sources, but it does not add them to the matched baseline by itself.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Linker symbols | {summary['linker_symbols']} |",
        f"| Mapped function bodies | {summary['mapped_function_bodies']} |",
        f"| Mapped source files | {summary['mapped_source_files']} |",
        f"| Compiled source files | {summary['compiled_source_files']} |",
        f"| Compile-failed source files | {summary['compile_failed_source_files']} |",
        f"| Compiled symbols | {summary['compiled_symbols']} |",
        f"| Optimization | `{summary['optimization']}` |",
        f"| Extra C++ flags | `{summary['extra_cxx_flags']}` |",
        f"| Exact functions | {counts['exact']} |",
        f"| Codegen-near functions | {counts['codegen-near']} |",
        f"| Structural-near functions | {counts['structural-near']} |",
        f"| Semantic-started functions | {counts['semantic-started']} |",
        f"| Semantic-gap functions | {counts['semantic-gap']} |",
        f"| Compare-blocked functions | {counts['compare-blocked']} |",
        f"| Compile-failed functions | {counts['compile-fail']} |",
        "",
        "## Best Rows",
        "",
        "| Category | Entry | Name | Target | Compiled | LCS | Run | Source | First difference |",
        "| --- | --- | --- | ---: | ---: | ---: | --- | --- | --- |",
    ]
    for result in ranked[:80]:
        lines.append(
            f"| `{result.category}` | `{result.entry}` | `{result.object_name}` | "
            f"{result.target_instruction_count} | {result.compiled_instruction_count} | "
            f"{result.lcs_instruction_count} | `{result.longest_common_run}` | "
            f"`{result.source}:{result.line}` | `{result.first_difference}` |"
        )

    lines.extend(
        [
            "",
            "## File Summary",
            "",
            "| File | Exact | Codegen-near | Structural-near | Semantic-started | Semantic-gap | Blocked | Failed |",
            "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
        ]
    )
    for source, file_counts in sorted(
        by_file.items(),
        key=lambda item: (
            -item[1]["exact"],
            -item[1]["codegen-near"],
            -item[1]["structural-near"],
            item[0],
        ),
    ):
        lines.append(
            f"| `{source}` | {file_counts['exact']} | {file_counts['codegen-near']} | "
            f"{file_counts['structural-near']} | {file_counts['semantic-started']} | "
            f"{file_counts['semantic-gap']} | {file_counts['compare-blocked']} | "
            f"{file_counts['compile-fail']} |"
        )

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, default=DEFAULT_REFERENCE)
    parser.add_argument("--target-disassembly", type=Path, default=ROOT / "ghidra_export" / "disassembly.txt")
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--tool-prefix", default="arm-none-eabi")
    parser.add_argument("--tool-root", type=Path)
    parser.add_argument("--optimization", default="-O2")
    parser.add_argument("--extra-cxx-flag", action="append", default=[])
    parser.add_argument("--limit-files", type=int, default=0)
    parser.add_argument("--limit-functions", type=int, default=0)
    parser.add_argument("--progress", type=int, default=25)
    args = parser.parse_args()

    args.reference = args.reference.resolve()
    args.target_disassembly = args.target_disassembly.resolve()
    args.build_dir = args.build_dir.resolve()
    args.out_json = args.out_json.resolve()
    args.out_csv = args.out_csv.resolve()
    args.out_md = args.out_md.resolve()
    if args.tool_root:
        args.tool_root = args.tool_root.resolve()

    summary, results = run_probe(args)
    write_json(args.out_json, summary, results)
    write_csv(args.out_csv, results)
    write_markdown(args.out_md, summary, results)

    counts = Counter(result.category for result in results)
    print(
        "oot3d_before_public C++ probe: "
        f"{summary['mapped_function_bodies']} mapped bodies, "
        f"{summary['compiled_source_files']}/{summary['mapped_source_files']} files compiled, "
        f"{counts['exact']} exact, {counts['codegen-near']} codegen-near, "
        f"{counts['structural-near']} structural-near"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
