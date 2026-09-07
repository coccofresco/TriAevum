#!/usr/bin/env python3
"""Extract mapped N64 source functions for OOT3D porting units."""

from __future__ import annotations

import argparse
import csv
import re
import shutil
import sys
from collections import defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_N64_ROOT = ROOT.parent / "external" / "oot"

sys.path.insert(0, str(ROOT / "scripts"))
from rank_n64_port_candidates import iter_c_functions, rel  # noqa: E402


def slug(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value).strip("_")


def read_map(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def extract_function(n64_root: Path, source: str, name: str) -> str:
    path = n64_root / source
    if not path.is_file():
        raise SystemExit(f"N64 source not found: {path}")
    for function_name, body in iter_c_functions(path):
        if function_name == name:
            return body.rstrip() + "\n"
    raise SystemExit(f"N64 function {name} not found in {path}")


def write_unit(
    out_root: Path,
    port_file: str,
    rows: list[dict[str, str]],
    n64_root: Path,
) -> None:
    unit_dir = out_root / slug(port_file)
    if unit_dir.exists():
        for child in unit_dir.iterdir():
            if child.is_dir():
                shutil.rmtree(child)
            else:
                child.unlink()
    unit_dir.mkdir(parents=True, exist_ok=True)

    lines = [
        f"# N64 Port Unit `{port_file}`",
        "",
        "Generated from `metadata/n64_port_map.csv`.",
        "",
        "| OOT3D | N64 source function | Status | Extract | Notes |",
        "| --- | --- | --- | --- | --- |",
    ]

    combined: list[str] = [
        "/* N64 source extracts for OOT3D porting.",
        " * This file is reference input for structured ports, not a directly",
        " * compilable OOT3D translation unit.",
        " */",
        "",
    ]

    for row in rows:
        body = extract_function(n64_root, row["n64_source"], row["n64_name"])
        filename = f"{row['oot3d_entry']}_{row['oot3d_name']}__n64_{row['n64_name']}.c"
        filename = slug(filename)
        (unit_dir / filename).write_text(body, encoding="utf-8")
        combined.extend(
            [
                f"/* OOT3D {row['oot3d_entry']} {row['oot3d_name']} <= N64 {row['n64_name']} */",
                body,
                "",
            ]
        )
        lines.append(
            f"| `{row['oot3d_entry']}` `{row['oot3d_name']}` | "
            f"`{row['n64_name']}` | `{row['status']}` | `{filename}` | {row['notes']} |"
        )

    (unit_dir / "n64_source_extracts.c").write_text("\n".join(combined).rstrip() + "\n", encoding="utf-8")
    (unit_dir / "README.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {rel(unit_dir)} ({len(rows)} functions)")


def prune_stale_units(out_root: Path, expected_unit_dirs: set[str]) -> None:
    if not out_root.is_dir():
        return

    for unit_dir in out_root.iterdir():
        if not unit_dir.is_dir() or unit_dir.name in expected_unit_dirs:
            continue

        readme = unit_dir / "README.md"
        if not readme.is_file():
            continue
        if "Generated from `metadata/n64_port_map.csv`." not in readme.read_text(encoding="utf-8"):
            continue

        shutil.rmtree(unit_dir)
        print(f"removed stale {rel(unit_dir)}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map", type=Path, default=ROOT / "metadata" / "n64_port_map.csv")
    parser.add_argument("--n64-root", type=Path, default=DEFAULT_N64_ROOT)
    parser.add_argument("--out-dir", type=Path, default=ROOT / "analysis" / "n64_port_units")
    parser.add_argument("--port-file", help="only extract rows for one OOT3D port file")
    args = parser.parse_args()

    rows = read_map(args.map)
    if args.port_file:
        rows = [row for row in rows if row["port_file"] == args.port_file]
    if not rows:
        raise SystemExit("no rows selected")

    grouped: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        grouped[row["port_file"]].append(row)

    if not args.port_file:
        prune_stale_units(args.out_dir, {slug(port_file) for port_file in grouped})

    for port_file, group_rows in sorted(grouped.items()):
        write_unit(args.out_dir, port_file, group_rows, args.n64_root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
