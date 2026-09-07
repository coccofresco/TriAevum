#!/usr/bin/env python3
"""Probe whether the ARM toolchain can emit the OOT3D cmpne state gate."""

from __future__ import annotations

import argparse
import csv
import json
import os
import subprocess
from pathlib import Path
from typing import Any

from compare_direct_packet_probe_matches import read_objdump_file
from compare_runtime_objects import compare_ops
from probe_direct_packet_compilability import compile_command, find_tool, rel


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BUILD = ROOT / "build" / "direct_state_gate_codegen_probes"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_state_gate_codegen_probe.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_state_gate_codegen_probe.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_state_gate_codegen_probe.md"

TARGET_GATE_OPS = [
    "ldr r0,[literal]",
    "ldrsb r0,[r0,#9]",
    "cmp r0,#10",
    "cmpne r0,#11",
    "cmpne r0,#12",
    "beq <target>",
    "cmp r0,#13",
    "bne <target>",
]

PRELUDE = r"""
typedef signed char s8;
typedef int s32;

extern volatile s8 oot3d_boss_va_cs_state_bytes[16];
void StateGateHit(s32 value);

#define STATE_VALUE() ((s32)oot3d_boss_va_cs_state_bytes[9])
#define REG_BARRIER(value) ({ s32 _v = (value); __asm__ volatile("" : "+r"(_v)); _v; })
"""

VARIANTS: dict[str, tuple[str, bool]] = {
    "or_chain": (
        r"""
void StateGateProbe(void) {
    s32 state = STATE_VALUE();
    if (state == 10 || state == 11 || state == 12) {
        StateGateHit(1);
    } else if (state == 13) {
        StateGateHit(2);
    }
}
""",
        True,
    ),
    "negative_and_chain": (
        r"""
void StateGateProbe(void) {
    s32 state = STATE_VALUE();
    if (state != 10 && state != 11 && state != 12) {
        if (state == 13) {
            StateGateHit(2);
        }
    } else {
        StateGateHit(1);
    }
}
""",
        True,
    ),
    "nested_negative_goto": (
        r"""
void StateGateProbe(void) {
    s32 state = STATE_VALUE();
    if (state != 10) {
        if (state != 11) {
            if (state != 12) {
                if (state == 13) {
                    StateGateHit(2);
                }
                return;
            }
        }
    }
    StateGateHit(1);
}
""",
        True,
    ),
    "volatile_state": (
        r"""
void StateGateProbe(void) {
    volatile s32 state = STATE_VALUE();
    if (state == 10 || state == 11 || state == 12) {
        StateGateHit(1);
    } else if (state == 13) {
        StateGateHit(2);
    }
}
""",
        True,
    ),
    "barrier_or_chain": (
        r"""
void StateGateProbe(void) {
    s32 state = STATE_VALUE();
    if (state == 10 || REG_BARRIER(state) == 11 || REG_BARRIER(state) == 12) {
        StateGateHit(1);
    } else if (state == 13) {
        StateGateHit(2);
    }
}
""",
        True,
    ),
    "inline_asm_cmpne_gate": (
        r"""
void StateGateProbe(void) {
    s32 state = STATE_VALUE();
    __asm__ volatile(
        "cmp %[state], #10\n\t"
        "cmpne %[state], #11\n\t"
        "cmpne %[state], #12\n\t"
        "beq 1f\n\t"
        "cmp %[state], #13\n\t"
        "bne 2f\n\t"
        "mov r0, #2\n\t"
        "bl StateGateHit\n\t"
        "b 2f\n\t"
        "1:\n\t"
        "mov r0, #1\n\t"
        "bl StateGateHit\n\t"
        "2:\n\t"
        :
        : [state] "r"(state)
        : "r0", "lr", "cc", "memory");
}
""",
        False,
    ),
}

PROFILES: dict[str, tuple[str, tuple[str, ...]]] = {
    "o2": ("-O2", ()),
    "o1": ("-O1", ()),
    "os": ("-Os", ()),
    "o3": ("-O3", ()),
    "o2_no_reorder": ("-O2", ("-fno-reorder-blocks", "-fno-reorder-functions")),
    "o2_no_gcse": ("-O2", ("-fno-gcse", "-fno-cse-follow-jumps")),
    "o2_no_if_conversion": ("-O2", ("-fno-if-conversion", "-fno-if-conversion2")),
    "o2_force_if_conversion": ("-O2", ("-fif-conversion", "-fif-conversion2")),
    "o2_no_vrp": ("-O2", ("-fno-tree-vrp", "-fno-tree-ccp")),
    "o2_no_fold": ("-O2", ("-fno-tree-vrp", "-fno-tree-ccp", "-fno-tree-dominator-opts", "-fno-tree-forwprop")),
}


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")


def write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})


def float_value(value: Any) -> float:
    try:
        return float(value or 0.0)
    except (TypeError, ValueError):
        return 0.0


