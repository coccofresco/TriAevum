#!/usr/bin/env python3
"""Audit local 3dsdecomp reference repositories for reusable workflow pieces."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_REFERENCE_DIR = ROOT / "build" / "reference"
DEFAULT_OUT_JSON = ROOT / "analysis" / "3dsdecomp_reference_audit.json"
DEFAULT_OUT_MD = ROOT / "analysis" / "3dsdecomp_reference_audit.md"

REPOS = {
    "RedPepper": "Super Mario 3D Land decompilation workflow and scripts",
    "RedPepper-Headers": "Super Mario 3D Land shared headers",
    "nnsdk": "3DS nnsdk decompilation",
    "sead": "3DS sead decompilation",
    "LibMessageStudio": "3DS LibMessageStudio decompilation",
    "asm-differ": "RedPepper asm-differ fork",
}

FLAG_MARKERS = (
    "--apcs=",
    "--cpu=",
    "--fpmode=",
    "--c99",
    "--cpp",
    "--arm",
    "--signed_chars",
    "--multibyte-chars",
    "--locale=",
    "--data-reorder",
    "--split_sections",
    "--forceinline",
    "--no_exceptions",
    "--no_rtti",
    "-Otime",
    "-O3",
)

ARMCC_TOOLS = ("armcc", "armasm", "armar", "armlink", "fromelf")
STANDARD_ARMCC_ROOTS = (
    ROOT.parent / "tools/rvct40_591",
    Path("C:/Program Files (x86)/ARM_Compiler_5.06u7"),
    Path("C:/Program Files/ARM_Compiler_5.06u7"),
    Path("C:/Program Files (x86)/Arm Compiler 5.06u7"),
    Path("C:/Program Files/Arm Compiler 5.06u7"),
    Path("C:/Keil_v5/ARM/ARMCC"),
    Path("C:/Keil/ARM/ARMCC"),
    *( (Path(os.environ["LOCALAPPDATA"]) / "Keil_v5/ARM/ARMCC",) if os.environ.get("LOCALAPPDATA") else () ),
)


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def git_revision(path: Path) -> str:
    if not (path / ".git").exists():
        return ""
    completed = subprocess.run(
        ["git", "-C", str(path), "rev-parse", "--short", "HEAD"],
        text=True,
        capture_output=True,
        check=False,
    )
    if completed.returncode != 0:
        return ""
    return completed.stdout.strip()


def count_files(path: Path, suffixes: tuple[str, ...]) -> int:
    if not path.exists():
        return 0
    return sum(1 for file in path.rglob("*") if file.is_file() and file.suffix.lower() in suffixes)


def read_text(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def executable_names(name: str) -> list[str]:
    if name.lower().endswith(".exe"):
        return [name]
    return [name, f"{name}.exe"]


def find_armcc_local_tool(name: str, armcc_path: str) -> str:
    roots = [Path(armcc_path)] if armcc_path else []
    roots.extend(STANDARD_ARMCC_ROOTS)
    for root in roots:
        for base in (root, root / "bin", root / "bin64", root / "win_32-pentium"):
            for executable in executable_names(name):
                candidate = base / executable
                if candidate.is_file():
                    return str(candidate)
    for executable in executable_names(name):
        found = shutil.which(executable)
        if found:
            return found
    return ""


def armcc_license_check(armcc: str) -> dict[str, Any]:
    if not armcc:
        return {"returncode": None, "ok": False, "message": "armcc missing"}
    completed = subprocess.run([armcc, "--vsn"], text=True, capture_output=True, check=False, timeout=20)
    lines = [line for line in (completed.stdout + "\n" + completed.stderr).splitlines() if line.strip()]
    first_line = (lines or [""])[0]
    if completed.returncode != 0:
        for line in lines:
            if re.search(r"(C\d+E|license|cannot obtain|failed to check out)", line, re.IGNORECASE):
                first_line = line
                break
    return {
        "returncode": completed.returncode,
        "ok": completed.returncode == 0,
        "message": first_line,
    }


def armcc_target_check(armcc: str) -> dict[str, Any]:
    if not armcc:
        return {"returncode": None, "ok": False, "message": "armcc missing"}
    completed = subprocess.run([armcc, "--cpu=list"], text=True, capture_output=True, check=False, timeout=20)
    text = completed.stdout + "\n" + completed.stderr
    if completed.returncode != 0 and ("C9555E" in text or "license" in text.lower()):
        return {
            "returncode": completed.returncode,
            "ok": False,
            "message": "target check blocked by ARMCC license checkout failure",
        }
    ok = completed.returncode == 0 and "MPCore" in text
    message = "supports --cpu=MPCore" if ok else "does not support --cpu=MPCore"
    return {
        "returncode": completed.returncode,
        "ok": ok,
        "message": message,
    }


def armcc_status() -> dict[str, Any]:
    armcc_path = os.environ.get("ARMCC_PATH", "")
    tools = {tool: find_armcc_local_tool(tool, armcc_path) for tool in ARMCC_TOOLS}
    required = ("armcc", "armasm", "armar", "armlink")
    installed = all(tools[tool] for tool in required)
    license_check = armcc_license_check(tools["armcc"])
    target_check = armcc_target_check(tools["armcc"])
    return {
        "armcc_path": armcc_path,
        "tools": tools,
        "installed": installed,
        "license_ready": bool(license_check["ok"]),
        "target_ready": bool(target_check["ok"]),
        "ready": installed and bool(license_check["ok"]) and bool(target_check["ok"]),
        "license_check": license_check,
        "target_check": target_check,
    }


def license_name(path: Path) -> str:
    for name in ("LICENSE", "LICENSE.md", "COPYING"):
        license_path = path / name
        if not license_path.is_file():
            continue
        text = read_text(license_path)[:4000].lower()
        if "gnu general public license" in text or "gpl" in text:
            return "GPL-family"
        if "unlicense" in text:
            return "Unlicense"
        if "mit license" in text:
            return "MIT"
        return "present"
    return "unknown"


def cmake_text(path: Path) -> str:
    texts = []
    for cmake_file in sorted(path.rglob("CMakeLists.txt")) + sorted(path.rglob("ARMCC.cmake")):
        texts.append(read_text(cmake_file))
    return "\n".join(texts)


def extract_flags(text: str) -> list[str]:
    flags: list[str] = []
    for marker in FLAG_MARKERS:
        if marker in text:
            flags.append(marker.rstrip("="))
    return flags


def script_files(path: Path) -> list[str]:
    roots = [path / "Tools", path]
    files = []
    for root in roots:
        if not root.is_dir():
            continue
        for file in sorted(root.glob("*.py")):
            files.append(rel(file))
    return sorted(set(files))


def scan_repo(reference_dir: Path, name: str) -> dict[str, Any]:
    path = reference_dir / name
    text = cmake_text(path)
    return {
        "name": name,
        "path": rel(path),
        "exists": path.is_dir(),
        "revision": git_revision(path),
        "license": license_name(path),
        "description": REPOS[name],
        "uses_armcc_path": "ARMCC_PATH" in text,
        "uses_armcc_cmake": (path / "ARMCC.cmake").is_file(),
        "armcc_flags": extract_flags(text),
        "python_scripts": script_files(path),
        "symbol_files": count_files(path, (".sym",)),
        "headers": count_files(path, (".h", ".hpp")),
        "source_files": count_files(path, (".c", ".cpp", ".s", ".asm")),
    }


def build_summary(rows: list[dict[str, Any]]) -> dict[str, Any]:
    status = armcc_status()
    return {
        "reference_repos": len(rows),
        "present_repos": sum(1 for row in rows if row["exists"]),
        "armcc_repos": [row["name"] for row in rows if row["uses_armcc_path"] or row["uses_armcc_cmake"]],
        "total_symbol_files": sum(int(row["symbol_files"]) for row in rows),
        "total_headers": sum(int(row["headers"]) for row in rows),
        "total_source_files": sum(int(row["source_files"]) for row in rows),
        "local_armcc": status,
    }


def write_json(path: Path, summary: dict[str, Any], rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({"summary": summary, "rows": rows}, indent=2) + "\n", encoding="utf-8")


def write_markdown(path: Path, summary: dict[str, Any], rows: list[dict[str, Any]]) -> None:
    lines = [
        "# 3dsdecomp Reference Audit",
        "",
        "Audit of locally cloned 3dsdecomp repositories under `build/reference`.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Present repos | {summary['present_repos']} / {summary['reference_repos']} |",
        f"| Repos using ARMCC/ARMCC_PATH | {len(summary['armcc_repos'])} |",
        f"| Symbol files | {summary['total_symbol_files']} |",
        f"| Headers | {summary['total_headers']} |",
        f"| Source files | {summary['total_source_files']} |",
        f"| Local ARMCC installed | {'yes' if summary['local_armcc']['installed'] else 'no'} |",
        f"| Local ARMCC license ready | {'yes' if summary['local_armcc']['license_ready'] else 'no'} |",
        f"| Local ARMCC target ready | {'yes' if summary['local_armcc']['target_ready'] else 'no'} |",
        f"| Local ARMCC ready | {'yes' if summary['local_armcc']['ready'] else 'no'} |",
        "",
        "## Repositories",
        "",
        "| Repo | Rev | License | ARMCC | Scripts | Symbols | Headers | Sources | Reuse value |",
        "| --- | --- | --- | --- | ---: | ---: | ---: | ---: | --- |",
    ]
    for row in rows:
        scripts = len(row["python_scripts"])
        armcc = "yes" if row["uses_armcc_path"] or row["uses_armcc_cmake"] else "no"
        lines.append(
            f"| `{row['name']}` | `{row['revision']}` | `{row['license']}` | {armcc} | "
            f"{scripts} | {row['symbol_files']} | {row['headers']} | {row['source_files']} | "
            f"{row['description']} |"
        )

    lines.extend(
        [
            "",
            "## Reusable Pieces",
            "",
            "- ARMCC/RVCT lane: RedPepper, nnsdk, sead, and LibMessageStudio all use `ARMCC_PATH`, `armcc.exe`, `armar.exe`, `armlink.exe`, and MPCore ARM flags.",
            "- Compiler flags worth testing here: `--apcs=//interwork`, `--cpu=MPCore`, `--fpmode=fast`, `--c99`, `--arm`, `--signed_chars`, `--multibyte-chars`, `--locale=japanese`, `-Otime`, `--data-reorder`, `--split_sections`.",
            "- RedPepper's `Symbols`/`DataSymbols` plus `check.py`, `diff.py`, `progress.py`, and `genLinkerScript.py` confirm that rank catalogs are a productive batch workflow.",
            "- `nnsdk`, `sead`, and `LibMessageStudio` are more useful as ABI/header references than as direct OOT3D game-code ports.",
            "- License boundary: do not copy GPL or unknown-license code into this repo without an explicit decision; use the workflow patterns, generated metrics, and external references instead.",
            "",
            "## Local ARMCC Status",
            "",
            "| Tool | Path |",
            "| --- | --- |",
        ]
    )
    for tool, tool_path in summary["local_armcc"]["tools"].items():
        lines.append(f"| `{tool}` | `{tool_path or 'missing'}` |")
    lines.extend(
        [
            "",
            f"License check: `{summary['local_armcc']['license_check']['message'] or 'ok'}`",
            f"Target check: `{summary['local_armcc']['target_check']['message'] or 'unknown'}`",
            "",
            "Official acquisition route: use an ARMCC/RVCT package with a valid license/evaluation entitlement. Do not use unofficial mirrors for `armcc.exe`.",
            "",
            "Relevant upstream URLs: `https://github.com/3dsdecomp`, `https://developer.arm.com/downloads/view/ACOMP5`, `https://www.keil.com/download/product/`, `https://www.keil.com/support/man/docs/armcc/`.",
            "",
            "## Local Actions",
            "",
            "- `scripts/probe_plain_c_fallbacks.py` now has `--compiler auto|gcc|armcc` and ARMCC flag support.",
            "- `scripts/export_symbol_rank_catalog.py` exports a RedPepper-style `O/m/M/U` function catalog from the current OOT3D artifacts.",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-dir", type=Path, default=DEFAULT_REFERENCE_DIR)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    args = parser.parse_args()

    rows = [scan_repo(args.reference_dir, name) for name in REPOS]
    summary = build_summary(rows)
    write_json(args.out_json, summary, rows)
    write_markdown(args.out_md, summary, rows)

    print(
        "3dsdecomp reference audit: "
        f"{summary['present_repos']}/{summary['reference_repos']} repos, "
        f"{len(summary['armcc_repos'])} ARMCC-based, "
        f"{summary['total_symbol_files']} symbol files"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
