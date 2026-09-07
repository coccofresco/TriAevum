#!/usr/bin/env python3
"""Analyze title-intro material-scalar fog payload writer traces."""

from __future__ import annotations

import argparse
import csv
import json
import re
import struct
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


FOG_LABEL_RE = re.compile(r"material_scalar_fog_rgb_float candidate=(\d+) state_base=(0x[0-9A-Fa-f]+)")
STATE_LABEL_RE = re.compile(
    r"material_scalar_submit_state candidate=(\d+) fog_rgb_float_ptr=(0x[0-9A-Fa-f]+)"
)


def parse_hex(value: str | None) -> int | None:
    if value is None or value == "":
        return None
    try:
        return int(value, 16)
    except ValueError:
        return None


def fmt_u32(value: int | None) -> str | None:
    if value is None:
        return None
    return f"0x{value & 0xFFFFFFFF:08X}"


def f32_from_u32(value: int) -> float:
    return struct.unpack("<f", value.to_bytes(4, "little", signed=False))[0]


def float_to_u8(value: float) -> int:
    return max(0, min(255, int(round(value * 255.0))))


def parse_watchlist(path: Path) -> dict[int, dict[str, Any]]:
    candidates: dict[int, dict[str, Any]] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        label = line.split("#", 1)[1].strip() if "#" in line else ""
        fog = FOG_LABEL_RE.search(label)
        if fog:
            index = int(fog.group(1))
            candidates.setdefault(index, {})["state_base"] = int(fog.group(2), 16)
            continue
        state = STATE_LABEL_RE.search(label)
        if state:
            index = int(state.group(1))
            candidates.setdefault(index, {})["fog_rgb_float_ptr"] = int(state.group(2), 16)
    return candidates


def load_pica_fog_color(path: Path | None) -> list[int] | None:
    if path is None or not path.exists():
        return None
    data = json.loads(path.read_text(encoding="utf-8"))
    rgb = data.get("fog_state", {}).get("color_rgb_u8")
    if not isinstance(rgb, dict):
        return None
    return [int(rgb.get("r", 0)), int(rgb.get("g", 0)), int(rgb.get("b", 0))]


