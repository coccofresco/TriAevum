#!/usr/bin/env python3
"""Build a global index for structured N64-derived port units."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def read_compare(path: Path) -> dict[str, dict[str, object]]:
    if not path.is_file():
        return {}
    rows = json.loads(path.read_text(encoding="utf-8"))
    return {str(row["name"]): row for row in rows}


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def count_extracted(path: Path) -> int:
    if not path.is_dir():
        return 0
    return len([item for item in path.glob("*.c") if item.name != "n64_source_extracts.c"])


def unit_rows(
    units: list[dict[str, str]],
    map_rows: list[dict[str, str]],
    default_compare: dict[str, dict[str, object]],
) -> list[dict[str, object]]:
    rows = []
    for unit in units:
        port_file = unit["port_file"]
        mapped = [row for row in map_rows if row["port_file"] == port_file]
        structured_compare = read_compare(ROOT / unit["out_dir"] / "compare_matched_objects.json")
        compared_names = set(structured_compare)

        rows.append(
            {
                "unit": unit["unit"],
                "port_file": port_file,
                "source": unit["source"],
                "source_exists": (ROOT / unit["source"]).is_file(),
                "extra_cflag": unit.get("extra_cflag", ""),
                "out_dir": unit["out_dir"],
                "status_report": unit["status_report"],
                "n64_extracts": unit.get("n64_extracts", ""),
                "mapped_functions": len(mapped),
                "structured_started": sum(1 for row in mapped if row.get("status") == "structured-port-started"),
                "semantic_mapped": sum(1 for row in mapped if row.get("status") == "semantic-mapped"),
                "extracted_functions": count_extracted(ROOT / unit.get("n64_extracts", "")),
                "default_compared": sum(1 for row in mapped if row["oot3d_name"] in default_compare),
                "default_exact": sum(
                    1
                    for row in mapped
                    if row["oot3d_name"] in default_compare and default_compare[row["oot3d_name"]].get("exact_match")
                ),
                "structured_compared": len(compared_names),
                "structured_exact": sum(1 for row in structured_compare.values() if row.get("exact_match")),
            }
        )
    return rows


def write_outputs(
    rows: list[dict[str, object]],
    out_json: Path,
    out_md: Path,
    out_csv: Path,
) -> None:
    out_json.parent.mkdir(parents=True, exist_ok=True)
    out_json.write_text(json.dumps({"units": rows}, indent=2) + "\n", encoding="utf-8")

    out_csv.parent.mkdir(parents=True, exist_ok=True)
    with out_csv.open("w", newline="", encoding="utf-8") as handle:
        fieldnames = [
            "unit",
            "port_file",
            "source",
            "source_exists",
            "mapped_functions",
            "structured_started",
            "semantic_mapped",
            "extracted_functions",
            "default_compared",
            "default_exact",
            "structured_compared",
            "structured_exact",
            "status_report",
            "n64_extracts",
        ]
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: row.get(key, "") for key in fieldnames})

    lines = [
        "# Structured Port Unit Index",
        "",
        "Generated from `metadata/structured_port_units.csv`, `metadata/n64_port_map.csv`, and object comparison reports.",
        "",
        "| Unit | Source | Mapped | Started | Extracted | Default exact | Structured exact | Report |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |",
    ]
    for row in rows:
        source_state = "yes" if row["source_exists"] else "missing"
        lines.append(
            f"| `{row['unit']}` | `{row['source']}` ({source_state}) | {row['mapped_functions']} | "
            f"{row['structured_started']} | {row['extracted_functions']} | "
            f"{row['default_exact']}/{row['default_compared']} | "
            f"{row['structured_exact']}/{row['structured_compared']} | `{row['status_report']}` |"
        )

    if not rows:
        lines.append("| - | - | 0 | 0 | 0 | 0/0 | 0/0 | - |")

    out_md.parent.mkdir(parents=True, exist_ok=True)
    out_md.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(rel(out_md))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--units", type=Path, default=ROOT / "metadata" / "structured_port_units.csv")
    parser.add_argument("--map", type=Path, default=ROOT / "metadata" / "n64_port_map.csv")
    parser.add_argument("--default-compare", type=Path, default=ROOT / "build" / "matched" / "compare_matched_objects.json")
    parser.add_argument("--out-json", type=Path, default=ROOT / "analysis" / "structured_port_status" / "index.json")
    parser.add_argument("--out-md", type=Path, default=ROOT / "analysis" / "structured_port_status" / "index.md")
    parser.add_argument("--out-csv", type=Path, default=ROOT / "analysis" / "structured_port_status" / "index.csv")
    args = parser.parse_args()

    write_outputs(
        unit_rows(read_csv(args.units), read_csv(args.map), read_compare(args.default_compare)),
        args.out_json,
        args.out_md,
        args.out_csv,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
