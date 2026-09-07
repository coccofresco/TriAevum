#!/usr/bin/env python3
"""Probe ARM codegen shapes for the BossVa smooth-step tail."""

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
DEFAULT_BUILD = ROOT / "build" / "direct_smooth_tail_codegen_probes"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_smooth_tail_codegen_probe.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_smooth_tail_codegen_probe.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_smooth_tail_codegen_probe.md"

TARGET_TAIL_OPS = [
    "mov r3,#0",
    "str r3,[sp]",
    "ldrh r0,[r4,#190]",
    "ldrh r1,[r4,#188]",
    "ldr r5,[literal]",
    "mov r2,#1",
    "sub r0,r0,r1",
    "mov r3,r5",
    "sxth r1,r0",
    "add r0,r4,#3840",
    "add r0,r0,#246",
    "bl <target>",
    "ldr r0,[r4,#540]",
    "mov r2,#0",
    "add r1,sp,#4",
    "add r0,r0,#156",
    "bl <target>",
    "mov r3,#0",
    "add r0,r4,#3072",
    "str r3,[sp]",
    "ldrsh r1,[sp,#8]",
    "mov r3,r5",
    "mov r2,#1",
    "add r0,r0,#1012",
    "bl <target>",
]

PRELUDE = r"""
typedef signed char s8;
typedef signed short s16;
typedef unsigned short u16;
typedef signed int s32;
typedef unsigned int u32;
typedef unsigned char u8;

typedef struct Vec3s { s16 x; s16 y; s16 z; } Vec3s;

#define S16_AT(base, offset) (*(s16*)((u8*)(base) + (offset)))
#define U16_AT(base, offset) (*(u16*)((u8*)(base) + (offset)))
#define PTR_AT(base, offset) (*(void**)((u8*)(base) + (offset)))
#define REG_BARRIER(value) ({ s32 _v = (value); __asm__ volatile("" : "+r"(_v)); _v; })
#define PTR_BARRIER(value) ({ void* _p = (void*)(value); __asm__ volatile("" : "+r"(_p)); _p; })

void Math_SmoothStepToS(s16* value, s16 target, s32 scale, s16 maxStep, s16 minStep);
void BossVa_ReadJointRot(void* joint, Vec3s* out, s32 mode);
"""