def analyze_trace(writer_trace: Path, watchlist: Path, pica_trace: Path | None) -> dict[str, Any]:
    candidates = parse_watchlist(watchlist)
    by_state = {info.get("state_base"): index for index, info in candidates.items()}
    by_fog = {info.get("fog_rgb_float_ptr"): index for index, info in candidates.items()}

    candidate_rows: dict[int, int] = defaultdict(int)
    pc_lrs: dict[int, Counter[tuple[str, str]]] = defaultdict(Counter)
    object_bases: dict[int, Counter[int]] = defaultdict(Counter)
    component_values: dict[int, dict[int, Counter[int]]] = defaultdict(lambda: defaultdict(Counter))
    triplet_snapshots: dict[int, Counter[tuple[int, int, int]]] = defaultdict(Counter)
    latest_components: dict[int, dict[int, int]] = defaultdict(dict)

    row_count = 0
    with writer_trace.open("r", newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            row_count += 1
            label = row.get("label", "")
            index: int | None = None
            fog_label = FOG_LABEL_RE.search(label)
            state_label = STATE_LABEL_RE.search(label)
            if fog_label:
                index = int(fog_label.group(1))
            elif state_label:
                index = int(state_label.group(1))
            if index is None:
                continue

            candidate_rows[index] += 1
            pc = row.get("pc", "")
            lr = row.get("lr", "")
            pc_lrs[index][(pc, lr)] += 1

            r0 = parse_hex(row.get("r0"))
            r1 = parse_hex(row.get("r1"))
            if pc in {"0x004C05C0", "0x002D6124", "0x0033EA1C", "0x00368750"}:
                state_base = candidates.get(index, {}).get("state_base")
                if r0 == state_base and r1 is not None:
                    object_bases[index][r1 - 4] += 1

            address = parse_hex(row.get("address"))
            value = parse_hex(row.get("value"))
            size_text = row.get("size")
            try:
                size = int(size_text, 0) if size_text else 0
            except ValueError:
                size = 0
            fog_ptr = candidates.get(index, {}).get("fog_rgb_float_ptr")
            if address is None or value is None or fog_ptr is None or size != 4:
                continue
            if not (fog_ptr <= address < fog_ptr + 12):
                continue
            component = (address - fog_ptr) // 4
            if component not in {0, 1, 2}:
                continue
            component_values[index][component][value] += 1
            latest_components[index][component] = value
            if all(component_index in latest_components[index] for component_index in (0, 1, 2)):
                triplet = tuple(latest_components[index][component_index] for component_index in (0, 1, 2))
                triplet_snapshots[index][triplet] += 1

    selected: list[dict[str, Any]] = []
    for index in sorted(candidates):
        info = candidates[index]
        top_triplets = []
        for triplet, count in triplet_snapshots[index].most_common(12):
            floats = [f32_from_u32(value) for value in triplet]
            top_triplets.append(
                {
                    "raw_words": [fmt_u32(value) for value in triplet],
                    "float_rgb": floats,
                    "rgb_u8_rounded": [float_to_u8(value) for value in floats],
                    "count": int(count),
                }
            )
        selected.append(
            {
                "candidate": index,
                "state_base": fmt_u32(info.get("state_base")),
                "fog_rgb_float_ptr": fmt_u32(info.get("fog_rgb_float_ptr")),
                "row_count": int(candidate_rows[index]),
                "top_object_bases_from_registers": [
                    {"object_base": fmt_u32(value), "count": int(count)}
                    for value, count in object_bases[index].most_common(8)
                ],
                "top_pc_lrs": [
                    {"pc": pc, "lr": lr, "count": int(count)}
                    for (pc, lr), count in pc_lrs[index].most_common(12)
                ],
                "top_float_triplets": top_triplets,
            }
        )

    pica_rgb = load_pica_fog_color(pica_trace)
    return {
        "format": "oot3d_title_intro_material_scalar_source_trace_v1",
        "policy": "validation_only_not_runtime_replacement_data",
        "writer_trace": str(writer_trace),
        "watchlist": str(watchlist),
        "pica_trace": str(pica_trace) if pica_trace is not None else None,
        "pica_gpureg_fog_color_rgb": pica_rgb,
        "row_count": row_count,
        "candidate_count": len(selected),
        "candidates": selected,
    }


def write_markdown(path: Path, report: dict[str, Any]) -> None:
    lines = [
        "# OOT3D Title Intro Material Scalar Source Trace",
        "",
        "This report is validation/backtrace evidence only. It identifies native producers for the material-scalar fog payload; it is not runtime replacement data.",
        "",
        f"- Writer trace: `{report['writer_trace']}`",
        f"- Watchlist: `{report['watchlist']}`",
        f"- PICA `GPUREG_FOG_COLOR`: `{report.get('pica_gpureg_fog_color_rgb')}`",
        f"- Rows: `{report['row_count']}`",
        "",
        "## Candidates",
        "",
    ]
    for candidate in report["candidates"]:
        lines.extend(
            [
                f"### Candidate {candidate['candidate']}",
                "",
                f"- State base: `{candidate['state_base']}`",
                f"- Fog RGB float pointer: `{candidate['fog_rgb_float_ptr']}`",
                f"- Rows: `{candidate['row_count']}`",
                "",
                "Top object bases from registers:",
                "",
                "| Object base | Count |",
                "|---|---:|",
            ]
        )
        for entry in candidate["top_object_bases_from_registers"]:
            lines.append(f"| `{entry['object_base']}` | {entry['count']} |")
        lines.extend(["", "Top writer PCs:", "", "| PC | LR | Count |", "|---|---|---:|"])
        for entry in candidate["top_pc_lrs"]:
            lines.append(f"| `{entry['pc']}` | `{entry['lr']}` | {entry['count']} |")
        lines.extend(["", "Observed float triplets:", "", "| RGB u8 | Float RGB | Count |", "|---|---|---:|"])
        for entry in candidate["top_float_triplets"][:8]:
            float_text = ", ".join(f"{value:.9f}" for value in entry["float_rgb"])
            lines.append(f"| `{entry['rgb_u8_rounded']}` | `{float_text}` | {entry['count']} |")
        lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--writer-trace", type=Path, required=True)
    parser.add_argument("--watchlist", type=Path, required=True)
    parser.add_argument("--pica-trace", type=Path)
    parser.add_argument("--output-json", type=Path, required=True)
    parser.add_argument("--output-md", type=Path, required=True)
    args = parser.parse_args()

    report = analyze_trace(args.writer_trace, args.watchlist, args.pica_trace)
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(report, indent=2) + "\n", encoding="ascii")
    write_markdown(args.output_md, report)
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