def int_value(value: Any) -> int:
    try:
        return int(value or 0)
    except (TypeError, ValueError):
        return 0


def compile_profile_command(
    gcc: str,
    source: Path,
    obj: Path,
    optimization: str,
    extra_flags: tuple[str, ...],
) -> list[str]:
    command = compile_command(gcc, source, obj, optimization)
    command[3:3] = list(extra_flags)
    return command


def compile_variant(
    args: argparse.Namespace,
    gcc: str,
    objdump: str,
    variant: str,
    source_text: str,
    pure_c: bool,
    profile: str,
    optimization: str,
    extra_flags: tuple[str, ...],
) -> dict[str, Any]:
    source_dir = args.build_dir / "sources"
    object_dir = args.build_dir / "objects"
    dump_dir = args.build_dir / "dumps"
    log_dir = args.build_dir / "logs"
    for directory in (source_dir, object_dir, dump_dir, log_dir):
        directory.mkdir(parents=True, exist_ok=True)

    stem = f"{variant}_{profile}"
    source = source_dir / f"{stem}.c"
    obj = object_dir / f"{stem}.o"
    dump = dump_dir / f"{stem}.dump"
    stderr_log = log_dir / f"{stem}.stderr.txt"
    source.write_text(PRELUDE + "\n" + source_text.strip() + "\n", encoding="utf-8", newline="\n")
    for stale in (obj, dump):
        if stale.is_file():
            stale.unlink()

    env = os.environ.copy()
    env["PATH"] = str(Path(gcc).parent) + os.pathsep + env.get("PATH", "")
    completed = subprocess.run(
        compile_profile_command(gcc, source, obj, optimization, extra_flags),
        cwd=ROOT,
        text=True,
        capture_output=True,
        env=env,
    )
    stderr_log.write_text(completed.stderr, encoding="utf-8", newline="\n")
    compiled = completed.returncode == 0 and obj.is_file()
    dumped = False
    if compiled:
        dumped_result = subprocess.run([objdump, "-dr", str(obj)], cwd=ROOT, text=True, capture_output=True, env=env)
        dump.write_text(dumped_result.stdout, encoding="utf-8", newline="\n")
        dumped = dumped_result.returncode == 0 and dump.is_file()
    return {
        "variant": variant,
        "profile": profile,
        "optimization": optimization,
        "extra_flags": " ".join(extra_flags),
        "pure_c": pure_c,
        "symbol": "StateGateProbe",
        "source": rel(source),
        "object": rel(obj),
        "dump": rel(dump),
        "stderr_log": rel(stderr_log),
        "returncode": completed.returncode,
        "compiled": compiled,
        "dumped": dumped,
    }


def branch_shape(ops: list[str]) -> str:
    text = " ".join(ops)
    if "cmpne" in text:
        return "cmpne-chain"
    if "sub " in text and ("bls <target>" in text or "bhi <target>" in text):
        return "subtract-range-check"
    if "ble <target>" in text or "bgt <target>" in text:
        return "range-check"
    if sum(1 for op in ops if op.startswith("beq ")) >= 2:
        return "explicit-eq-chain"
    if any(op.startswith("beq ") for op in ops):
        return "single-eq-branch"
    return "other"


def gate_ops(compiled_ops: list[str]) -> list[str]:
    if not compiled_ops:
        return []
    end = min(len(compiled_ops), 18)
    for index, op in enumerate(compiled_ops[:end]):
        if op.startswith("bl ") and index > 3:
            return compiled_ops[:index]
    return compiled_ops[:end]


def compare_row(row: dict[str, Any]) -> dict[str, Any]:
    if not row.get("dumped"):
        return {**row, "branch_shape": "compile-blocked"}
    compiled_ops = list(read_objdump_file(ROOT / str(row["dump"])).get("StateGateProbe", {}).get("ops", []))
    ops = gate_ops(compiled_ops)
    compare = compare_ops(TARGET_GATE_OPS, ops) if ops else {}
    shape = branch_shape(ops)
    return {
        **row,
        "compiled_gate_instruction_count": len(ops),
        "lcs_instruction_count": int_value(compare.get("lcs_instruction_count")),
        "lcs_target_ratio": float_value(compare.get("lcs_target_ratio")),
        "branch_shape": shape,
        "cmpne_chain_recovered": shape == "cmpne-chain",
        "range_check_avoided": shape not in {"range-check", "subtract-range-check"},
        "next_gate": next_gate(shape, bool(row.get("pure_c"))),
    }


def next_gate(shape: str, pure_c: bool) -> str:
    if shape == "cmpne-chain" and pure_c:
        return "Promote this pure-C form back into the BossVa structured probe."
    if shape == "cmpne-chain":
        return "cmpne is reachable only through inline assembly in this probe set."
    if shape in {"range-check", "subtract-range-check"}:
        return "Compiler range folding remains; keep as negative C evidence."
    return "Range folding avoided, but conditional cmpne was not recovered."


