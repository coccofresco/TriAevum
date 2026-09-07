#!/usr/bin/env python3
"""Promote ranked N64 importability rows into a generated exact-seed batch."""

from __future__ import annotations

import argparse
import csv
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


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


def snake(value: str) -> str:
    value = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", value)
    value = re.sub(r"[^A-Za-z0-9]+", "_", value)
    return value.strip("_").lower()


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
    else:
        lines[-1:] = [source]
    path.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")


def select_rows(args: argparse.Namespace) -> list[dict[str, str]]:
    rows = read_csv(args.importability)
    selected = []
    explicit_entries = {entry.lower() for entry in args.entry}
    for row in rows:
        if explicit_entries and row.get("oot3d_entry", "").lower() not in explicit_entries:
            continue
        if not explicit_entries and args.bucket and row.get("bucket") != args.bucket:
            continue
        if args.n64_name and row.get("n64_name") != args.n64_name:
            continue
        if args.n64_path and row.get("n64_path") != args.n64_path:
            continue
        if row.get("mapped_status"):
            continue
        selected.append(row)
        if args.limit and len(selected) >= args.limit:
            break
    return selected


def generated_name(row: dict[str, str], prefix: str) -> str:
    base = prefix or f"oot3d_{snake(row['n64_name'])}"
    return f"{base}_{row['oot3d_entry'].lower()}"


def update_n64_map(path: Path, rows: list[dict[str, str]], port_file: str, names: dict[str, str]) -> None:
    fieldnames = [
        "oot3d_entry",
        "oot3d_name",
        "n64_source",
        "n64_name",
        "port_file",
        "status",
        "notes",
    ]
    existing = read_csv(path)
    existing_entries = {row["oot3d_entry"].lower() for row in existing}
    for row in rows:
        entry = row["oot3d_entry"].lower()
        if entry in existing_entries:
            continue
        existing.append(
            {
                "oot3d_entry": entry,
                "oot3d_name": names[entry],
                "n64_source": row["n64_path"],
                "n64_name": row["n64_name"],
                "port_file": port_file,
                "status": "exact-seed-started",
                "notes": (
                    "Split/subsystem N64 queue batch. Exact seed is generated from the OOT3D target "
                    f"while the N64 {row['n64_name']} source is extracted to recover the split helper role."
                ),
            }
        )
    write_csv(path, existing, fieldnames)


def update_manual_symbols(path: Path, rows: list[dict[str, str]], source_file: str, names: dict[str, str]) -> None:
    fieldnames = ["entry", "old_name", "new_name", "kind", "confidence", "source_file", "notes"]
    existing = read_csv(path)
    existing_entries = {row["entry"].lower() for row in existing}
    for row in rows:
        entry = row["oot3d_entry"].lower()
        if entry in existing_entries:
            continue
        existing.append(
            {
                "entry": entry,
                "old_name": row.get("oot3d_name") or f"FUN_{entry}",
                "new_name": names[entry],
                "kind": "function",
                "confidence": "medium",
                "source_file": source_file,
                "notes": (
                    f"Split/subsystem exact-seed batch target mapped to N64 {row['n64_name']}. "
                    "Exact seed is matched in the baseline while high-level C role recovery remains pending."
                ),
            }
        )
    write_csv(path, existing, fieldnames)


def generate_source(args: argparse.Namespace, rows: list[dict[str, str]], names: dict[str, str]) -> None:
    command = [
        sys.executable,
        str(ROOT / "scripts" / "generate_exact_seed_source.py"),
        "--out",
        str(ROOT / args.source),
        "--banner",
        args.banner,
    ]
    for row in rows:
        entry = row["oot3d_entry"].lower()
        command.extend(["--function", f"{entry}:{names[entry]}"])
    subprocess.run(command, cwd=ROOT, check=True)


def write_summary(path: Path, rows: list[dict[str, str]], names: dict[str, str], source: str) -> None:
    lines = [
        "# Promoted Exact-Seed Batch",
        "",
        f"- Generated source: `{source}`",
        f"- Promoted functions: `{len(rows)}`",
        "",
        "| OOT3D | Generated name | N64 source | N64 function | Bucket | Score | Ratio |",
        "| --- | --- | --- | --- | --- | ---: | ---: |",
    ]
    for row in rows:
        entry = row["oot3d_entry"].lower()
        lines.append(
            f"| `{entry}` `{row.get('oot3d_name', '')}` | `{names[entry]}` | "
            f"`{row['n64_path']}` | `{row['n64_name']}` | `{row.get('bucket', '')}` | "
            f"{row.get('score', '')} | {row.get('line_ratio', '')} |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--importability", type=Path, default=ROOT / "analysis" / "n64_importability.csv")
    parser.add_argument("--bucket", default="split-or-subsystem")
    parser.add_argument("--n64-name", default="")
    parser.add_argument("--n64-path", default="")
    parser.add_argument("--entry", action="append", default=[])
    parser.add_argument("--limit", type=int, default=5)
    parser.add_argument("--name-prefix", default="")
    parser.add_argument("--source", required=True)
    parser.add_argument("--summary", type=Path, required=True)
    parser.add_argument("--banner", default="Generated exact seeds for a ranked N64 split/subsystem queue.")
    args = parser.parse_args()

    rows = select_rows(args)
    if not rows:
        raise SystemExit("no importability rows selected")

    names = {row["oot3d_entry"].lower(): generated_name(row, args.name_prefix) for row in rows}
    generate_source(args, rows, names)
    append_source(ROOT / "metadata" / "matched_sources.txt", args.source)
    update_n64_map(ROOT / "metadata" / "n64_port_map.csv", rows, args.source, names)
    update_manual_symbols(ROOT / "symbols" / "manual_symbols.csv", rows, args.source, names)
    write_summary(args.summary, rows, names, args.source)

    print(f"promoted {len(rows)} rows into {args.source}")
    for row in rows:
        entry = row["oot3d_entry"].lower()
        print(f"{entry} {names[entry]} <= {row['n64_path']}::{row['n64_name']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
