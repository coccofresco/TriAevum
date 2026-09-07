"""Generate the whole-program AOT artifact from the workspace inputs."""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Sequence

from generate_aot import (
    SUPPLEMENTAL_ENTRIES,
    UPSTREAM,
    add_local_entry_intervals,
    operational_path,
)
from whole_aot_program import DEFAULT_BASE, main as extract_program


ROOT = Path(__file__).resolve().parent
REPO_ROOT = ROOT.parents[2]


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Generate the complete structural OOT3D AOT program."
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=REPO_ROOT / "build-codex/oot3d_whole_aot/aot_program.json",
    )
    parser.add_argument(
        "--inventory-output",
        type=Path,
        default=(
            REPO_ROOT
            / "build-codex/oot3d_a32_generated/inventory_with_process_entry.csv"
        ),
        help="generated inventory path kept alongside the owning artifact cache",
    )
    parser.add_argument("--include-instructions", action="store_true")
    parser.add_argument("--force", action="store_true")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    code_path = operational_path("oot3d_code_bin")
    exheader_path = operational_path("oot3d_exheader")
    exheader = exheader_path.read_bytes()
    if len(exheader) < 0x18:
        raise ValueError("OOT3D ExHeader is truncated")
    entrypoint = int.from_bytes(exheader[0x10:0x14], "little")
    executable_size = int.from_bytes(exheader[0x14:0x18], "little") * 0x1000
    if executable_size == 0:
        raise ValueError("OOT3D ExHeader has an empty text segment")

    inventory = add_local_entry_intervals(
        UPSTREAM / "analysis/codebin_function_inventory.csv",
        SUPPLEMENTAL_ENTRIES,
        args.inventory_output,
        entrypoint,
    )
    arguments = [
        "--code",
        str(code_path),
        "--inventory",
        str(inventory),
        "--boundary-audit",
        str(UPSTREAM / "analysis/codebin_callable_boundary_residue_audit_166.csv"),
        "--base",
        hex(DEFAULT_BASE),
        "--executable-size",
        hex(executable_size),
        "--output",
        str(args.output),
    ]
    if args.include_instructions:
        arguments.append("--include-instructions")
    if args.force:
        arguments.append("--force")
    return extract_program(arguments)


if __name__ == "__main__":
    raise SystemExit(main())
