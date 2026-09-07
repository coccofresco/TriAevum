#!/usr/bin/env python3
"""Probe mechanical helper-lowering variants for the BossVa structured port.

This is a batch lane for N64-derived C: it rewrites accessor/helper-heavy
structured source into more direct OOT3D offset forms, builds each variant with
the normal object comparison gate, and reports whether any function moves closer
to the target.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import shutil
import subprocess
from collections import Counter
from pathlib import Path
from typing import Any, Callable


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = ROOT / "src" / "overlays" / "actors" / "ovl_Boss_Va" / "z_boss_va.c"
DEFAULT_SOURCE_ROOT = ROOT / "analysis" / "boss_va_helper_lowering_sources"
DEFAULT_BUILD_ROOT = ROOT / "build" / "boss_va_helper_lowering_probe"
DEFAULT_OUT_JSON = ROOT / "analysis" / "boss_va_helper_lowering_probe.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "boss_va_helper_lowering_probe.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "boss_va_helper_lowering_probe.md"
DEFAULT_FUNCTIONS = [
    "oot3d_boss_va_zapper_intro",
    "oot3d_boss_va_zapper_attack",
    "oot3d_boss_va_zapper_damaged",
    "oot3d_boss_va_zapper_enraged",
    "oot3d_boss_va_zapper_death",
]
STRUCTURED_FLAG = "-DOOT3D_BOSS_VA_STRUCTURED_PORT"


Renderer = Callable[[list[str]], str]
Transform = Callable[[str], str]


def rel(path: Path | str) -> str:
    path = Path(path)
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def slug(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value).strip("_")


def read_json(path: Path, default: Any) -> Any:
    if not path.is_file():
        return default
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def find_matching_paren(text: str, open_index: int) -> int:
    depth = 0
    state = "code"
    i = open_index
    while i < len(text):
        char = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if state == "code":
            if char == "/" and nxt == "/":
                state = "line-comment"
                i += 2
                continue
            if char == "/" and nxt == "*":
                state = "block-comment"
                i += 2
                continue
            if char == '"':
                state = "string"
            elif char == "'":
                state = "char"
            elif char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
                if depth == 0:
                    return i
        elif state == "line-comment":
            if char == "\n":
                state = "code"
        elif state == "block-comment":
            if char == "*" and nxt == "/":
                state = "code"
                i += 2
                continue
        elif state in {"string", "char"}:
            if char == "\\":
                i += 2
                continue
            if state == "string" and char == '"':
                state = "code"
            elif state == "char" and char == "'":
                state = "code"
        i += 1
    raise ValueError(f"unmatched '(' at {open_index}")


def split_args(text: str) -> list[str]:
    args: list[str] = []
    start = 0
    paren_depth = 0
    bracket_depth = 0
    brace_depth = 0
    state = "code"
    i = 0
    while i < len(text):
        char = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if state == "code":
            if char == "/" and nxt == "/":
                state = "line-comment"
                i += 2
                continue
            if char == "/" and nxt == "*":
                state = "block-comment"
                i += 2
                continue
            if char == '"':
                state = "string"
            elif char == "'":
                state = "char"
            elif char == "(":
                paren_depth += 1
            elif char == ")":
                paren_depth -= 1
            elif char == "[":
                bracket_depth += 1
            elif char == "]":
                bracket_depth -= 1
            elif char == "{":
                brace_depth += 1
            elif char == "}":
                brace_depth -= 1
            elif char == "," and paren_depth == 0 and bracket_depth == 0 and brace_depth == 0:
                args.append(text[start:i].strip())
                start = i + 1
        elif state == "line-comment":
            if char == "\n":
                state = "code"
        elif state == "block-comment":
            if char == "*" and nxt == "/":
                state = "code"
                i += 2
                continue
        elif state in {"string", "char"}:
            if char == "\\":
                i += 2
                continue
            if state == "string" and char == '"':
                state = "code"
            elif state == "char" and char == "'":
                state = "code"
        i += 1
    tail = text[start:].strip()
    if tail:
        args.append(tail)
    return args


def is_ident(char: str) -> bool:
    return char == "_" or char.isalnum()


def replace_calls(text: str, name: str, renderer: Renderer) -> str:
    out: list[str] = []
    state = "code"
    i = 0
    last = 0
    while i < len(text):
        char = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if state == "code":
            if char == "/" and nxt == "/":
                state = "line-comment"
                i += 2
                continue
            if char == "/" and nxt == "*":
                state = "block-comment"
                i += 2
                continue
            if char == '"':
                state = "string"
                i += 1
                continue
            if char == "'":
                state = "char"
                i += 1
                continue
            if text.startswith(name, i):
                prev_ok = i == 0 or not is_ident(text[i - 1])
                end = i + len(name)
                next_ok = end == len(text) or not is_ident(text[end])
                if prev_ok and next_ok:
                    j = end
                    while j < len(text) and text[j].isspace():
                        j += 1
                    if j < len(text) and text[j] == "(":
                        close = find_matching_paren(text, j)
                        k = close + 1
                        while k < len(text) and text[k].isspace():
                            k += 1
                        if k < len(text) and text[k] == "{":
                            i += 1
                            continue
                        args = split_args(text[j + 1 : close])
                        replacement = renderer(args)
                        out.append(text[last:i])
                        out.append(replacement)
                        i = close + 1
                        last = i
                        continue
        elif state == "line-comment":
            if char == "\n":
                state = "code"
        elif state == "block-comment":
            if char == "*" and nxt == "/":
                state = "code"
                i += 2
                continue
        elif state in {"string", "char"}:
            if char == "\\":
                i += 2
                continue
            if state == "string" and char == '"':
                state = "code"
            elif state == "char" and char == "'":
                state = "code"
        i += 1
    out.append(text[last:])
    return "".join(out)


def require_args(name: str, args: list[str], count: int) -> None:
    if len(args) != count:
        raise ValueError(f"{name} expected {count} args, got {len(args)}: {args!r}")


def c_ptr(base: str, offset: str, ctype: str = "u8", const: bool = False) -> str:
    qualifier = "const " if const else ""
    return f"(({qualifier}{ctype}*)({base}) + ({offset}))"


def ptr_add_renderer(args: list[str]) -> str:
    require_args("oot3d_ptr_add", args, 2)
    return c_ptr(args[0], args[1], "u8")


def const_ptr_add_renderer(args: list[str]) -> str:
    require_args("oot3d_const_ptr_add", args, 2)
    return c_ptr(args[0], args[1], "u8", const=True)


def two_arg_cast_ptr(ctype: str, const_base: bool = False) -> Renderer:
    def renderer(args: list[str]) -> str:
        require_args(ctype, args, 2)
        base = c_ptr(args[0], args[1], "u8", const=const_base)
        return f"({ctype}*){base}"

    return renderer


def one_arg_load(ctype: str, offset: str, const_base: bool = True) -> Renderer:
    def renderer(args: list[str]) -> str:
        require_args(ctype, args, 1)
        base = c_ptr(args[0], offset, "u8", const=const_base)
        return f"(*({ctype}*){base})"

    return renderer


def one_arg_ptr(ctype: str, offset: str, const_base: bool = False) -> Renderer:
    def renderer(args: list[str]) -> str:
        require_args(ctype, args, 1)
        base = c_ptr(args[0], offset, "u8", const=const_base)
        return f"({ctype}*){base}"

    return renderer


def set_action_renderer(args: list[str]) -> str:
    require_args("oot3d_boss_va_set_action", args, 2)
    base = c_ptr(args[0], "OOT3D_BOSS_VA_OFFSET_ACTION_FUNC", "u8")
    return f"(*(const void**){base} = ({args[1]}))"


def set_burst_renderer(args: list[str]) -> str:
    require_args("oot3d_boss_va_set_burst", args, 2)
    base = c_ptr(args[0], "OOT3D_BOSS_VA_OFFSET_BURST", "u8")
    return f"(*(u8*){base} = ({args[1]}))"


def vec3f_copy_memcpy_renderer(args: list[str]) -> str:
    require_args("oot3d_vec3f_copy", args, 2)
    return f"FUN_00371738(({args[0]}), ({args[1]}), 0xc)"


def vec3f_copy_assign_renderer(args: list[str]) -> str:
    require_args("oot3d_vec3f_copy", args, 2)
    return f"(*(Oot3dVec3f*)({args[0]}) = *(const Oot3dVec3f*)({args[1]}))"


def abs_ternary_renderer(args: list[str]) -> str:
    require_args("oot3d_abs_s32", args, 1)
    return f"(({args[0]}) < 0 ? -({args[0]}) : ({args[0]}))"


def smooth_abs_expanded_renderer(args: list[str]) -> str:
    require_args("oot3d_boss_va_smooth_abs", args, 3)
    return f"oot3d_abs_s32(FUN_00375a18(({args[0]}), ({args[1]}), 1, ({args[2]}), 0))"


def transform_ptr_add_direct(text: str) -> str:
    text = replace_calls(text, "oot3d_const_ptr_add", const_ptr_add_renderer)
    return replace_calls(text, "oot3d_ptr_add", ptr_add_renderer)


def transform_boss_accessors_direct(text: str) -> str:
    accessors: list[tuple[str, Renderer]] = [
        ("oot3d_boss_va_const_vec3f", two_arg_cast_ptr("const Oot3dVec3f", const_base=True)),
        ("oot3d_boss_va_vec3f", two_arg_cast_ptr("Oot3dVec3f")),
        ("oot3d_boss_va_float", two_arg_cast_ptr("float")),
        ("oot3d_boss_va_s16", two_arg_cast_ptr("s16")),
        ("oot3d_boss_va_u8", lambda args: c_ptr(args[0], args[1], "u8")),
        ("oot3d_boss_va_params", one_arg_load("const s16", "OOT3D_BOSS_VA_OFFSET_PARAMS")),
        ("oot3d_boss_va_shape_rot_y", one_arg_load("const s16", "OOT3D_ACTOR_OFFSET_SHAPE_ROT_Y")),
        ("oot3d_boss_va_shape_rot_x", one_arg_load("const s16", "OOT3D_ACTOR_OFFSET_SHAPE_ROT_X")),
        ("oot3d_boss_va_actor_flags", one_arg_ptr("u32", "OOT3D_ACTOR_OFFSET_FLAGS")),
        ("oot3d_boss_va_skel_anime", lambda args: c_ptr(args[0], "OOT3D_BOSS_VA_OFFSET_SKEL_ANIME", "u8")),
        (
            "oot3d_boss_va_joint_table",
            one_arg_load("void*", "OOT3D_BOSS_VA_OFFSET_JOINT_TABLE_PTR", const_base=False),
        ),
        (
            "oot3d_boss_va_body_actor",
            one_arg_load("void*", "OOT3D_BOSS_VA_OFFSET_BODY_ACTOR_PTR", const_base=False),
        ),
    ]
    for name, renderer in accessors:
        text = replace_calls(text, name, renderer)
    return text


def transform_engine_accessors_direct(text: str) -> str:
    accessors: list[tuple[str, Renderer]] = [
        ("oot3d_actor_world_pos", one_arg_ptr("Oot3dVec3f", "OOT3D_ACTOR_OFFSET_WORLD_POS")),
        ("oot3d_play_player_actor", one_arg_load("void*", "OOT3D_PLAY_OFFSET_PLAYER_ACTOR", const_base=False)),
        ("oot3d_player_state_flags1", one_arg_load("const u32", "OOT3D_PLAYER_OFFSET_STATE_FLAGS1")),
        ("oot3d_actor_speed", one_arg_load("const float", "OOT3D_ACTOR_OFFSET_SPEED")),
    ]
    for name, renderer in accessors:
        text = replace_calls(text, name, renderer)
    return text


def transform_setters_direct(text: str) -> str:
    text = replace_calls(text, "oot3d_boss_va_set_action", set_action_renderer)
    return replace_calls(text, "oot3d_boss_va_set_burst", set_burst_renderer)


def transform_vec3f_copy_memcpy(text: str) -> str:
    return replace_calls(text, "oot3d_vec3f_copy", vec3f_copy_memcpy_renderer)


def transform_vec3f_copy_assign(text: str) -> str:
    return replace_calls(text, "oot3d_vec3f_copy", vec3f_copy_assign_renderer)


def transform_abs_ternary(text: str) -> str:
    return replace_calls(text, "oot3d_abs_s32", abs_ternary_renderer)


def transform_smooth_abs_expanded(text: str) -> str:
    return replace_calls(text, "oot3d_boss_va_smooth_abs", smooth_abs_expanded_renderer)


def compose(*transforms: Transform) -> Transform:
    def composed(text: str) -> str:
        for transform in transforms:
            text = transform(text)
        return text

    return composed


VARIANTS: dict[str, Transform] = {
    "ptr_add_direct": transform_ptr_add_direct,
    "boss_accessors_direct": transform_boss_accessors_direct,
    "engine_accessors_direct": transform_engine_accessors_direct,
    "setters_direct": transform_setters_direct,
    "vec3f_copy_memcpy": transform_vec3f_copy_memcpy,
    "vec3f_copy_assign": transform_vec3f_copy_assign,
    "abs_ternary": transform_abs_ternary,
    "smooth_abs_expanded": transform_smooth_abs_expanded,
    "all_direct_helpers": compose(
        transform_boss_accessors_direct,
        transform_engine_accessors_direct,
        transform_setters_direct,
        transform_ptr_add_direct,
    ),
    "all_direct_plus_memcpy": compose(
        transform_boss_accessors_direct,
        transform_engine_accessors_direct,
        transform_setters_direct,
        transform_vec3f_copy_memcpy,
        transform_ptr_add_direct,
    ),
    "all_direct_plus_struct_copy": compose(
        transform_boss_accessors_direct,
        transform_engine_accessors_direct,
        transform_setters_direct,
        transform_vec3f_copy_assign,
        transform_ptr_add_direct,
    ),
    "all_direct_plus_abs": compose(
        transform_boss_accessors_direct,
        transform_engine_accessors_direct,
        transform_setters_direct,
        transform_abs_ternary,
        transform_smooth_abs_expanded,
        transform_ptr_add_direct,
    ),
}


def build_source(source: Path, out_dir: Path, args: argparse.Namespace) -> dict[str, Any]:
    if out_dir.exists():
        shutil.rmtree(out_dir)
    command = [
        "powershell",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        str(ROOT / "scripts" / "build-matched-objects.ps1"),
        "-Compiler",
        args.compiler,
        "-Source",
        rel(source),
        "-OutDir",
        rel(out_dir),
        "-ExtraCFlag",
        STRUCTURED_FLAG,
    ]
    completed = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
    compare = read_json(out_dir / "compare_matched_objects.json", []) if completed.returncode == 0 else []
    manifest = read_json(out_dir / "manifest.json", {}) if completed.returncode == 0 else {}
    return {
        "returncode": completed.returncode,
        "stdout_tail": tail(completed.stdout),
        "stderr_tail": tail(completed.stderr),
        "compare": compare,
        "manifest": manifest,
    }


def tail(text: str, lines: int = 20) -> str:
    return "\n".join(text.splitlines()[-lines:])


def compare_by_name(compare_rows: list[dict[str, Any]], functions: list[str]) -> dict[str, dict[str, Any]]:
    wanted = set(functions)
    return {str(row.get("name", "")): row for row in compare_rows if row.get("name") in wanted}


def first_difference_text(row: dict[str, Any] | None) -> str:
    if not row:
        return ""
    diff = row.get("first_difference")
    if not isinstance(diff, dict):
        return ""
    return f"{diff.get('index')}: {diff.get('target')} vs {diff.get('compiled')}"


def run_text(row: dict[str, Any] | None) -> str:
    if not row:
        return ""
    run = row.get("longest_common_run")
    if not isinstance(run, dict):
        return ""
    return f"{run.get('length', '')}@{run.get('target_index', '')}/{run.get('compiled_index', '')}"


def classify(row: dict[str, Any] | None, built: bool) -> str:
    if not built:
        return "build-failed"
    if not row:
        return "not-compared"
    if row.get("exact_match"):
        return "exact"
    target = int(row.get("target_instruction_count", 0) or 0)
    compiled = int(row.get("compiled_instruction_count", 0) or 0)
    lcs_ratio = float(row.get("lcs_target_ratio", 0.0) or 0.0)
    prefix = int(row.get("matching_prefix", 0) or 0)
    suffix = int(row.get("matching_suffix", 0) or 0)
    run = row.get("longest_common_run")
    run_length = int(run.get("length", 0) or 0) if isinstance(run, dict) else 0
    if abs(target - compiled) <= 3 and (lcs_ratio >= 0.50 or prefix >= 5 or suffix >= 5 or run_length >= 8):
        return "codegen-near"
    if lcs_ratio >= 0.35 or run_length >= 5:
        return "structural-near"
    if lcs_ratio >= 0.15 or run_length >= 3:
        return "semantic-started"
    return "semantic-gap"


def metric(row: dict[str, Any] | None, key: str, default: int | float = 0) -> int | float:
    if not row:
        return default
    value = row.get(key, default)
    if isinstance(default, float):
        return float(value or 0.0)
    return int(value or 0)


def build_result_rows(
    variant: str,
    source: Path,
    build_dir: Path,
    result: dict[str, Any],
    baseline_rows: dict[str, dict[str, Any]],
    functions: list[str],
) -> list[dict[str, Any]]:
    built = result["returncode"] == 0
    rows = compare_by_name(result["compare"], functions) if built else {}
    out: list[dict[str, Any]] = []
    for function in functions:
        row = rows.get(function)
        baseline = baseline_rows.get(function)
        lcs = metric(row, "lcs_instruction_count")
        baseline_lcs = metric(baseline, "lcs_instruction_count")
        prefix = metric(row, "matching_prefix")
        baseline_prefix = metric(baseline, "matching_prefix")
        suffix = metric(row, "matching_suffix")
        baseline_suffix = metric(baseline, "matching_suffix")
        ratio = metric(row, "lcs_target_ratio", 0.0)
        baseline_ratio = metric(baseline, "lcs_target_ratio", 0.0)
        target_count = metric(row, "target_instruction_count")
        compiled_count = metric(row, "compiled_instruction_count")
        baseline_target_count = metric(baseline, "target_instruction_count")
        baseline_compiled_count = metric(baseline, "compiled_instruction_count")
        count_delta = abs(int(target_count) - int(compiled_count))
        baseline_count_delta = abs(int(baseline_target_count) - int(baseline_compiled_count))
        exact = bool(row.get("exact_match")) if row else False
        instruction_improved = bool(
            row
            and baseline
            and (
                exact
                or lcs > baseline_lcs
                or prefix > baseline_prefix
                or suffix > baseline_suffix
            )
        )
        count_distance_improved = bool(row and baseline and count_delta < baseline_count_delta)
        if exact:
            improvement_reason = "exact"
        elif lcs > baseline_lcs:
            improvement_reason = "lcs"
        elif prefix > baseline_prefix:
            improvement_reason = "prefix"
        elif suffix > baseline_suffix:
            improvement_reason = "suffix"
        elif count_distance_improved:
            improvement_reason = "count-distance-only"
        else:
            improvement_reason = ""
        out.append(
            {
                "category": classify(row, built),
                "variant": variant,
                "function": function,
                "source": rel(source),
                "build_dir": rel(build_dir),
                "returncode": result["returncode"],
                "compiler": result.get("manifest", {}).get("compiler", ""),
                "target_instruction_count": target_count,
                "compiled_instruction_count": compiled_count,
                "matching_prefix": prefix,
                "matching_suffix": suffix,
                "lcs_instruction_count": lcs,
                "lcs_target_ratio": ratio,
                "longest_common_run": run_text(row),
                "exact_match": exact,
                "first_difference": first_difference_text(row) or build_failure_text(result),
                "baseline_compiled_instruction_count": baseline_compiled_count,
                "baseline_matching_prefix": baseline_prefix,
                "baseline_matching_suffix": baseline_suffix,
                "baseline_lcs_instruction_count": baseline_lcs,
                "baseline_lcs_target_ratio": baseline_ratio,
                "delta_compiled_instruction_count": int(compiled_count) - int(baseline_compiled_count),
                "delta_count_distance": count_delta - baseline_count_delta,
                "delta_matching_prefix": int(prefix) - int(baseline_prefix),
                "delta_matching_suffix": int(suffix) - int(baseline_suffix),
                "delta_lcs_instruction_count": int(lcs) - int(baseline_lcs),
                "delta_lcs_target_ratio": round(float(ratio) - float(baseline_ratio), 4),
                "improved": instruction_improved,
                "count_distance_improved": count_distance_improved,
                "improvement_reason": improvement_reason,
            }
        )
    return out


def build_failure_text(result: dict[str, Any]) -> str:
    for key in ("stderr_tail", "stdout_tail"):
        lines = [line.strip() for line in str(result.get(key, "")).splitlines() if line.strip()]
        if lines:
            return lines[-1][:180]
    return ""


def write_variant_source(source: Path, source_root: Path, variant: str, transform: Transform) -> Path | None:
    original = source.read_text(encoding="utf-8", errors="replace")
    transformed = transform(original)
    if transformed == original:
        return None
    source_root.mkdir(parents=True, exist_ok=True)
    out = source_root / f"{source.stem}_{variant}.c"
    out.write_text(
        "/* Generated by scripts/probe_boss_va_helper_lowering.py. */\n"
        f"/* Variant: {variant}; source: {rel(source)}. */\n"
        + transformed,
        encoding="utf-8",
    )
    return out


def row_sort_key(row: dict[str, Any]) -> tuple[int, int, int, int, str, str]:
    category_order = {
        "exact": 0,
        "codegen-near": 1,
        "structural-near": 2,
        "semantic-started": 3,
        "semantic-gap": 4,
        "not-compared": 5,
        "build-failed": 6,
    }
    return (
        category_order.get(str(row["category"]), 99),
        -int(row["delta_lcs_instruction_count"]),
        -int(row["delta_matching_prefix"]),
        int(row["delta_count_distance"]),
        str(row["function"]),
        str(row["variant"]),
    )


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    fieldnames = [
        "category",
        "variant",
        "function",
        "improved",
        "count_distance_improved",
        "improvement_reason",
        "exact_match",
        "target_instruction_count",
        "compiled_instruction_count",
        "baseline_compiled_instruction_count",
        "delta_compiled_instruction_count",
        "delta_count_distance",
        "matching_prefix",
        "baseline_matching_prefix",
        "delta_matching_prefix",
        "matching_suffix",
        "baseline_matching_suffix",
        "delta_matching_suffix",
        "lcs_instruction_count",
        "baseline_lcs_instruction_count",
        "delta_lcs_instruction_count",
        "lcs_target_ratio",
        "baseline_lcs_target_ratio",
        "delta_lcs_target_ratio",
        "longest_common_run",
        "first_difference",
        "source",
        "build_dir",
        "compiler",
        "returncode",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: row.get(key, "") for key in fieldnames})


def summarize(rows: list[dict[str, Any]], baseline_rows: list[dict[str, Any]]) -> dict[str, Any]:
    counts = Counter(row["category"] for row in rows)
    improved = [row for row in rows if row["improved"]]
    count_distance_improved = [row for row in rows if row["count_distance_improved"]]
    count_only_improved = [
        row for row in rows if row["count_distance_improved"] and not row["improved"]
    ]
    variants = sorted({row["variant"] for row in rows})
    functions = sorted({row["function"] for row in rows})
    return {
        "variants": len(variants),
        "functions": len(functions),
        "rows": len(rows),
        "exact_rows": counts["exact"],
        "instruction_improved_rows": len(improved),
        "count_distance_improved_rows": len(count_distance_improved),
        "count_only_improved_rows": len(count_only_improved),
        "codegen_near_rows": counts["codegen-near"],
        "structural_near_rows": counts["structural-near"],
        "semantic_started_rows": counts["semantic-started"],
        "semantic_gap_rows": counts["semantic-gap"],
        "build_failed_rows": counts["build-failed"],
        "baseline_exact_rows": sum(1 for row in baseline_rows if row.get("exact_match")),
    }


def write_markdown(
    path: Path,
    rows: list[dict[str, Any]],
    baseline_rows: list[dict[str, Any]],
    summary: dict[str, Any],
) -> None:
    best_rows = sorted((row for row in rows if row["improved"]), key=row_sort_key)
    lines = [
        "# BossVa Helper-Lowering Probe",
        "",
        "Generated by mechanically lowering accessor-heavy N64-derived BossVa structured C and comparing each variant with the OOT3D target disassembly.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Variants built | {summary['variants']} |",
        f"| Functions checked per variant | {summary['functions']} |",
        f"| Exact variant rows | {summary['exact_rows']} |",
        f"| Instruction-improved rows vs baseline | {summary['instruction_improved_rows']} |",
        f"| Count-distance-improved rows | {summary['count_distance_improved_rows']} |",
        f"| Count-only improved rows | {summary['count_only_improved_rows']} |",
        f"| Codegen-near rows | {summary['codegen_near_rows']} |",
        f"| Structural-near rows | {summary['structural_near_rows']} |",
        f"| Build-failed rows | {summary['build_failed_rows']} |",
        f"| Baseline exact rows | {summary['baseline_exact_rows']} |",
        "",
        "## Baseline",
        "",
        "| Function | Target | Compiled | Prefix | Suffix | LCS | Ratio | Exact | First difference |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |",
    ]
    for row in sorted(baseline_rows, key=lambda item: item["function"]):
        lines.append(
            f"| `{row['function']}` | {row['target_instruction_count']} | {row['compiled_instruction_count']} | "
            f"{row['matching_prefix']} | {row['matching_suffix']} | {row['lcs_instruction_count']} | "
            f"{float(row['lcs_target_ratio']):.4f} | {row['exact_match']} | `{row['first_difference']}` |"
        )

    lines.extend(
        [
            "",
            "## Instruction Improvements",
            "",
            "| Variant | Function | Reason | Category | Target | Compiled | dCountDist | Prefix | dPrefix | LCS | dLCS | Ratio | First difference |",
            "| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |",
        ]
    )
    if best_rows:
        for row in best_rows:
            lines.append(
                f"| `{row['variant']}` | `{row['function']}` | `{row['improvement_reason']}` | "
                f"`{row['category']}` | "
                f"{row['target_instruction_count']} | {row['compiled_instruction_count']} | "
                f"{row['delta_count_distance']} | {row['matching_prefix']} | {row['delta_matching_prefix']} | "
                f"{row['lcs_instruction_count']} | {row['delta_lcs_instruction_count']} | "
                f"{float(row['lcs_target_ratio']):.4f} | `{row['first_difference']}` |"
            )
    else:
        lines.append("|  |  |  |  |  |  |  |  |  |  |  |  | No instruction-improving helper-lowering variants. |")

    count_only_rows = sorted(
        (row for row in rows if row["count_distance_improved"] and not row["improved"]),
        key=row_sort_key,
    )
    lines.extend(
        [
            "",
            "## Count-Only Improvements",
            "",
            "These variants reduce the compiled/target instruction-count distance but do not improve LCS, prefix, suffix, or exactness.",
            "",
            "| Variant | Function | Category | Target | Compiled | dCountDist | LCS | dLCS | Ratio | First difference |",
            "| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |",
        ]
    )
    if count_only_rows:
        for row in count_only_rows:
            lines.append(
                f"| `{row['variant']}` | `{row['function']}` | `{row['category']}` | "
                f"{row['target_instruction_count']} | {row['compiled_instruction_count']} | "
                f"{row['delta_count_distance']} | {row['lcs_instruction_count']} | "
                f"{row['delta_lcs_instruction_count']} | {float(row['lcs_target_ratio']):.4f} | "
                f"`{row['first_difference']}` |"
            )
    else:
        lines.append("|  |  |  |  |  |  |  |  |  | No count-only improvements. |")

    lines.extend(
        [
            "",
            "## All Rows",
            "",
            "| Variant | Function | Category | Improved | CountDist+ | Target | Compiled | Prefix | Suffix | LCS | Ratio | dLCS | dPrefix | dCountDist | First difference |",
            "| --- | --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |",
        ]
    )
    for row in sorted(rows, key=row_sort_key):
        lines.append(
            f"| `{row['variant']}` | `{row['function']}` | `{row['category']}` | "
            f"{str(row['improved']).lower()} | {str(row['count_distance_improved']).lower()} | "
            f"{row['target_instruction_count']} | {row['compiled_instruction_count']} | "
            f"{row['matching_prefix']} | {row['matching_suffix']} | {row['lcs_instruction_count']} | "
            f"{float(row['lcs_target_ratio']):.4f} | {row['delta_lcs_instruction_count']} | "
            f"{row['delta_matching_prefix']} | {row['delta_count_distance']} | `{row['first_difference']}` |"
        )

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def baseline_report_rows(
    baseline_by_name: dict[str, dict[str, Any]],
    functions: list[str],
    result: dict[str, Any],
    build_dir: Path,
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for function in functions:
        row = baseline_by_name.get(function)
        rows.append(
            {
                "function": function,
                "build_dir": rel(build_dir),
                "returncode": result["returncode"],
                "target_instruction_count": metric(row, "target_instruction_count"),
                "compiled_instruction_count": metric(row, "compiled_instruction_count"),
                "matching_prefix": metric(row, "matching_prefix"),
                "matching_suffix": metric(row, "matching_suffix"),
                "lcs_instruction_count": metric(row, "lcs_instruction_count"),
                "lcs_target_ratio": metric(row, "lcs_target_ratio", 0.0),
                "longest_common_run": run_text(row),
                "exact_match": bool(row.get("exact_match")) if row else False,
                "first_difference": first_difference_text(row),
            }
        )
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--source-root", type=Path, default=DEFAULT_SOURCE_ROOT)
    parser.add_argument("--build-root", type=Path, default=DEFAULT_BUILD_ROOT)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--function", action="append", default=[], help="Function to compare; repeatable.")
    parser.add_argument("--variant", action="append", choices=sorted(VARIANTS), default=[], help="Variant to build.")
    parser.add_argument("--compiler", choices=["gcc", "armcc", "auto"], default="gcc")
    parser.add_argument("--keep-build", action="store_true", help="Do not clean previous generated build/source roots.")
    args = parser.parse_args()

    source = args.source if args.source.is_absolute() else ROOT / args.source
    functions = args.function or DEFAULT_FUNCTIONS
    variant_names = args.variant or list(VARIANTS)

    if not args.keep_build:
        if args.source_root.exists():
            shutil.rmtree(args.source_root)
        if args.build_root.exists():
            shutil.rmtree(args.build_root)
    args.source_root.mkdir(parents=True, exist_ok=True)
    args.build_root.mkdir(parents=True, exist_ok=True)

    baseline_dir = args.build_root / "baseline"
    baseline_result = build_source(source, baseline_dir, args)
    baseline_by_name = compare_by_name(baseline_result["compare"], functions) if baseline_result["returncode"] == 0 else {}
    baseline_rows = baseline_report_rows(baseline_by_name, functions, baseline_result, baseline_dir)

    rows: list[dict[str, Any]] = []
    for variant in variant_names:
        variant_source = write_variant_source(source, args.source_root, variant, VARIANTS[variant])
        if variant_source is None:
            continue
        out_dir = args.build_root / slug(variant)
        result = build_source(variant_source, out_dir, args)
        rows.extend(build_result_rows(variant, variant_source, out_dir, result, baseline_by_name, functions))

    summary = summarize(rows, baseline_rows)
    write_json(
        args.out_json,
        {
            "summary": summary,
            "source": rel(source),
            "source_root": rel(args.source_root),
            "build_root": rel(args.build_root),
            "functions": functions,
            "baseline": baseline_rows,
            "rows": rows,
        },
    )
    write_csv(args.out_csv, rows)
    write_markdown(args.out_md, rows, baseline_rows, summary)

    print(
        "boss_va helper-lowering probe: "
        f"{summary['exact_rows']} exact rows, {summary['instruction_improved_rows']} instruction-improved rows, "
        f"{summary['count_only_improved_rows']} count-only improved rows, "
        f"{summary['build_failed_rows']} build-failed rows"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
