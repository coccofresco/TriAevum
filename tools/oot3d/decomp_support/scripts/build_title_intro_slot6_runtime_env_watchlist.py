#!/usr/bin/env python3
"""Build a title-intro slot 6 runtime environment validation watchlist.

The watchlist observes OOT3D PlayState-relative fields that feed the native
environment light/fog path. It is validation evidence only: captured values must
not be copied into the engine as replacement data.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


DEFAULT_OUTPUT = Path(
    "captures/azahar_pica/derived/watch_addresses.title_intro_slot6_runtime_env.txt"
)
RUNTIME_ENV_STATE_OFFSET = 0x3190
RUNTIME_OUTPUT_LIGHT_CONTEXT_OFFSET = 0x0A70


def fmt_u32(value: int) -> str:
    return f"0x{value & 0xFFFFFFFF:08X}"


def add_watch(watches: list[tuple[int, int, str]], address: int, size: int, label: str) -> None:
    watches.append((address, size, label))


def build_watches(play_base: int) -> list[tuple[int, int, str]]:
    owner_base = play_base + RUNTIME_ENV_STATE_OFFSET
    watches: list[tuple[int, int, str]] = []

    add_watch(
        watches,
        owner_base + 0x20,
        0x30,
        "title_intro_runtime_env_transition_modes "
        f"owner_base={fmt_u32(owner_base)} play_base={fmt_u32(play_base)} "
        "mode=+0x21 current=+0x22 target=+0x23 counter=+0x24 duration=+0x26 blend=+0x28",
    )
    add_watch(
        watches,
        owner_base + 0x6C,
        0x12,
        "title_intro_runtime_color_addends_i16 "
        "ambient=+0x6c light=+0x72 fog=+0x78",
    )
    add_watch(
        watches,
        owner_base + 0xB2,
        0x12,
        "title_intro_runtime_preaddend_rgb_u8 "
        "ambient=+0xb2 light0=+0xb8 light1=+0xbe fog=+0xc1",
    )
    add_watch(
        watches,
        play_base + RUNTIME_OUTPUT_LIGHT_CONTEXT_OFFSET + 0x0E,
        0x10,
        "title_intro_runtime_final_light_context_u8 "
        "ambient=play+0xa7e fog=play+0xa82",
    )
    add_watch(
        watches,
        play_base + 0x31B0,
        0x10,
        "title_intro_play_transition_control_fields "
        "state=play+0x31b0 current=+0x31b1 previous=+0x31b2 target=+0x31b3 "
        "counter=+0x31b4 duration=+0x31b6",
    )
    add_watch(
        watches,
        play_base + 0x3230,
        0x30,
        "title_intro_play_light_indices_and_table "
        "list=play+0x3230 current=+0x3235 previous=+0x3236 target=+0x3237 blend=+0x3258",
    )
    add_watch(
        watches,
        play_base + 0x326E,
        0x08,
        "title_intro_play_environment_draw_flags "
        "env_brightness=+0x326e fog_or_kankyo_gate=+0x326f runtime_flag=+0x3271",
    )

    return sorted(watches, key=lambda item: (item[0], item[1], item[2]))


def write_watchlist(path: Path, watches: list[tuple[int, int, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# OOT3D title intro slot 6 runtime environment validation watchlist.",
        "# Generated from a native PlayState/global-context base; validation only.",
    ]
    for address, size, label in watches:
        lines.append(f"{fmt_u32(address)} {size} # {label}")
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    base_group = parser.add_mutually_exclusive_group(required=True)
    base_group.add_argument(
        "--play-base",
        "--global-context",
        dest="play_base",
        type=lambda text: int(text, 0),
        help="OOT3D PlayState/global-context base captured from the title-intro slot 6 run",
    )
    base_group.add_argument(
        "--owner-base",
        type=lambda text: int(text, 0),
        help="runtime environment state base, equal to PlayState + 0x3190",
    )
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--summary-out", type=Path)
    args = parser.parse_args()

    play_base = args.play_base
    if play_base is None:
        play_base = int(args.owner_base) - RUNTIME_ENV_STATE_OFFSET
    owner_base = play_base + RUNTIME_ENV_STATE_OFFSET

    watches = build_watches(play_base)
    write_watchlist(args.output, watches)

    summary = {
        "format": "oot3d_title_intro_slot6_runtime_env_watchlist_v1",
        "policy": "validation_only_not_runtime_replacement_data",
        "output": str(args.output),
        "play_base": fmt_u32(play_base),
        "owner_base": fmt_u32(owner_base),
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
