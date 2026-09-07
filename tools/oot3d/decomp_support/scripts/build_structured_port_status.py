#!/usr/bin/env python3
"""Build a status report for N64-derived structured ports."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path

from audit_c_conversion_readiness import BASELINE_DEFINES, cflag_defines, source_style


def read_map(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def read_compare(path: Path) -> dict[str, dict[str, object]]:
    if not path.is_file():
        return {}
    rows = json.loads(path.read_text(encoding="utf-8"))
    return {str(row["name"]): row for row in rows}


def format_diff(diff: object) -> str:
    if not isinstance(diff, dict):
        return ""
    target = diff.get("target")
    compiled = diff.get("compiled")
    index = diff.get("index")
    return f"{index}: `{target}` vs `{compiled}`"


def format_run(run: object) -> str:
    if not isinstance(run, dict):
        return ""
    return f"{run.get('length', '')} @ {run.get('target_index', '')}/{run.get('compiled_index', '')}"


def write_report(
    path: Path,
    map_rows: list[dict[str, str]],
    structured_compare: dict[str, dict[str, object]],
    default_compare: dict[str, dict[str, object]],
    structured_compare_path: Path,
    default_compare_path: Path,
    port_file: str,
    title: str,
    n64_extracts: str,
    extra_cflag: str = "",
) -> None:
    selected = [row for row in map_rows if row["port_file"] == port_file]
    defines = set(BASELINE_DEFINES) | cflag_defines(extra_cflag)

    default_exact = sum(1 for row in default_compare.values() if row.get("exact_match"))
    default_total = len(default_compare)
    structured_exact = sum(1 for row in structured_compare.values() if row.get("exact_match"))
    structured_total = len(structured_compare)

    lines = [
        f"# {title}",
        "",
        "Generated from the explicit N64 map in `metadata/n64_port_map.csv`.",
        "",
        "## Source Inputs",
        "",
        f"- OOT3D maintained source: `{port_file}`",
        f"- N64 extracts: `{n64_extracts}`",
        f"- Default exact baseline: `{default_compare_path}`",
        f"- Structured build comparison: `{structured_compare_path}`",
        "",
        "## Current Results",
        "",
        "| Lane | Compared | Exact |",
        "| --- | ---: | ---: |",
        f"| Default maintained source | {default_total} | {default_exact} |",
        f"| Structured N64-derived source | {structured_total} | {structured_exact} |",
        "",
        "## Mapped Functions",
        "",
        "| OOT3D function | N64 source function | Status | Style | Target insns | Structured insns | Prefix | LCS | Longest run | Exact | First difference |",
        "| --- | --- | --- | --- | ---: | ---: | ---: | ---: | --- | --- | --- |",
    ]

    for row in selected:
        name = row["oot3d_name"]
        compare = structured_compare.get(name)
        style = source_style(port_file, name, defines)
        if compare:
            target_count = compare["target_instruction_count"]
            compiled_count = compare["compiled_instruction_count"]
            prefix = compare["matching_prefix"]
            lcs = compare.get("lcs_instruction_count", "")
            run = format_run(compare.get("longest_common_run"))
            exact = str(compare["exact_match"]).lower()
            diff = format_diff(compare.get("first_difference"))
        else:
            target_count = ""
            compiled_count = ""
            prefix = ""
            lcs = ""
            run = ""
            exact = ""
            diff = "not built in structured lane"

        lines.append(
            f"| `{name}` | `{row['n64_name']}` | `{row['status']}` | `{style}` | "
            f"{target_count} | {compiled_count} | {prefix} | {lcs} | {run} | {exact} | {diff} |"
        )

    lines.extend(
        [
            "",
            "## Interpretation",
            "",
            "Structured lanes can be plain C, inline-asm constrained C, or naked exact seeds.",
            "Use the `Style` column and `analysis/convertible_ports.md` before promotion; naked exact seeds",
            "must still be reconstructed into target-shaped C.",
        ]
    )

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map", type=Path, default=Path("metadata/n64_port_map.csv"))
    parser.add_argument("--structured-compare", type=Path, default=Path("build/boss_va_structured_compare/compare_matched_objects.json"))
    parser.add_argument("--default-compare", type=Path, default=Path("build/matched/compare_matched_objects.json"))
    parser.add_argument("--port-file", default="src/overlays/actors/ovl_Boss_Va/z_boss_va.c")
    parser.add_argument("--out", type=Path, default=Path("analysis/structured_port_status/boss_va_zapper.md"))
    parser.add_argument("--title", default="BossVa Zapper Structured Port Status")
    parser.add_argument("--n64-extracts", default="analysis/n64_port_units/src_overlays_actors_ovl_Boss_Va_z_boss_va.c/")
    parser.add_argument("--extra-cflag", default="")
    args = parser.parse_args()

    write_report(
        args.out,
        read_map(args.map),
        read_compare(args.structured_compare),
        read_compare(args.default_compare),
        args.structured_compare,
        args.default_compare,
        args.port_file,
        args.title,
        args.n64_extracts,
        args.extra_cflag,
    )
    print(args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
