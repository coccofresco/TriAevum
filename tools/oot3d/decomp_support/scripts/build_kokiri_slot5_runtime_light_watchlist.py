#!/usr/bin/env python3
"""Build a Kokiri slot 5 runtime-light validation watchlist.

The resulting Azahar watchlist is validation evidence only. It follows the
previously resolved native 0x0045DD50 owner address from the actor/VS payload
source probe and expands that to the native PlayState-relative lighting fields.
It must not be used as runtime replacement data by the engine.
"""

from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path
from typing import Any


DEFAULT_PAYLOAD_PROBE = Path(
    "captures/azahar_pica/kokiri_slot5_after10s_actor_vs_payload_source_probe_20260705_080000/"
    "derived/actor_vs_payload_source_probe.json"
)
DEFAULT_OUTPUT = Path("captures/azahar_pica/derived/watch_addresses.kokiri_slot5_runtime_light_outputs.txt")
RUNTIME_ENV_STATE_OFFSET = 0x3190
RUNTIME_OUTPUT_LIGHT_CONTEXT_OFFSET = 0x0A70


def parse_int(value: Any) -> int:
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        return int(value, 0)
    raise ValueError(f"not an integer-like value: {value!r}")


def fmt_u32(value: int) -> str:
    return f"0x{value & 0xFFFFFFFF:08X}"


def load_owner_base(path: Path) -> int:
    data = json.loads(path.read_text(encoding="utf-8"))
    owners: Counter[int] = Counter()
    for payload in data.get("compact_payloads", []):
        owner = payload.get("owner_base_from_r4")
        if owner is not None:
            owners[parse_int(owner)] += 1
    if not owners:
        raise SystemExit(f"no owner_base_from_r4 values found in {path}")
    return owners.most_common(1)[0][0]


def add_watch(watches: list[tuple[int, int, str]], address: int, size: int, label: str) -> None:
    watches.append((address, size, label))


def build_watches(owner_base: int) -> list[tuple[int, int, str]]:
    play_base = owner_base - RUNTIME_ENV_STATE_OFFSET
    watches: list[tuple[int, int, str]] = []

    add_watch(
        watches,
        owner_base + 0x20,
        0x30,
        "runtime_env_state_modes_and_compact_payloads "
        f"owner_base={fmt_u32(owner_base)} play_base={fmt_u32(play_base)}",
    )
    add_watch(
        watches,
        owner_base + 0x6C,
        0x12,
        "runtime_light_color_addends_halfwords "
        "ambient=+0x6c light=+0x72 fog=+0x78",
    )
    add_watch(
        watches,
        owner_base + 0xB2,
        0x12,
        "runtime_light_preaddend_bytes "
        "ambient=+0xb2 light0=+0xb8 light1=+0xbe fog=+0xc1",
    )
    add_watch(
        watches,
        owner_base + 0x30,
        0x1E,
        "runtime_actor_vs_compact_payload_output_slots "
        "slot0=+0x30 slot1=+0x48",
    )
    add_watch(
        watches,
        play_base + RUNTIME_OUTPUT_LIGHT_CONTEXT_OFFSET + 0x0E,
        0x10,
        "runtime_final_light_context_bytes "
        "ambient=play+0xa7e fog=play+0xa82",
    )
    add_watch(
        watches,
        play_base + 0x3230,
        0x30,
        "runtime_light_state_indices_and_list_pointer "
        "list=play+0x3230 current=+0x3235 previous=+0x3236 target=+0x3237 blend=+0x3258",
    )

    return sorted(watches, key=lambda item: (item[0], item[1], item[2]))


def write_watchlist(path: Path, watches: list[tuple[int, int, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# OOT3D Kokiri slot 5 runtime-light validation watchlist.",
        "# Generated from a native 0x0045DD50 owner-base probe; validation only.",
    ]
    for address, size, label in watches:
        lines.append(f"{fmt_u32(address)} {size} # {label}")
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--payload-probe", type=Path, default=DEFAULT_PAYLOAD_PROBE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--owner-base", type=lambda text: int(text, 0), default=None)
    parser.add_argument("--summary-out", type=Path, default=None)
    args = parser.parse_args()

    owner_base = args.owner_base if args.owner_base is not None else load_owner_base(args.payload_probe)
    play_base = owner_base - RUNTIME_ENV_STATE_OFFSET
    watches = build_watches(owner_base)
    write_watchlist(args.output, watches)

    summary = {
        "format": "oot3d_kokiri_slot5_runtime_light_watchlist_v1",
        "policy": "validation_only_not_runtime_replacement_data",
        "payload_probe": str(args.payload_probe),
        "output": str(args.output),
        "owner_base": fmt_u32(owner_base),
        "play_base": fmt_u32(play_base),
        "runtime_environment_state_offset": fmt_u32(RUNTIME_ENV_STATE_OFFSET),
        "runtime_output_light_context_offset": fmt_u32(RUNTIME_OUTPUT_LIGHT_CONTEXT_OFFSET),
        "watch_count": len(watches),
        "watches": [
            {"address": fmt_u32(address), "size": size, "label": label}
            for address, size, label in watches
        ],
    }
    if args.summary_out is not None:
        args.summary_out.parent.mkdir(parents=True, exist_ok=True)
        args.summary_out.write_text(json.dumps(summary, indent=2) + "\n", encoding="ascii")
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
