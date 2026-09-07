"""Generate the selected whole-function C++ AOT shards for OOT3D."""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Sequence

from generate_aot import operational_path
from generate_whole_aot import main as generate_program
from whole_aot_cpp import generate


ROOT = Path(__file__).resolve().parent
REPO_ROOT = ROOT.parents[2]


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--program",
        type=Path,
        default=REPO_ROOT / "build-codex/oot3d_whole_aot/aot_program.json",
    )
    parser.add_argument(
        "--selection",
        type=Path,
        default=ROOT / "whole_aot_functions.json",
    )
    parser.add_argument(
        "--code",
        type=Path,
        default=operational_path("oot3d_code_bin"),
        help="decrypted code.bin matched by the whole-AOT program",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=REPO_ROOT / "build-codex/oot3d_whole_aot_cpp",
    )
    parser.add_argument(
        "--shards",
        type=int,
        default=32,
        help="stable generated C++ function shard count",
    )
    parser.add_argument(
        "--shard-strategy",
        choices=("stable", "balanced", "incremental", "affinity"),
        default="stable",
        help=(
            "stable hash placement, weight-balanced placement, preservation "
            "of existing assignments, or direct-call affinity placement"
        ),
    )
    args = parser.parse_args(argv)
    if args.shards <= 0:
        parser.error("--shards must be positive")
    if not args.program.is_file():
        result = generate_program(["--output", str(args.program)])
        if result != 0:
            return result
    manifest = generate(
        args.program,
        args.selection,
        args.code,
        args.output,
        shard_count=args.shards,
        shard_strategy=args.shard_strategy,
    )
    functions = manifest["functions"]
    print(
        f"Whole-function C++ AOT: {len(functions)} function(s), "
        f"{sum(item['instructions'] for item in functions)} instructions, "
        f"{manifest['shard_count']} shard(s)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
