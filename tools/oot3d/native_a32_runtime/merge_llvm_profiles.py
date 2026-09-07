#!/usr/bin/env python3
"""Merge raw LLVM profiles for a reproducible whole-AOT PGO build."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path


FORMAT = "oot3d_llvm_profile_merge_v1"


def merge_profiles(
    llvm_root: Path, inputs: list[Path], output: Path
) -> dict[str, object]:
    profiles = sorted({path.resolve() for path in inputs if path.is_file()})
    if not profiles:
        raise ValueError("no LLVM raw profiles were found")
    llvm_profdata = llvm_root.resolve() / "bin" / "llvm-profdata.exe"
    if not llvm_profdata.is_file():
        raise FileNotFoundError(f"llvm-profdata was not found: {llvm_profdata}")
    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [str(llvm_profdata), "merge", "-sparse", *map(str, profiles),
         "-o", str(output)],
        check=True,
    )
    return {
        "format": FORMAT,
        "llvm_profdata": str(llvm_profdata),
        "profiles": [str(path) for path in profiles],
        "profile_count": len(profiles),
        "output": str(output),
        "output_bytes": output.stat().st_size,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--llvm-root", type=Path, required=True)
    parser.add_argument("--input", type=Path, action="append", default=[])
    parser.add_argument("--input-directory", type=Path, action="append", default=[])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    inputs = list(args.input)
    for directory in args.input_directory:
        inputs.extend(directory.glob("*.profraw"))
    report = merge_profiles(args.llvm_root, inputs, args.output)
    encoded = json.dumps(report, indent=2, sort_keys=True)
    if args.report is not None:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(encoded + "\n", encoding="utf-8")
    print(encoded)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
