#!/usr/bin/env python3
"""Build a compact optimization report from the real whole-AOT program."""

from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path

from whole_aot_cpp import _load_functions
from whole_aot_optimization_ir import (
    Flag,
    analyze_functions,
    flag_names,
    register_names,
)


FORMAT = "oot3d_whole_aot_optimization_report_v1"


def _mask_names(mask: Flag) -> list[str]:
    return list(flag_names(mask))


def analyze(program_path: Path, selection_path: Path, code_path: Path) -> dict:
    program = json.loads(program_path.read_text(encoding="utf-8"))
    selection = json.loads(selection_path.read_text(encoding="utf-8"))
    functions, _ = _load_functions(program, selection, code_path.read_bytes())
    plans = analyze_functions(functions)

    register_frequency: Counter[str] = Counter()
    flag_frequency: Counter[str] = Counter()
    instructions = 0
    flag_producers = 0
    dead_producers = 0
    partial_producers = 0
    barriers = 0
    observable_exits = 0
    loop_regions = 0
    loop_blocks = 0
    dirty_exit_gprs = 0
    dirty_exit_flags = 0
    for plan in plans:
        register_frequency.update(register_names(plan.promoted_gprs))
        flag_frequency.update(flag_names(plan.promoted_flags))
        for region in plan.regions:
            if region.is_loop:
                loop_regions += 1
                loop_blocks += len(region.blocks)
        for block in plan.blocks:
            observable_exits += int(block.observable_exit)
            if block.observable_exit:
                dirty_exit_gprs += len(register_names(block.dirty_gprs_out))
                dirty_exit_flags += len(flag_names(block.dirty_flags_out))
            for item in block.instructions:
                instructions += 1
                barriers += int(item.effects.barrier)
                if item.effects.flag_writes:
                    flag_producers += 1
                    dead_producers += int(item.required_flag_writes == Flag.NONE)
                    partial_producers += int(
                        item.required_flag_writes != Flag.NONE
                        and item.required_flag_writes != item.effects.flag_writes
                    )

    hot_candidates = sorted(
        plans,
        key=lambda plan: (
            -sum(len(region.blocks) for region in plan.regions if region.is_loop),
            -sum(len(block.instructions) for block in plan.blocks),
            plan.entry,
        ),
    )[:32]
    return {
        "format": FORMAT,
        "inputs": {
            "program": str(program_path),
            "selection": str(selection_path),
            "code": str(code_path),
        },
        "summary": {
            "functions": len(plans),
            "blocks": sum(len(plan.blocks) for plan in plans),
            "instructions": instructions,
            "observable_exit_blocks": observable_exits,
            "loop_regions": loop_regions,
            "loop_blocks": loop_blocks,
            "conservative_barrier_instructions": barriers,
            "flag_producers": flag_producers,
            "fully_dead_flag_producers": dead_producers,
            "partially_live_flag_producers": partial_producers,
            "dirty_gprs_across_observable_exits": dirty_exit_gprs,
            "dirty_flags_across_observable_exits": dirty_exit_flags,
        },
        "promotion_frequency": {
            "gpr": dict(sorted(register_frequency.items())),
            "flag": dict(sorted(flag_frequency.items())),
        },
        "region_candidates": [
            {
                "entry": plan.entry,
                "entry_hex": f"0x{plan.entry:08X}",
                "name": plan.name,
                "blocks": len(plan.blocks),
                "instructions": sum(len(block.instructions) for block in plan.blocks),
                "loop_regions": sum(region.is_loop for region in plan.regions),
                "loop_blocks": sum(
                    len(region.blocks) for region in plan.regions if region.is_loop
                ),
                "promoted_gprs": list(register_names(plan.promoted_gprs)),
                "promoted_flags": _mask_names(plan.promoted_flags),
                "dead_flag_producers": plan.dead_flag_write_count,
                "partial_flag_producers": plan.partial_flag_write_count,
            }
            for plan in hot_candidates
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--program", type=Path, required=True)
    parser.add_argument("--selection", type=Path, required=True)
    parser.add_argument("--code", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = analyze(args.program, args.selection, args.code)
    payload = json.dumps(report, indent=2) + "\n"
    if args.output is None:
        print(payload, end="")
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(payload, encoding="utf-8")
        print(json.dumps(report["summary"], sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
