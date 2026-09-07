#!/usr/bin/env python3
"""Build a title-intro material-scalar fog payload watchlist.

The input is an Azahar writer trace that already correlates GPUREG_FOG_COLOR
emission with the native material path. The output watches the material submit
state and the `param_1 + 0x1b` float triplet consumed by FUN_0047D6AC and
FUN_0047FE44. Captured values are validation/backtrace evidence only.
"""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path
from typing import Any


DEFAULT_INPUT = Path(
    "captures/azahar_pica/title_intro_slot6_fog_color_emit_writer_20260707_02/"
    "derived/oot3d_pica_writer_trace.csv"
)
DEFAULT_OUTPUT = Path(
    "captures/azahar_pica/derived/watch_addresses.title_intro_material_scalar_fog.txt"
)
MATERIAL_EMIT_PCS = {"0x00452894", "0x004528A4", "0x0047FE44", "0x0047D6DC"}
FOG_FLOAT_OFFSET = 0x6C


def parse_hex(value: str | None) -> int | None:
    if value is None or value == "":
        return None
    try:
        return int(value, 16)
    except ValueError:
        return None


def fmt_u32(value: int) -> str:
    return f"0x{value & 0xFFFFFFFF:08X}"


def plausible_ptr(value: int | None) -> bool:
    if value is None:
        return False
    return 0x08000000 <= value < 0x18000000


def discover_candidates(path: Path) -> list[dict[str, Any]]:
    counts: Counter[tuple[int, int, str, str]] = Counter()
    with path.open("r", newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            pc = row.get("pc", "")
            if pc not in MATERIAL_EMIT_PCS:
                continue
            r1 = parse_hex(row.get("r1"))
            r4 = parse_hex(row.get("r4"))
            if not plausible_ptr(r1) or not plausible_ptr(r4):
                continue
            if r1 != r4 + FOG_FLOAT_OFFSET:
                continue
            counts[(r4, r1, pc, row.get("lr", ""))] += 1

    by_state: dict[tuple[int, int], Counter[tuple[str, str]]] = {}
    for (state_base, fog_ptr, pc, lr), count in counts.items():
        by_state.setdefault((state_base, fog_ptr), Counter())[(pc, lr)] += count

    candidates: list[dict[str, Any]] = []
    for (state_base, fog_ptr), pc_counts in sorted(
        by_state.items(), key=lambda item: (-sum(item[1].values()), item[0][0])
    ):
        candidates.append(
            {
                "state_base": fmt_u32(state_base),
                "fog_rgb_float_ptr": fmt_u32(fog_ptr),
                "count": int(sum(pc_counts.values())),
                "top_pc_lrs": [
                    {"pc": pc, "lr": lr, "count": int(count)}
                    for (pc, lr), count in pc_counts.most_common(8)
                ],
            }
        )
    return candidates


def build_watches(candidates: list[dict[str, Any]], limit: int) -> list[tuple[int, int, str]]:
    watches: list[tuple[int, int, str]] = []
    for index, candidate in enumerate(candidates[:limit]):
        state_base = int(candidate["state_base"], 16)
        fog_ptr = int(candidate["fog_rgb_float_ptr"], 16)
        count = int(candidate["count"])
        watches.append(
            (
                fog_ptr,
                12,
                "title_intro_material_scalar_fog_rgb_float "
                f"candidate={index} state_base={fmt_u32(state_base)} count={count}",
            )
        )
        watches.append(
            (
                state_base,
                0x90,
                "title_intro_material_scalar_submit_state "
                f"candidate={index} fog_rgb_float_ptr={fmt_u32(fog_ptr)} count={count}",
            )
        )
    return sorted(watches, key=lambda item: (item[0], item[1], item[2]))


def write_watchlist(path: Path, watches: list[tuple[int, int, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# OOT3D title intro material-scalar fog payload validation watchlist.",
        "# Generated from GPUREG_FOG_COLOR writer-trace register state; validation only.",
    ]
    for address, size, label in watches:
        lines.append(f"{fmt_u32(address)} {size} # {label}")
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--writer-trace", type=Path, default=DEFAULT_INPUT)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--summary-out", type=Path)
    parser.add_argument("--limit", type=int, default=8)
    args = parser.parse_args()

    candidates = discover_candidates(args.writer_trace)
    watches = build_watches(candidates, max(1, args.limit))
    write_watchlist(args.output, watches)

    summary = {
        "format": "oot3d_title_intro_material_scalar_watchlist_v1",
        "policy": "validation_only_not_runtime_replacement_data",
        "writer_trace": str(args.writer_trace),
        "output": str(args.output),
        "candidate_count": len(candidates),
        "watch_count": len(watches),
        "selected_candidates": candidates[: max(1, args.limit)],
    }
    if args.summary_out is not None:
        args.summary_out.parent.mkdir(parents=True, exist_ok=True)
        args.summary_out.write_text(json.dumps(summary, indent=2) + "\n", encoding="ascii")
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