def build_report(args: argparse.Namespace) -> dict[str, Any]:
    gcc = find_tool("gcc", args.tool_prefix)
    objdump = find_tool("objdump", args.tool_prefix)
    rows: list[dict[str, Any]] = []
    for variant, (source_text, pure_c) in VARIANTS.items():
        for profile, (optimization, extra_flags) in PROFILES.items():
            rows.append(
                compare_row(
                    compile_variant(args, gcc, objdump, variant, source_text, pure_c, profile, optimization, extra_flags)
                )
            )
    rows.sort(
        key=lambda row: (
            bool(row.get("pure_c")),
            bool(row.get("cmpne_chain_recovered")),
            bool(row.get("range_check_avoided")),
            float_value(row.get("lcs_target_ratio")),
        ),
        reverse=True,
    )
    pure_c_rows = [row for row in rows if row.get("pure_c")]
    best_pure_c = next((row for row in rows if row.get("pure_c")), {})
    summary = {
        "rows": len(rows),
        "compiled": sum(1 for row in rows if row.get("compiled")),
        "pure_c_rows": len(pure_c_rows),
        "pure_c_cmpne_chain_recovered_rows": sum(1 for row in pure_c_rows if row.get("cmpne_chain_recovered")),
        "pure_c_range_check_avoided_rows": sum(1 for row in pure_c_rows if row.get("range_check_avoided")),
        "inline_asm_cmpne_chain_recovered_rows": sum(
            1 for row in rows if not row.get("pure_c") and row.get("cmpne_chain_recovered")
        ),
        "best_pure_c_variant": best_pure_c.get("variant", ""),
        "best_pure_c_profile": best_pure_c.get("profile", ""),
        "best_pure_c_branch_shape": best_pure_c.get("branch_shape", ""),
        "best_pure_c_lcs_target_ratio": best_pure_c.get("lcs_target_ratio", 0),
        "next_gate": (
            "Promote a pure-C cmpne state-gate probe."
            if any(row.get("cmpne_chain_recovered") for row in pure_c_rows)
            else "Pure C did not recover cmpne; use this as evidence for an assembly-shaped state-gate adapter."
        ),
    }
    return {
        "format": "oot3d_direct_state_gate_codegen_probe_v1",
        "inputs": {"target_gate_ops": TARGET_GATE_OPS},
        "summary": summary,
        "rows": rows,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct State Gate Codegen Probe",
        "",
        "This report isolates whether the ARM toolchain can emit the target `cmpne` state gate.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Rows | {summary['rows']} |",
        f"| Compiled | {summary['compiled']} |",
        f"| Pure-C rows | {summary['pure_c_rows']} |",
        f"| Pure-C cmpne recovered | {summary['pure_c_cmpne_chain_recovered_rows']} |",
        f"| Pure-C range-check avoided | {summary['pure_c_range_check_avoided_rows']} |",
        f"| Inline-asm cmpne recovered | {summary['inline_asm_cmpne_chain_recovered_rows']} |",
        f"| Best pure-C variant | `{summary['best_pure_c_variant']}` |",
        f"| Best pure-C profile | `{summary['best_pure_c_profile']}` |",
        f"| Best pure-C branch shape | `{summary['best_pure_c_branch_shape']}` |",
        f"| Best pure-C LCS | {float_value(summary['best_pure_c_lcs_target_ratio']):.4f} |",
        "",
        "## Next Gate",
        "",
        summary["next_gate"],
        "",
        "## Rows",
        "",
        "| Variant | Profile | Pure C | Shape | cmpne | Avoids range | LCS | Next gate |",
        "| --- | --- | --- | --- | --- | --- | ---: | --- |",
    ]
    for row in data["rows"]:
        lines.append(
            f"| `{row['variant']}` | `{row['profile']}` | {row['pure_c']} | `{row.get('branch_shape', '')}` | "
            f"{row.get('cmpne_chain_recovered', False)} | {row.get('range_check_avoided', False)} | "
            f"{float_value(row.get('lcs_target_ratio')):.4f} | {row.get('next_gate', '')} |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--tool-prefix", default="arm-none-eabi")
    args = parser.parse_args()
    data = build_report(args)
    fields = [
        "variant",
        "profile",
        "optimization",
        "extra_flags",
        "pure_c",
        "compiled",
        "dumped",
        "compiled_gate_instruction_count",
        "lcs_instruction_count",
        "lcs_target_ratio",
        "branch_shape",
        "cmpne_chain_recovered",
        "range_check_avoided",
        "next_gate",
        "source",
        "object",
        "dump",
        "stderr_log",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct state gate codegen probe: "
        f"{summary['pure_c_cmpne_chain_recovered_rows']} pure-C cmpne rows, "
        f"{summary['inline_asm_cmpne_chain_recovered_rows']} inline-asm cmpne rows"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
