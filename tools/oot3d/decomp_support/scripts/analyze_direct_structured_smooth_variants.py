#!/usr/bin/env python3
"""Compare smooth-step tail lowering shapes across direct structured variants."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
from typing import Any

from compare_direct_packet_probe_matches import read_objdump_file
from probe_direct_packet_compilability import rel


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PROBE = ROOT / "analysis" / "direct_structured_lowering_probe.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_structured_smooth_variants.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_structured_smooth_variants.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_structured_smooth_variants.md"


def read_json(path: Path, default: Any) -> Any:
    if not path.is_file():
        return default
    return json.loads(path.read_text(encoding="utf-8"))


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


def has_op(ops: list[str], *needles: str) -> bool:
    return any(all(needle in op for needle in needles) for op in ops)


def first_index(ops: list[str], *needles: str) -> int:
    for index, op in enumerate(ops):
        if all(needle in op for needle in needles):
            return index
    return -1


def call_count(ops: list[str], symbol: str) -> int:
    return sum(1 for op in ops if op.startswith("bl ") and symbol in op)


def feature_names(row: dict[str, Any]) -> list[str]:
    features = [
        "cmpne_chain",
        "flag_before_action_store",
        "rot_unsigned_loads",
        "rot_target_offset_order",
        "rot_target_load_register_order",
        "rot_split_3840_246",
        "rot_sxth",
        "rot_stack_zero_arg",
        "zero_stack_arg_uses_r3",
        "joint_table_load",
        "joint_split_3072_1012",
        "joint_stack_zero_arg",
        "two_smoothstep_calls",
    ]
    return [feature for feature in features if row.get(feature)]


def next_gate(row: dict[str, Any]) -> str:
    if not row.get("rot_unsigned_loads") or not row.get("rot_split_3840_246"):
        return "Keep this row as negative evidence for smooth-step offset/load lowering."
    if not row.get("joint_split_3072_1012"):
        return "Recover the joint smooth-step high-offset materialization before using this tail seed."
    return (
        "Use split smooth-base evidence for the tail, then solve stack-frame, zero-argument register, "
        "and rot-load/subtract register ordering deltas."
    )


def row_for_variant(row: dict[str, Any]) -> dict[str, Any] | None:
    if not bool_value(row.get("dumped")):
        return None
    dump = ROOT / str(row.get("dump", ""))
    if not dump.is_file():
        return None
    compiled_ops = list(read_objdump_file(dump).get(str(row.get("symbol", "")), {}).get("ops", []))
    if not compiled_ops:
        return None

    flag_store = first_index(compiled_ops, "str ", "[r4,#4]")
    action_store = first_index(compiled_ops, "str ", "[r4,#3984]")
    zero_stack_stores = sum(1 for op in compiled_ops if op.startswith("str ") and ",[sp]" in op)

    rot_unsigned_loads = has_op(compiled_ops, "ldrh", "[r4,#188]") and has_op(compiled_ops, "ldrh", "[r4,#190]")
    rot_signed_loads = has_op(compiled_ops, "ldrsh", "[r4,#188]") or has_op(compiled_ops, "ldrsh", "[r4,#190]")
    rot_be_index = first_index(compiled_ops, "ldrh", "[r4,#190]")
    rot_bc_index = first_index(compiled_ops, "ldrh", "[r4,#188]")
    rot_target_offset_order = rot_be_index >= 0 and rot_bc_index >= 0 and rot_be_index < rot_bc_index
    rot_target_load_register_order = has_op(compiled_ops, "ldrh r0,[r4,#190]") and has_op(
        compiled_ops, "ldrh r1,[r4,#188]"
    )
    rot_split_3840_246 = has_op(compiled_ops, "add ", "r4,#3840") and has_op(compiled_ops, "add ", "#246")
    rot_compact_4080_6 = has_op(compiled_ops, "add ", "r4,#4080") and has_op(compiled_ops, "add ", "#6")
    joint_split_3072_1012 = has_op(compiled_ops, "add ", "r4,#3072") and has_op(compiled_ops, "add ", "#1012")

    result: dict[str, Any] = {
        "entry": row.get("entry", ""),
        "oot3d_name": row.get("oot3d_name", ""),
        "variant": row.get("variant", ""),
        "profile": row.get("profile", ""),
        "global_category": row.get("category", ""),
        "global_lcs_target_ratio": row.get("lcs_target_ratio", 0),
        "compiled_instruction_count": len(compiled_ops),
        "cmpne_chain": "cmpne" in " ".join(compiled_ops),
        "flag_store_index": flag_store,
        "action_store_index": action_store,
        "flag_before_action_store": flag_store >= 0 and action_store >= 0 and flag_store < action_store,
        "rot_unsigned_loads": rot_unsigned_loads,
        "rot_signed_loads": rot_signed_loads,
        "rot_target_offset_order": rot_target_offset_order,
        "rot_target_load_register_order": rot_target_load_register_order,
        "rot_split_3840_246": rot_split_3840_246,
        "rot_compact_4080_6": rot_compact_4080_6,
        "rot_sxth": has_op(compiled_ops, "sxth"),
        "rot_stack_zero_arg": zero_stack_stores >= 1,
        "zero_stack_arg_uses_r3": has_op(compiled_ops, "str r3,[sp]"),
        "joint_table_load": has_op(compiled_ops, "ldr ", "[r4,#540]"),
        "joint_split_3072_1012": joint_split_3072_1012,
        "joint_stack_zero_arg": zero_stack_stores >= 2,
        "zero_stack_store_count": zero_stack_stores,
        "smoothstep_call_count": call_count(compiled_ops, "Math_SmoothStepToS"),
        "two_smoothstep_calls": call_count(compiled_ops, "Math_SmoothStepToS") >= 2,
        "dump": row.get("dump", ""),
    }
    matched = feature_names(result)
    result["smooth_feature_count"] = len(matched)
    result["smooth_features"] = ";".join(matched)
    result["next_gate"] = next_gate(result)
    return result


def build_report(args: argparse.Namespace) -> dict[str, Any]:
    probe = read_json(args.probe, {})
    rows = [
        result
        for probe_row in probe.get("rows", [])
        if isinstance(probe_row, dict)
        for result in [row_for_variant(probe_row)]
        if result is not None
    ]
    rows.sort(
        key=lambda row: (
            int(row.get("smooth_feature_count", 0)),
            bool_value(row.get("rot_unsigned_loads")),
            bool_value(row.get("rot_split_3840_246")),
            bool_value(row.get("joint_split_3072_1012")),
            bool_value(row.get("zero_stack_arg_uses_r3")),
            bool_value(row.get("rot_target_offset_order")),
            float_value(row.get("global_lcs_target_ratio")),
        ),
        reverse=True,
    )
    best = rows[0] if rows else {}
    summary = {
        "rows": len(rows),
        "rot_unsigned_load_rows": sum(1 for row in rows if row.get("rot_unsigned_loads")),
        "rot_split_high_base_rows": sum(1 for row in rows if row.get("rot_split_3840_246")),
        "rot_compact_high_base_rows": sum(1 for row in rows if row.get("rot_compact_4080_6")),
        "rot_target_offset_order_rows": sum(1 for row in rows if row.get("rot_target_offset_order")),
        "rot_target_load_register_order_rows": sum(1 for row in rows if row.get("rot_target_load_register_order")),
        "zero_stack_arg_uses_r3_rows": sum(1 for row in rows if row.get("zero_stack_arg_uses_r3")),
        "joint_split_high_base_rows": sum(1 for row in rows if row.get("joint_split_3072_1012")),
        "flag_before_action_rows": sum(1 for row in rows if row.get("flag_before_action_store")),
        "best_variant": best.get("variant", ""),
        "best_profile": best.get("profile", ""),
        "best_feature_count": best.get("smooth_feature_count", 0),
        "best_global_lcs_target_ratio": best.get("global_lcs_target_ratio", 0),
        "best_features": best.get("smooth_features", ""),
        "next_gate": best.get("next_gate", "No smooth-step variants compared."),
    }
    return {
        "format": "oot3d_direct_structured_smooth_variants_v1",
        "inputs": {"probe": rel(args.probe)},
        "summary": summary,
        "rows": rows,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Structured Smooth Variants",
        "",
        "This report compares the `00398484` smooth-step tail evidence across structured variants.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Rows | {summary['rows']} |",
        f"| Rot unsigned-load rows | {summary['rot_unsigned_load_rows']} |",
        f"| Rot split high-base rows | {summary['rot_split_high_base_rows']} |",
        f"| Rot target offset-order rows | {summary['rot_target_offset_order_rows']} |",
        f"| Zero stack arg uses r3 rows | {summary['zero_stack_arg_uses_r3_rows']} |",
        f"| Joint split high-base rows | {summary['joint_split_high_base_rows']} |",
        f"| Flag-before-action rows | {summary['flag_before_action_rows']} |",
        f"| Best variant | `{summary['best_variant']}` |",
        f"| Best profile | `{summary['best_profile']}` |",
        f"| Best feature count | {summary['best_feature_count']} |",
        f"| Best global LCS | {float_value(summary['best_global_lcs_target_ratio']):.4f} |",
        "",
        "## Best Features",
        "",
        summary["best_features"] or "(none)",
        "",
        "## Next Gate",
        "",
        summary["next_gate"],
        "",
        "## Rows",
        "",
        "| Variant | Profile | Features | Rot ldrh | Offset order | Zero r3 | Rot split | Joint split | Global LCS | Next gate |",
        "| --- | --- | ---: | --- | --- | --- | --- | --- | ---: | --- |",
    ]
    for row in data["rows"]:
        lines.append(
            f"| `{row['variant']}` | `{row['profile']}` | {row['smooth_feature_count']} | "
            f"{row['rot_unsigned_loads']} | {row['rot_target_offset_order']} | "
            f"{row['zero_stack_arg_uses_r3']} | {row['rot_split_3840_246']} | "
            f"{row['joint_split_3072_1012']} | {float_value(row['global_lcs_target_ratio']):.4f} | "
            f"{row['next_gate']} |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, default=DEFAULT_PROBE)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    args = parser.parse_args()
    data = build_report(args)
    fields = [
        "entry",
        "oot3d_name",
        "variant",
        "profile",
        "global_category",
        "global_lcs_target_ratio",
        "compiled_instruction_count",
        "cmpne_chain",
        "flag_store_index",
        "action_store_index",
        "flag_before_action_store",
        "rot_unsigned_loads",
        "rot_signed_loads",
        "rot_target_offset_order",
        "rot_target_load_register_order",
        "rot_split_3840_246",
        "rot_compact_4080_6",
        "rot_sxth",
        "rot_stack_zero_arg",
        "zero_stack_arg_uses_r3",
        "joint_table_load",
        "joint_split_3072_1012",
        "joint_stack_zero_arg",
        "zero_stack_store_count",
        "smoothstep_call_count",
        "two_smoothstep_calls",
        "smooth_feature_count",
        "smooth_features",
        "next_gate",
        "dump",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct structured smooth variants: "
        f"{summary['rot_unsigned_load_rows']}/{summary['rows']} recover rot ldrh, "
        f"{summary['rot_split_high_base_rows']} recover rot split high-base, "
        f"{summary['joint_split_high_base_rows']} recover joint split high-base"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
