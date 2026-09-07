#!/usr/bin/env python3
"""Promote every currently convertible C file into the matched source lane.

The input is `analysis/convertible_ports.csv`.  Only rows classified as
`ready-to-promote` are promoted by default.  Inline-asm exact rows can be
included explicitly as an interim lane with `--include-inline-asm`.
"""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_AUDIT = ROOT / "analysis" / "convertible_ports.csv"
DEFAULT_MATCHED = ROOT / "metadata" / "matched_sources.txt"
DEFAULT_BUILD = ROOT / "scripts" / "build-matched-objects.ps1"
DEFAULT_COMPARE = ROOT / "build" / "matched" / "compare_matched_objects.json"


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def read_json(path: Path, default: Any) -> Any:
    if not path.is_file():
        return default
    return json.loads(path.read_text(encoding="utf-8"))


def read_source_lines(path: Path) -> list[str]:
    if not path.is_file():
        return []
    return path.read_text(encoding="utf-8", errors="replace").splitlines()


def normalized_source_set(lines: list[str]) -> set[str]:
    sources: set[str] = set()
    for raw in lines:
        line = raw.strip()
        if line and not line.startswith("#"):
            sources.add(line.replace("\\", "/"))
    return sources


def candidate_rows(rows: list[dict[str, str]], include_inline_asm: bool) -> list[dict[str, str]]:
    statuses = {"ready-to-promote"}
    if include_inline_asm:
        statuses.add("ready-to-promote-inline-asm")
    return [row for row in rows if row.get("status") in statuses]


def append_sources(original_lines: list[str], sources: list[str]) -> list[str]:
    existing = normalized_source_set(original_lines)
    additions = [source for source in sources if source not in existing]
    if not additions:
        return original_lines

    lines = list(original_lines)
    if lines and lines[-1].strip():
        lines.append("")
    lines.append("# Auto-promoted by scripts/promote_convertible_c_batch.py")
    lines.extend(additions)
    return lines


def write_source_lines(path: Path, lines: list[str]) -> None:
    path.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")


def run_build(args: argparse.Namespace) -> None:
    command = [
        "powershell",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        str(args.build_script),
        "-OutDir",
        args.build_out_dir,
    ]
    completed = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
    if completed.returncode != 0:
        tail = "\n".join((completed.stdout + "\n" + completed.stderr).splitlines()[-40:])
        raise RuntimeError(f"matched build failed with exit code {completed.returncode}\n{tail}")


def verify_exact(candidate_names: set[str], compare_path: Path) -> tuple[bool, list[str]]:
    rows = read_json(compare_path, [])
    if not isinstance(rows, list):
        return False, sorted(candidate_names)
    exact_by_name = {str(row.get("name", "")): bool(row.get("exact_match")) for row in rows}
    failed = sorted(name for name in candidate_names if not exact_by_name.get(name, False))
    return not failed, failed


def promote(args: argparse.Namespace) -> dict[str, Any]:
    rows = candidate_rows(read_csv(args.audit_csv), args.include_inline_asm)
    files = sorted({row.get("port_file", "").replace("\\", "/") for row in rows if row.get("port_file")})
    names = {row.get("oot3d_name", "") for row in rows if row.get("oot3d_name")}
    original_lines = read_source_lines(args.matched_sources)
    existing = normalized_source_set(original_lines)
    additions = [source for source in files if source not in existing]

    result: dict[str, Any] = {
        "candidate_functions": len(rows),
        "candidate_files": len(files),
        "already_present_files": len(files) - len(additions),
        "promoted_files": len(additions),
        "files": files,
        "promoted": additions,
        "dry_run": bool(args.dry_run),
        "verified": False,
        "failed_functions": [],
    }

    if not additions:
        result["verified"] = True
        return result

    updated_lines = append_sources(original_lines, additions)
    if args.dry_run:
        return result

    write_source_lines(args.matched_sources, updated_lines)
    try:
        if not args.skip_build:
            run_build(args)
            ok, failed = verify_exact(names, args.compare_json)
            result["verified"] = ok
            result["failed_functions"] = failed
            if not ok:
                raise RuntimeError(f"promoted functions failed exact compare: {', '.join(failed)}")
        else:
            result["verified"] = True
    except Exception:
        write_source_lines(args.matched_sources, original_lines)
        raise

    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--audit-csv", type=Path, default=DEFAULT_AUDIT)
    parser.add_argument("--matched-sources", type=Path, default=DEFAULT_MATCHED)
    parser.add_argument("--build-script", type=Path, default=DEFAULT_BUILD)
    parser.add_argument("--build-out-dir", default="build/matched")
    parser.add_argument("--compare-json", type=Path, default=DEFAULT_COMPARE)
    parser.add_argument("--include-inline-asm", action="store_true")
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    result = promote(args)
    print(
        "convertible C batch promotion: "
        f"{result['candidate_functions']} candidate functions, "
        f"{result['candidate_files']} candidate files, "
        f"{result['promoted_files']} promoted files, "
        f"verified={str(result['verified']).lower()}"
    )
    if result["promoted"]:
        print("promoted: " + ", ".join(result["promoted"]))
    else:
        print("promoted: none")
    if result["failed_functions"]:
        print("failed: " + ", ".join(result["failed_functions"]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