VARIANTS: dict[str, tuple[str, bool]] = {
    "direct_offsets": (
        r"""
void SmoothTailProbe(void* this) {
    Vec3s jointRot;
    Math_SmoothStepToS(&S16_AT(this, 0xF6), (s16)((s32)U16_AT(this, 0xBE) - (s32)U16_AT(this, 0xBC)), 1, 0x2EE, 0);
    BossVa_ReadJointRot((u8*)PTR_AT(this, 0x21C) + 0x9C, &jointRot, 0);
    Math_SmoothStepToS(&S16_AT(this, 0x3F4), jointRot.z, 1, 0x2EE, 0);
}
""",
        True,
    ),
    "split_base": (
        r"""
void SmoothTailProbe(void* this) {
    Vec3s jointRot;
    void* rotBase = PTR_BARRIER((u8*)this + 0xF00);
    void* jointBase = PTR_BARRIER((u8*)this + 0xC00);
    Math_SmoothStepToS(&S16_AT(rotBase, 0xF6), (s16)((s32)U16_AT(this, 0xBE) - (s32)U16_AT(this, 0xBC)), 1, 0x2EE, 0);
    BossVa_ReadJointRot((u8*)PTR_AT(this, 0x21C) + 0x9C, &jointRot, 0);
    Math_SmoothStepToS(&S16_AT(jointBase, 0x3F4), jointRot.z, 1, 0x2EE, 0);
}
""",
        True,
    ),
    "split_base_order_locals": (
        r"""
void SmoothTailProbe(void* this) {
    Vec3s jointRot;
    void* rotBase = PTR_BARRIER((u8*)this + 0xF00);
    void* jointBase = PTR_BARRIER((u8*)this + 0xC00);
    u16 shapeRotY = U16_AT(this, 0xBE);
    u16 shapeRotX = U16_AT(this, 0xBC);
    Math_SmoothStepToS(&S16_AT(rotBase, 0xF6), (s16)((s32)shapeRotY - (s32)shapeRotX), 1, 0x2EE, 0);
    BossVa_ReadJointRot((u8*)PTR_AT(this, 0x21C) + 0x9C, &jointRot, 0);
    Math_SmoothStepToS(&S16_AT(jointBase, 0x3F4), jointRot.z, 1, 0x2EE, 0);
}
""",
        True,
    ),
    "split_base_local_zero": (
        r"""
void SmoothTailProbe(void* this) {
    Vec3s jointRot;
    void* rotBase = PTR_BARRIER((u8*)this + 0xF00);
    void* jointBase = PTR_BARRIER((u8*)this + 0xC00);
    Math_SmoothStepToS(&S16_AT(rotBase, 0xF6), (s16)((s32)U16_AT(this, 0xBE) - (s32)U16_AT(this, 0xBC)), 1, 0x2EE, REG_BARRIER(0));
    BossVa_ReadJointRot((u8*)PTR_AT(this, 0x21C) + 0x9C, &jointRot, REG_BARRIER(0));
    Math_SmoothStepToS(&S16_AT(jointBase, 0x3F4), jointRot.z, 1, 0x2EE, REG_BARRIER(0));
}
""",
        True,
    ),
    "register_rot_loads": (
        r"""
void SmoothTailProbe(void* this) {
    Vec3s jointRot;
    void* rotBase = PTR_BARRIER((u8*)this + 0xF00);
    void* jointBase = PTR_BARRIER((u8*)this + 0xC00);
    register s32 shapeRotY __asm__("r0") = U16_AT(this, 0xBE);
    register s32 shapeRotX __asm__("r1") = U16_AT(this, 0xBC);
    register s32 rotDelta __asm__("r0") = shapeRotY - shapeRotX;
    Math_SmoothStepToS(&S16_AT(rotBase, 0xF6), (s16)rotDelta, 1, 0x2EE, REG_BARRIER(0));
    BossVa_ReadJointRot((u8*)PTR_AT(this, 0x21C) + 0x9C, &jointRot, REG_BARRIER(0));
    Math_SmoothStepToS(&S16_AT(jointBase, 0x3F4), jointRot.z, 1, 0x2EE, REG_BARRIER(0));
}
""",
        True,
    ),
    "asm_rot_setup_control": (
        r"""
void SmoothTailProbe(void* this) {
    Vec3s jointRot;
    __asm__ volatile(
        "mov r4, %[self]\n\t"
        "mov r3, #0\n\t"
        "str r3, [sp]\n\t"
        "ldrh r0, [r4, #190]\n\t"
        "ldrh r1, [r4, #188]\n\t"
        "ldr r5, =0x2EE\n\t"
        "mov r2, #1\n\t"
        "sub r0, r0, r1\n\t"
        "mov r3, r5\n\t"
        "sxth r1, r0\n\t"
        "add r0, r4, #3840\n\t"
        "add r0, r0, #246\n\t"
        "bl Math_SmoothStepToS\n\t"
        :
        : [self] "r"(this)
        : "r0", "r1", "r2", "r3", "r4", "r5", "lr", "cc", "memory");
    BossVa_ReadJointRot((u8*)PTR_AT(this, 0x21C) + 0x9C, &jointRot, 0);
    Math_SmoothStepToS(&S16_AT((u8*)this + 0xC00, 0x3F4), jointRot.z, 1, 0x2EE, 0);
}
""",
        False,
    ),
    "naked_full_tail_adapter": (
        r"""
__attribute__((naked)) void SmoothTailProbe(void* this) {
    __asm__ volatile(
        "push {r4, r5, lr}\n\t"
        "sub sp, sp, #12\n\t"
        "mov r4, r0\n\t"
        "mov r3, #0\n\t"
        "str r3, [sp]\n\t"
        "ldrh r0, [r4, #190]\n\t"
        "ldrh r1, [r4, #188]\n\t"
        "ldr r5, =0x2EE\n\t"
        "mov r2, #1\n\t"
        "sub r0, r0, r1\n\t"
        "mov r3, r5\n\t"
        "sxth r1, r0\n\t"
        "add r0, r4, #3840\n\t"
        "add r0, r0, #246\n\t"
        "bl Math_SmoothStepToS\n\t"
        "ldr r0, [r4, #540]\n\t"
        "mov r2, #0\n\t"
        "add r1, sp, #4\n\t"
        "add r0, r0, #156\n\t"
        "bl BossVa_ReadJointRot\n\t"
        "mov r3, #0\n\t"
        "add r0, r4, #3072\n\t"
        "str r3, [sp]\n\t"
        "ldrsh r1, [sp, #8]\n\t"
        "mov r3, r5\n\t"
        "mov r2, #1\n\t"
        "add r0, r0, #1012\n\t"
        "bl Math_SmoothStepToS\n\t"
        "add sp, sp, #12\n\t"
        "pop {r4, r5, pc}\n\t"
    );
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
    "o2_no_schedule": ("-O2", ("-fno-schedule-insns", "-fno-schedule-insns2")),
    "o2_no_reorder": ("-O2", ("-fno-reorder-blocks", "-fno-reorder-functions")),
    "o2_no_gcse": ("-O2", ("-fno-gcse", "-fno-cse-follow-jumps")),
    "o2_no_if_conversion": ("-O2", ("-fno-if-conversion", "-fno-if-conversion2")),
    "o2_no_tree_sra": ("-O2", ("-fno-tree-sra", "-fno-ipa-sra")),
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


def bool_value(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    return str(value).strip().lower() in {"1", "true", "yes"}


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


def has_op(ops: list[str], *needles: str) -> bool:
    return any(all(needle in op for needle in needles) for op in ops)


def first_index(ops: list[str], *needles: str) -> int:
    for index, op in enumerate(ops):
        if all(needle in op for needle in needles):
            return index
    return -1


def feature_count(row: dict[str, Any]) -> int:
    features = [
        "no_saved_r6",
        "stack_12",
        "zero_stack_arg_uses_r3",
        "rot_target_load_register_order",
        "rot_target_offset_order",
        "rot_sub_register_shape",
        "max_step_register_shape",
        "rot_split_3840_246",
        "joint_stack_offset_4",
        "joint_split_3072_1012",
    ]
    return sum(1 for feature in features if row.get(feature))


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
        "symbol": "SmoothTailProbe",
        "source": rel(source),
        "object": rel(obj),
        "dump": rel(dump),
        "stderr_log": rel(stderr_log),
        "returncode": completed.returncode,
        "compiled": compiled,
        "dumped": dumped,
    }


def compare_row(row: dict[str, Any]) -> dict[str, Any]:
    if not row.get("dumped"):
        return {**row, "feature_count": 0, "next_gate": "Compile or dump this tail probe before classification."}
    compiled_ops = list(read_objdump_file(ROOT / str(row["dump"])).get("SmoothTailProbe", {}).get("ops", []))
    compare = compare_ops(TARGET_TAIL_OPS, compiled_ops)
    rot_be = first_index(compiled_ops, "ldrh", "[r4,#190]")
    rot_bc = first_index(compiled_ops, "ldrh", "[r4,#188]")
    result = {
        **row,
        "compiled_instruction_count": len(compiled_ops),
        "lcs_instruction_count": int_value(compare.get("lcs_instruction_count")),
        "lcs_target_ratio": float_value(compare.get("lcs_target_ratio")),
        "first_difference": first_difference(compare),
        "no_saved_r6": not has_op(compiled_ops, "push", "r6") and not has_op(compiled_ops, "stmdb", "r6"),
        "stack_12": has_op(compiled_ops, "sub sp,sp,#12"),
        "zero_stack_arg_uses_r3": has_op(compiled_ops, "str r3,[sp]"),
        "rot_target_load_register_order": has_op(compiled_ops, "ldrh r0,[r4,#190]")
        and has_op(compiled_ops, "ldrh r1,[r4,#188]"),
        "rot_target_offset_order": rot_be >= 0 and rot_bc >= 0 and rot_be < rot_bc,
        "rot_sub_register_shape": has_op(compiled_ops, "sub r0,r0,r1"),
        "max_step_register_shape": has_op(compiled_ops, "mov r3,r5"),
        "rot_split_3840_246": has_op(compiled_ops, "add ", "r4,#3840") and has_op(compiled_ops, "add ", "#246"),
        "joint_stack_offset_4": has_op(compiled_ops, "add r1,sp,#4"),
        "joint_split_3072_1012": has_op(compiled_ops, "add ", "r4,#3072") and has_op(compiled_ops, "add ", "#1012"),
    }
    result["feature_count"] = feature_count(result)
    result["next_gate"] = next_gate(result)
    return result


def first_difference(compare: dict[str, Any]) -> str:
    diff = compare.get("first_difference")
    if not isinstance(diff, dict):
        return ""
    return f"{diff.get('index')}: {diff.get('target')} != {diff.get('compiled')}"


def next_gate(row: dict[str, Any]) -> str:
    if row.get("rot_target_load_register_order") and row.get("no_saved_r6"):
        return "Promote this tail shape back into the full BossVa structured probe."
    if row.get("rot_target_load_register_order"):
        return "Exact rotation load registers are reachable; next gate is removing saved r6/stack-frame delta."
    if row.get("no_saved_r6"):
        return "No-r6 frame is reachable; next gate is rotation load/subtract register order."
    return "Keep probing register allocation: exact rot r0/r1 shape and no-r6 frame are not both recovered."


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
            bool_value(row.get("pure_c")),
            int_value(row.get("feature_count")),
            float_value(row.get("lcs_target_ratio")),
        ),
        reverse=True,
    )
    pure_c_rows = [row for row in rows if row.get("pure_c")]
    overall_rows = sorted(
        rows,
        key=lambda row: (
            int_value(row.get("feature_count")),
            float_value(row.get("lcs_target_ratio")),
            bool_value(row.get("pure_c")),
        ),
        reverse=True,
    )
    best = rows[0] if rows else {}
    best_pure_c = pure_c_rows[0] if pure_c_rows else {}
    best_overall = overall_rows[0] if overall_rows else {}
    exact_adapter_rows = [
        row
        for row in rows
        if not row.get("pure_c")
        and int_value(row.get("feature_count")) == 10
        and float_value(row.get("lcs_target_ratio")) >= 1.0
    ]
    summary = {
        "rows": len(rows),
        "compiled": sum(1 for row in rows if row.get("compiled")),
        "pure_c_rows": len(pure_c_rows),
        "best_variant": best.get("variant", ""),
        "best_profile": best.get("profile", ""),
        "best_feature_count": best.get("feature_count", 0),
        "best_lcs_target_ratio": best.get("lcs_target_ratio", 0),
        "best_pure_c_variant": best_pure_c.get("variant", ""),
        "best_pure_c_profile": best_pure_c.get("profile", ""),
        "best_pure_c_feature_count": best_pure_c.get("feature_count", 0),
        "best_overall_variant": best_overall.get("variant", ""),
        "best_overall_profile": best_overall.get("profile", ""),
        "best_overall_feature_count": best_overall.get("feature_count", 0),
        "best_overall_lcs_target_ratio": best_overall.get("lcs_target_ratio", 0),
        "exact_tail_adapter_rows": len(exact_adapter_rows),
        "no_saved_r6_rows": sum(1 for row in rows if row.get("no_saved_r6")),
        "stack_12_rows": sum(1 for row in rows if row.get("stack_12")),
        "zero_stack_arg_uses_r3_rows": sum(1 for row in rows if row.get("zero_stack_arg_uses_r3")),
        "rot_target_load_register_order_rows": sum(1 for row in rows if row.get("rot_target_load_register_order")),
        "pure_c_rot_target_load_register_order_rows": sum(
            1 for row in pure_c_rows if row.get("rot_target_load_register_order")
        ),
        "no_saved_r6_and_rot_target_load_register_order_rows": sum(
            1 for row in rows if row.get("no_saved_r6") and row.get("rot_target_load_register_order")
        ),
        "next_gate": (
            "Exact naked tail adapter is materializable; next gate is deciding whether to use it in the "
            "full BossVa structured packet or keep probing for a pure-C register/stack match."
            if exact_adapter_rows
            else best.get("next_gate", "No tail probes compared.")
        ),
    }
    return {
        "format": "oot3d_direct_smooth_tail_codegen_probe_v1",
        "inputs": {"target_tail_ops": TARGET_TAIL_OPS},
        "summary": summary,
        "rows": rows,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Smooth Tail Codegen Probe",
        "",
        "This report isolates the `00398484` smooth-step tail register and stack shape.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Rows | {summary['rows']} |",
        f"| Compiled | {summary['compiled']} |",
        f"| Pure-C rows | {summary['pure_c_rows']} |",
        f"| Best variant | `{summary['best_variant']}` |",
        f"| Best profile | `{summary['best_profile']}` |",
        f"| Best feature count | {summary['best_feature_count']} |",
        f"| Best LCS | {float_value(summary['best_lcs_target_ratio']):.4f} |",
        f"| Best pure-C variant | `{summary['best_pure_c_variant']}` |",
        f"| Best pure-C profile | `{summary['best_pure_c_profile']}` |",
        f"| Best pure-C feature count | {summary['best_pure_c_feature_count']} |",
        f"| Best overall variant | `{summary['best_overall_variant']}` |",
        f"| Best overall profile | `{summary['best_overall_profile']}` |",
        f"| Best overall feature count | {summary['best_overall_feature_count']} |",
        f"| Best overall LCS | {float_value(summary['best_overall_lcs_target_ratio']):.4f} |",
        f"| Exact tail adapter rows | {summary['exact_tail_adapter_rows']} |",
        f"| No saved r6 rows | {summary['no_saved_r6_rows']} |",
        f"| Stack #12 rows | {summary['stack_12_rows']} |",
        f"| Zero stack arg uses r3 rows | {summary['zero_stack_arg_uses_r3_rows']} |",
        f"| Rot target load-register rows | {summary['rot_target_load_register_order_rows']} |",
        f"| Pure-C rot target load-register rows | {summary['pure_c_rot_target_load_register_order_rows']} |",
        "",
        "## Next Gate",
        "",
        summary["next_gate"],
        "",
        "## Rows",
        "",
        "| Variant | Profile | Pure C | Features | LCS | no r6 | stack12 | zero r3 | rot r0/r1 | Next gate |",
        "| --- | --- | --- | ---: | ---: | --- | --- | --- | --- | --- |",
    ]
    for row in data["rows"]:
        lines.append(
            f"| `{row['variant']}` | `{row['profile']}` | {row['pure_c']} | {row.get('feature_count', 0)} | "
            f"{float_value(row.get('lcs_target_ratio')):.4f} | {row.get('no_saved_r6', False)} | "
            f"{row.get('stack_12', False)} | {row.get('zero_stack_arg_uses_r3', False)} | "
            f"{row.get('rot_target_load_register_order', False)} | {row.get('next_gate', '')} |"
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
        "compiled_instruction_count",
        "lcs_instruction_count",
        "lcs_target_ratio",
        "feature_count",
        "no_saved_r6",
        "stack_12",
        "zero_stack_arg_uses_r3",
        "rot_target_load_register_order",
        "rot_target_offset_order",
        "rot_sub_register_shape",
        "max_step_register_shape",
        "rot_split_3840_246",
        "joint_stack_offset_4",
        "joint_split_3072_1012",
        "first_difference",
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
        "direct smooth tail codegen probe: "
        f"{summary['compiled']}/{summary['rows']} compiled, "
        f"{summary['rot_target_load_register_order_rows']} recover rot r0/r1, "
        f"{summary['no_saved_r6_rows']} avoid saved r6"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
