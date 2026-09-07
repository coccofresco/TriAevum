#!/usr/bin/env python3
"""Audit gamestabled/oot3d_before_public as an OOT3D reference source."""

from __future__ import annotations

import argparse
import csv
import json
import os
import re
import subprocess
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_REFERENCE = ROOT.parent / "external" / "oot3d_before_public"
DEFAULT_RVCT = ROOT.parent / "tools" / "rvct40_591"
DEFAULT_OUT_JSON = ROOT / "analysis" / "oot3d_before_public_audit.json"
DEFAULT_OUT_MD = ROOT / "analysis" / "oot3d_before_public_audit.md"

GLOBAL_ASM_RE = re.compile(r'GLOBAL_ASM\("([^"]+)"\)')
ACTOR_INIT_RE = re.compile(r"\bActorInit\s+([A-Za-z_][A-Za-z0-9_]*)")
FUNC_NAME_RE = re.compile(r"([A-Za-z_~][A-Za-z0-9_:~]*)\s*\(")


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def read_text(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def git_revision(path: Path) -> str:
    if not (path / ".git").exists():
        return ""
    completed = subprocess.run(
        ["git", "-C", str(path), "rev-parse", "--short", "HEAD"],
        text=True,
        capture_output=True,
        check=False,
    )
    return completed.stdout.strip() if completed.returncode == 0 else ""


def count_files(path: Path, suffixes: tuple[str, ...]) -> int:
    if not path.exists():
        return 0
    return sum(1 for file in path.rglob("*") if file.is_file() and file.suffix.lower() in suffixes)


def nonblank_lines(files: list[Path]) -> int:
    total = 0
    for file in files:
        for line in read_text(file).splitlines():
            if line.strip():
                total += 1
    return total


def license_name(reference: Path) -> str:
    for name in ("LICENSE", "LICENSE.md", "LICENSE.txt", "COPYING"):
        text = read_text(reference / name).lower()
        if not text:
            continue
        if "cc0 1.0 universal" in text:
            return "CC0-1.0"
        if "mit license" in text:
            return "MIT"
        return "present"
    return "unknown"


def find_tool(root: Path, name: str) -> str:
    names = [name] if name.lower().endswith(".exe") else [name, f"{name}.exe"]
    for base in (root, root / "bin", root / "bin64", root / "win_32-pentium"):
        for executable in names:
            candidate = base / executable
            if candidate.is_file():
                return str(candidate)
    return ""


def run_tool(executable: str, args: list[str]) -> dict[str, Any]:
    if not executable:
        return {"returncode": None, "stdout": "", "stderr": "", "lines": [], "first_line": "missing"}
    completed = subprocess.run([executable, *args], text=True, capture_output=True, check=False, timeout=20)
    lines = [line for line in (completed.stdout + "\n" + completed.stderr).splitlines() if line.strip()]
    return {
        "returncode": completed.returncode,
        "stdout": completed.stdout,
        "stderr": completed.stderr,
        "lines": lines,
        "first_line": lines[0] if lines else "",
    }


def diagnostic_line(lines: list[str], returncode: int | None) -> str:
    if returncode:
        for line in lines:
            if re.search(r"(C\d+E|license|cannot obtain|failed to check out)", line, re.IGNORECASE):
                return line
    return lines[0] if lines else ""


def function_definitions(text: str) -> list[str]:
    names: list[str] = []
    lines = text.splitlines()
    skip_prefixes = ("#", "//", "typedef ", "using ", "return ", "if ", "for ", "while ", "switch ")
    for index, line in enumerate(lines):
        stripped = line.strip()
        if not stripped or stripped.startswith(skip_prefixes):
            continue
        if "(" not in stripped or stripped.endswith(";"):
            continue
        search_window = stripped
        lookahead = index + 1
        while "{" not in search_window and ";" not in search_window and lookahead < min(index + 8, len(lines)):
            next_line = lines[lookahead].strip()
            search_window += " " + next_line
            lookahead += 1
        if "{" not in search_window or ";" in search_window.split("{", 1)[0]:
            continue
        before_paren = search_window.split("(", 1)[0]
        match = FUNC_NAME_RE.search(before_paren + "(")
        if not match:
            continue
        name = match.group(1)
        if name in {"if", "for", "while", "switch"}:
            continue
        names.append(name)
    return names


def rvct_status(rvct_root: Path) -> dict[str, Any]:
    env_root = os.environ.get("ARMCC_PATH")
    root = Path(env_root) if env_root else rvct_root
    tools = {tool: find_tool(root, tool) for tool in ("armcc", "armasm", "armar", "armlink", "fromelf")}
    version = run_tool(tools["armcc"], ["--vsn"])
    cpus = run_tool(tools["armcc"], ["--cpu=list"])
    cpu_text = "\n".join(cpus["lines"])
    license_message = diagnostic_line(version["lines"], version["returncode"])
    return {
        "root": str(root),
        "tools": tools,
        "installed": all(tools[tool] for tool in ("armcc", "armasm", "armar", "armlink")),
        "version": version["first_line"],
        "license_ready": version["returncode"] == 0,
        "license_message": license_message or "ok",
        "target_ready": cpus["returncode"] == 0 and "MPCore" in cpu_text,
        "target_message": "supports --cpu=MPCore"
        if cpus["returncode"] == 0 and "MPCore" in cpu_text
        else (cpus["first_line"] or "unknown"),
    }


def source_files(reference: Path) -> list[Path]:
    roots = [reference / "src", reference / "include", reference / "library", reference / "tools"]
    suffixes = {".c", ".cpp", ".h", ".hpp", ".s", ".asm", ".py"}
    files: list[Path] = []
    for root in roots:
        if root.exists():
            files.extend(file for file in root.rglob("*") if file.is_file() and file.suffix.lower() in suffixes)
    return sorted(files)


def keep_symbols(reference: Path) -> list[str]:
    symbols: list[str] = []
    for line in read_text(reference / "data" / "keep_symbols.txt").splitlines():
        stripped = line.strip()
        if stripped.startswith("--keep="):
            symbols.append(stripped.split("=", 1)[1])
    return symbols


def manual_symbols() -> dict[str, set[str]]:
    path = ROOT / "symbols" / "manual_symbols.csv"
    old_names: set[str] = set()
    new_names: set[str] = set()
    if not path.is_file():
        return {"old_names": old_names, "new_names": new_names}
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            old = (row.get("old_name") or "").strip()
            new = (row.get("new_name") or "").strip()
            if old:
                old_names.add(old)
            if new:
                new_names.add(new)
    return {"old_names": old_names, "new_names": new_names}


def cmake_flags(reference: Path) -> dict[str, Any]:
    cmake = read_text(reference / "CMakeLists.txt")
    match = re.search(r'SET\(CXX_FLAGS\s+"([^"]+)"\)', cmake)
    cxx_flags = match.group(1).split() if match else []
    markers = [
        "--apcs=//interwork",
        "--cpu=MPCore",
        "--fpmode=fast",
        "--cpp",
        "--arm",
        "--signed_chars",
        "--multibyte-chars",
        "--locale=japanese",
        "--data-reorder",
        "--split_sections",
        "--forceinline",
        "-O3",
        "-Otime",
    ]
    return {
        "cxx_flags": cxx_flags,
        "matched_markers": [flag for flag in markers if flag in cxx_flags or flag in cmake],
        "uses_armcc_path": "ARMCC_PATH" in cmake,
        "uses_rvct40inc": "RVCT40INC" in cmake,
        "uses_rvct40lib": "RVCT40LIB" in cmake,
    }


def file_inventory(reference: Path) -> tuple[list[dict[str, Any]], dict[str, Any], set[str]]:
    rows: list[dict[str, Any]] = []
    all_global_asm: list[str] = []
    all_actor_inits: list[str] = []
    all_function_defs: list[str] = []
    for file in source_files(reference):
        text = read_text(file)
        global_asm = GLOBAL_ASM_RE.findall(text)
        actor_inits = ACTOR_INIT_RE.findall(text)
        function_defs = function_definitions(text) if file.suffix.lower() in (".c", ".cpp", ".h", ".hpp") else []
        all_global_asm.extend(Path(item).stem for item in global_asm)
        all_actor_inits.extend(actor_inits)
        all_function_defs.extend(function_defs)
        rows.append(
            {
                "path": rel(file),
                "suffix": file.suffix.lower(),
                "nonblank_lines": sum(1 for line in text.splitlines() if line.strip()),
                "global_asm": len(global_asm),
                "actor_init": len(actor_inits),
                "function_definitions": len(function_defs),
                "area": rel(file.parent),
            }
        )
    summary = {
        "global_asm_refs": len(all_global_asm),
        "unique_global_asm_refs": len(set(all_global_asm)),
        "actor_init_vars": len(all_actor_inits),
        "unique_actor_init_vars": len(set(all_actor_inits)),
        "function_definitions": len(all_function_defs),
        "unique_function_definitions": len(set(all_function_defs)),
        "named_global_asm_refs": sum(1 for name in set(all_global_asm) if not name.startswith("FUN_")),
    }
    return rows, summary, set(all_global_asm)


def top_rows(rows: list[dict[str, Any]]) -> dict[str, list[dict[str, Any]]]:
    concrete = [
        row
        for row in rows
        if row["suffix"] in (".c", ".cpp")
        and row["global_asm"] == 0
        and row["function_definitions"] > 0
    ]
    actor_scaffolds = [
        row
        for row in rows
        if "/src/game/actors/" in row["path"]
        and (row["global_asm"] > 0 or row["actor_init"] > 0)
    ]
    return {
        "top_concrete_files": sorted(
            concrete, key=lambda row: (row["function_definitions"], row["nonblank_lines"]), reverse=True
        )[:20],
        "top_actor_scaffold_files": sorted(
            actor_scaffolds, key=lambda row: (row["actor_init"], row["global_asm"], row["nonblank_lines"]), reverse=True
        )[:20],
    }


def build_audit(reference: Path, rvct_root: Path) -> dict[str, Any]:
    rows, inventory_summary, global_asm_names = file_inventory(reference)
    files = source_files(reference)
    cpp_files = [file for file in files if file.suffix.lower() == ".cpp"]
    header_files = [file for file in files if file.suffix.lower() in (".h", ".hpp")]
    keep = keep_symbols(reference)
    manual = manual_symbols()
    keep_set = set(keep)
    overlaps = {
        "keep_vs_manual_old": sorted(keep_set & manual["old_names"])[:50],
        "keep_vs_manual_new": sorted(keep_set & manual["new_names"])[:50],
        "global_asm_vs_manual_old": sorted(global_asm_names & manual["old_names"])[:50],
        "global_asm_vs_manual_new": sorted(global_asm_names & manual["new_names"])[:50],
    }
    overlaps["counts"] = {
        key: len(value) for key, value in overlaps.items() if isinstance(value, list)
    }

    top = top_rows(rows)
    return {
        "summary": {
            "reference": str(reference),
            "revision": git_revision(reference),
            "license": license_name(reference),
            "source_like_files": len(files),
            "cpp_files": len(cpp_files),
            "header_files": len(header_files),
            "actor_directories": sum(1 for item in (reference / "src" / "game" / "actors").iterdir() if item.is_dir())
            if (reference / "src" / "game" / "actors").is_dir()
            else 0,
            "source_nonblank_lines": sum(row["nonblank_lines"] for row in rows),
            "keep_symbols": len(keep),
            **inventory_summary,
        },
        "rvct": rvct_status(rvct_root),
        "cmake": cmake_flags(reference),
        "overlaps": overlaps,
        "rows": rows,
        **top,
    }


def write_json(path: Path, audit: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8")


def write_markdown(path: Path, audit: dict[str, Any]) -> None:
    summary = audit["summary"]
    rvct = audit["rvct"]
    cmake = audit["cmake"]
    overlaps = audit["overlaps"]
    lines = [
        "# oot3d_before_public Reference Audit",
        "",
        "Audit of `https://github.com/gamestabled/oot3d_before_public` cloned as an external reference.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Revision | `{summary['revision']}` |",
        f"| License | `{summary['license']}` |",
        f"| Source/header/script files | {summary['source_like_files']} |",
        f"| C++ files | {summary['cpp_files']} |",
        f"| Header files | {summary['header_files']} |",
        f"| Actor directories | {summary['actor_directories']} |",
        f"| Nonblank source-like lines | {summary['source_nonblank_lines']} |",
        f"| Function definitions found | {summary['function_definitions']} |",
        f"| `GLOBAL_ASM` refs | {summary['global_asm_refs']} |",
        f"| Named `GLOBAL_ASM` refs | {summary['named_global_asm_refs']} |",
        f"| Actor init vars | {summary['actor_init_vars']} |",
        f"| Keep symbols | {summary['keep_symbols']} |",
        "",
        "## RVCT Status",
        "",
        "| Metric | Value |",
        "| --- | --- |",
        f"| Root | `{rvct['root']}` |",
        f"| Version | `{rvct['version']}` |",
        f"| Installed tools | {'yes' if rvct['installed'] else 'no'} |",
        f"| License ready | {'yes' if rvct['license_ready'] else 'no'} |",
        f"| Target ready | {'yes' if rvct['target_ready'] else 'no'} |",
        f"| License message | `{rvct['license_message']}` |",
        f"| Target message | `{rvct['target_message']}` |",
        "",
        "## Build Setup Reuse",
        "",
        f"- Uses `ARMCC_PATH`: {'yes' if cmake['uses_armcc_path'] else 'no'}",
        f"- Uses `RVCT40INC`: {'yes' if cmake['uses_rvct40inc'] else 'no'}",
        f"- Uses `RVCT40LIB`: {'yes' if cmake['uses_rvct40lib'] else 'no'}",
        f"- Matched flag markers: `{', '.join(cmake['matched_markers'])}`",
        "",
        "High-value files to mine first: `ARMCC.cmake`, `CMakeLists.txt`, `tools/preproc.py`, "
        "`include/functions.hpp`, `include/z3Dactor.hpp`, `include/z3D.hpp`, `data/keep_symbols.txt`.",
        "",
        "## Concrete C/C++ Files",
        "",
        "| File | Functions | Lines |",
        "| --- | ---: | ---: |",
    ]
    for row in audit["top_concrete_files"][:15]:
        lines.append(f"| `{row['path']}` | {row['function_definitions']} | {row['nonblank_lines']} |")

    lines.extend(
        [
            "",
            "## Actor Scaffolds",
            "",
            "| File | ActorInit | GLOBAL_ASM | Lines |",
            "| --- | ---: | ---: | ---: |",
        ]
    )
    for row in audit["top_actor_scaffold_files"][:15]:
        lines.append(f"| `{row['path']}` | {row['actor_init']} | {row['global_asm']} | {row['nonblank_lines']} |")

    lines.extend(
        [
            "",
            "## Symbol Overlap With Current Repo",
            "",
            "| Overlap | Count | Examples |",
            "| --- | ---: | --- |",
        ]
    )
    for key, count in overlaps["counts"].items():
        examples = ", ".join(f"`{name}`" for name in overlaps[key][:8])
        lines.append(f"| `{key}` | {count} | {examples or '-'} |")

    lines.extend(
        [
            "",
            "## Actionable Conclusions",
            "",
            "- Treat this repo as the primary OOT3D-native reference for names, headers, actor layouts, keep-symbol ordering, and RVCT flags.",
            "- Do not assume each `.cpp` is real C++: many files are linkable scaffolds around `GLOBAL_ASM`; prioritize files with zero `GLOBAL_ASM` first.",
            "- RVCT4 build 591 has the correct `MPCore` target and tool layout, but a FLEXnet license is still required before it can compile.",
            "- Once RVCT is licensed, rerun matched build/profile search with `-Compiler armcc -ArmccPath ..\\tools\\rvct40_591` before further GCC-only tuning.",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, default=DEFAULT_REFERENCE)
    parser.add_argument("--rvct", type=Path, default=DEFAULT_RVCT)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    args = parser.parse_args()

    audit = build_audit(args.reference, args.rvct)
    write_json(args.out_json, audit)
    write_markdown(args.out_md, audit)
    summary = audit["summary"]
    print(
        "oot3d_before_public audit: "
        f"{summary['source_like_files']} files, "
        f"{summary['function_definitions']} function defs, "
        f"{summary['global_asm_refs']} GLOBAL_ASM refs"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
