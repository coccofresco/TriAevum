#!/usr/bin/env python3
"""Generates a title's whole-AOT C++ shards (the source of its title module)
from its structural program, function selection and code.bin.

usage: generate_whole_aot_cpp.py program.json selection.json code.bin output_dir [--shards 256]
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "oot3d/native_a32_runtime"))
from whole_aot_cpp import generate  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("program", type=Path)
    parser.add_argument("selection", type=Path)
    parser.add_argument("code", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--shards", type=int, default=256)
    parser.add_argument("--strategy", default="stable")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = generate(args.program, args.selection, args.code, args.output,
                        shard_count=args.shards, shard_strategy=args.strategy)
    print("generated", len(manifest.get("files", {})), "files in", args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
