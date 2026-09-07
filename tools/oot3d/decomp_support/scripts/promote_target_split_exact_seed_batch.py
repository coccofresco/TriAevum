#!/usr/bin/env python3
"""Promote created target splits into generated exact-seed matched sources."""

from __future__ import annotations

import argparse
import csv
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MAP = ROOT / "metadata" / "n64_port_map.csv"
DEFAULT_SOURCE_LIST = ROOT / "metadata" / "matched_sources.txt"
DEFAULT_SOURCE = "src/n64_target_split_exact.c"
DEFAULT_SUMMARY = ROOT / "analysis" / "promoted_target_split_exact_seeds.md"
MAP_FIELDS = ["oot3d_entry", "oot3d_name", "n64_source", "n64_name", "port_file", "status", "notes"]


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def normalize_entry(value: str) -> str:
    value = value.strip().lower().removeprefix("0x")
    if len(value) != 8:
        raise ValueError(f"entry must be 8 hex digits: {value}")
    int(value, 16)
    return value


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def write_csv(path: Path, rows: list[dict[str, str]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def read_source_list(path: Path) -> list[str]:
    if not path.is_file():
        return []
    return path.read_text(encoding="utf-8").splitlines()


def append_source(path: Path, source: str) -> None:
    lines = read_source_list(path)
    existing = {line.strip() for line in lines if line.strip() and not line.strip().startswith("#")}
    if source in existing:
        return
    if lines and lines[-1].strip():
        lines.append(source)
    elif lines:
        lines[-1:] = [source]
    else:
        lines = [source]
    path.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")


def select_rows(args: argparse.Namespace, rows: list[dict[str, str]]) -> list[dict[str, str]]:
    entries = {normalize_entry(entry) for entry in args.entry}
    statuses = set(args.status)
    selected = []
    for row in rows:
        entry = normalize_entry(row.get("oot3d_entry", ""))
        if entries and entry not in entries:
            continue
        if not entries and row.get("status", "") not in statuses:
            continue
        if not row.get("oot3d_name"):
            raise SystemExit(f"map row {entry} has no oot3d_name")
        selected.append(row)
    return selected


def generate_source(args: argparse.Namespace, rows: list[dict[str, str]]) -> None:
    command = [
        sys.executable,
        str(ROOT / "scripts" / "generate_exact_seed_source.py"),
        "--out",
        str(ROOT / args.source),
        "--banner",
        args.banner,
    ]
    for row in rows:
        entry = normalize_entry(row["oot3d_entry"])
        command.extend(["--function", f"{entry}:{row['oot3d_name']}"])
    subprocess.run(command, cwd=ROOT, check=True)


def update_n64_map(path: Path, rows: list[dict[str, str]], selected: list[dict[str, str]], source: str) -> None:
    selected_entries = {normalize_entry(row["oot3d_entry"]) for row in selected}
    for row in rows:
        entry = normalize_entry(row.get("oot3d_entry", ""))
        if entry not in selected_entries:
            continue
        row["port_file"] = source
        row["status"] = "target-split-exact-seed"
        row["notes"] = (
            "Target split exact seed generated from the OOT3D function recovered by the direct split workflow. "
            f"N64 helper source is {row.get('n64_source', '')}::{row.get('n64_name', '')}; "
            "matched assembly is now in the baseline while high-level C reconstruction remains pending."
        )
    write_csv(path, rows, MAP_FIELDS)


def write_summary(path: Path, rows: list[dict[str, str]], source: str) -> None:
    lines = [
        "# Promoted Target Split Exact Seeds",
        "",
        f"- Generated source: `{source}`",
        f"- Promoted target splits: `{len(rows)}`",
        "",
        "| OOT3D | N64 source | N64 function | Selected status |",
        "| --- | --- | --- | --- |",
    ]
    for row in rows:
        entry = normalize_entry(row["oot3d_entry"])
        lines.append(
            f"| `{entry}` `{row.get('oot3d_name', '')}` | "
            f"`{row.get('n64_source', '')}` | `{row.get('n64_name', '')}` | "
            f"`{row.get('status', '')}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map", type=Path, default=DEFAULT_MAP)
    parser.add_argument("--source-list", type=Path, default=DEFAULT_SOURCE_LIST)
    parser.add_argument("--source", default=DEFAULT_SOURCE)
    parser.add_argument("--summary", type=Path, default=DEFAULT_SUMMARY)
    parser.add_argument("--entry", action="append", default=[])
    parser.add_argument(
        "--status",
        action="append",
        default=["target-split-symbolized", "target-split-exact-seed"],
        help="n64_port_map status to include; repeatable",
    )
    parser.add_argument(
        "--banner",
        default="Generated exact seeds for OOT3D target splits recovered from N64 helper workorders.",
    )
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    rows = read_csv(args.map)
    selected = select_rows(args, rows)
    if not selected:
        raise SystemExit("no target split rows selected")

    if args.dry_run:
        for row in selected:
            print(f"would promote {normalize_entry(row['oot3d_entry'])}: {row['oot3d_name']}")
        print(f"validated {len(selected)} target split exact seeds")
        return 0

    generate_source(args, selected)
    append_source(args.source_list, args.source)
    update_n64_map(args.map, rows, selected, args.source)
    write_summary(args.summary, selected, args.source)

    print(f"promoted {len(selected)} target split exact seeds into {args.source}")
    print(rel(args.summary))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
