#!/usr/bin/env python3
"""Qualify structural-near direct structured lowering rows before promotion."""

from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path
from typing import Any

from compare_direct_packet_probe_matches import read_objdump_file
from compare_runtime_objects import compare_ops, read_target_functions
from probe_direct_packet_compilability import rel


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PROBE = ROOT / "analysis" / "direct_structured_lowering_probe.json"
DEFAULT_TARGET_DISASM = ROOT / "ghidra_export" / "disassembly.txt"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_structured_lowering_seed_qualification.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_structured_lowering_seed_qualification.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_structured_lowering_seed_qualification.md"
NEAR_CATEGORIES = {"exact-c", "codegen-near", "structural-near"}
GENERIC_OP_RE = re.compile(
    r"^(?:bl <target>|b[a-z]* <target>|mov r\d+,r\d+|stmdb sp!,\{[^}]+\}|ldmia sp!,\{[^}]+\}|"
    r"add sp,sp,#\d+|sub sp,sp,#\d+|mov r0,r0)$"
)
SPECIFIC_PATTERNS = {
    "battle_state_immediate": re.compile(r"#13\b"),
    "anim_base_offset": re.compile(r"r4,#420\b"),
    "vfp_frame_transfer": re.compile(r"^vmov s0,r0$"),
    "vfp_frame_subtract": re.compile(r"^vsub\.f32\b"),
    "joint_table_load": re.compile(r"\[r4,#540\]"),
    "joint_read_offset": re.compile(r"r0,#156\b"),
    "action_store": re.compile(r"\[r4,#3984\]"),
    "actor_flags": re.compile(r"\[r4,#4\]"),
}


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


def lcs_pairs(target_ops: list[str], compiled_ops: list[str]) -> list[tuple[int, int, str]]:
    n = len(target_ops)
    m = len(compiled_ops)
    dp = [[0] * (m + 1) for _ in range(n + 1)]
    for i in range(n - 1, -1, -1):
        for j in range(m - 1, -1, -1):
            if target_ops[i] == compiled_ops[j]:
                dp[i][j] = dp[i + 1][j + 1] + 1
            else:
                dp[i][j] = max(dp[i + 1][j], dp[i][j + 1])
    pairs: list[tuple[int, int, str]] = []
    i = 0
    j = 0
    while i < n and j < m:
        if target_ops[i] == compiled_ops[j]:
            pairs.append((i, j, target_ops[i]))
            i += 1
            j += 1
        elif dp[i + 1][j] >= dp[i][j + 1]:
            i += 1
        else:
            j += 1
    return pairs


def feature_hits(ops: list[str]) -> set[str]:
    return {name for name, pattern in SPECIFIC_PATTERNS.items() if any(pattern.search(op) for op in ops)}


def matched_feature_hits(pairs: list[tuple[int, int, str]]) -> set[str]:
    matched_ops = [op for _, _, op in pairs]
    return feature_hits(matched_ops)


def non_generic_matches(pairs: list[tuple[int, int, str]]) -> list[str]:
    return [op for _, _, op in pairs if not GENERIC_OP_RE.match(op)]


def branch_shape(ops: list[str]) -> str:
    head = " ".join(ops[:14])
    if "cmpne" in head:
        return "cmpne-chain"
    if "ble <target>" in head:
        return "range-check"
    return "other"


def qualify_row(row: dict[str, Any], target_functions: dict[str, dict[str, object]]) -> dict[str, Any]:
    target = target_functions.get(str(row.get("oot3d_name", "")), {})
    target_ops = list(target.get("ops", []))
    dump_path = ROOT / str(row.get("dump", ""))
    compiled_ops: list[str] = []
    if dump_path.is_file():
        compiled_ops = list(read_objdump_file(dump_path).get(str(row.get("symbol", "")), {}).get("ops", []))
    compare = compare_ops(target_ops, compiled_ops) if target_ops and compiled_ops else {}
    pairs = lcs_pairs(target_ops, compiled_ops) if target_ops and compiled_ops else []
    non_generic = non_generic_matches(pairs)
    matched_features = matched_feature_hits(pairs)
    target_features = feature_hits(target_ops)
    compiled_features = feature_hits(compiled_ops)
    feature_coverage = len(matched_features) / max(len(target_features), 1)
    category = str(row.get("category", ""))
    variant = str(row.get("variant", ""))
    naked_adapter_control = "naked" in variant
    near = category in NEAR_CATEGORIES
    qualified_seed = (
        near
        and float_value(row.get("lcs_target_ratio")) >= 0.38
        and len(non_generic) >= 8
        and feature_coverage >= 0.45
        and {"battle_state_immediate", "anim_base_offset", "vfp_frame_transfer", "joint_table_load"}
        <= matched_features
    )
    blockers: list[str] = []
    if branch_shape(target_ops) != branch_shape(compiled_ops):
        blockers.append("state-branch-shape-delta")
    if "action_store" not in matched_features:
        blockers.append("action-store-not-in-lcs")
    if "actor_flags" not in matched_features:
        blockers.append("actor-flag-store-not-in-lcs")
    if "vfp_frame_subtract" not in matched_features:
        blockers.append("vfp-subtract-register-shape-delta")
    if int_value(row.get("compiled_instruction_count")) != int_value(row.get("target_instruction_count")):
        blockers.append("instruction-count-delta")
    if target_ops and compiled_ops and target_ops[1] != compiled_ops[1]:
        blockers.append("stack-frame-delta")
    exact_adapter_control = (
        naked_adapter_control
        and float_value(compare.get("lcs_target_ratio")) >= 1.0
        and len(target_ops) == len(compiled_ops)
    )
    if naked_adapter_control:
        blockers.append("naked-adapter-control-not-maintained-c")
    promotion_ready = qualified_seed and not blockers
    return {
        "entry": row.get("entry", ""),
        "oot3d_name": row.get("oot3d_name", ""),
        "variant": variant,
        "profile": row.get("profile", ""),
        "category": category,
        "target_instruction_count": len(target_ops),
        "compiled_instruction_count": len(compiled_ops),
        "lcs_instruction_count": int_value(compare.get("lcs_instruction_count")),
        "lcs_target_ratio": float_value(compare.get("lcs_target_ratio")),
        "non_generic_lcs_matches": len(non_generic),
        "matched_feature_count": len(matched_features),
        "target_feature_count": len(target_features),
        "feature_coverage": round(feature_coverage, 4),
        "matched_features": ";".join(sorted(matched_features)),
        "target_features": ";".join(sorted(target_features)),
        "compiled_features": ";".join(sorted(compiled_features)),
        "target_branch_shape": branch_shape(target_ops),
        "compiled_branch_shape": branch_shape(compiled_ops),
        "qualified_seed": qualified_seed,
        "exact_adapter_control": exact_adapter_control,
        "promotion_ready": promotion_ready,
        "blockers": ";".join(blockers),
        "next_gate": next_gate(qualified_seed, blockers),
        "dump": row.get("dump", ""),
    }


def next_gate(qualified_seed: bool, blockers: list[str]) -> str:
    if not qualified_seed:
        return "Keep as orientation evidence; require stronger non-generic structural coverage."
    if blockers:
        return "Use as a qualified structural seed, then resolve blockers before maintained C promotion."
    return "Promotion candidate; run maintained-source integration checks."


def build_report(args: argparse.Namespace) -> dict[str, Any]:
    probe = read_json(args.probe, {})
    target_functions = read_target_functions(args.target_disasm)
    rows = [
        qualify_row(row, target_functions)
        for row in probe.get("rows", [])
        if isinstance(row, dict) and str(row.get("category", "")) in NEAR_CATEGORIES
    ]
    rows.sort(
        key=lambda row: (
            bool_value(row.get("promotion_ready")),
            bool_value(row.get("qualified_seed")),
            float_value(row.get("lcs_target_ratio")),
        ),
        reverse=True,
    )
    summary = {
        "rows": len(rows),
        "qualified_seeds": sum(1 for row in rows if row.get("qualified_seed")),
        "exact_adapter_controls": sum(1 for row in rows if row.get("exact_adapter_control")),
        "promotion_ready": sum(1 for row in rows if row.get("promotion_ready")),
        "best_entry": rows[0].get("entry", "") if rows else "",
        "best_variant": rows[0].get("variant", "") if rows else "",
        "best_profile": rows[0].get("profile", "") if rows else "",
        "best_lcs_target_ratio": rows[0].get("lcs_target_ratio", 0) if rows else 0,
        "best_blockers": rows[0].get("blockers", "") if rows else "",
        "next_gate": rows[0].get("next_gate", "") if rows else "No near structured lowering rows to qualify.",
    }
    return {
        "format": "oot3d_direct_structured_lowering_seed_qualification_v1",
        "inputs": {"probe": rel(args.probe), "target_disasm": rel(args.target_disasm)},
        "summary": summary,
        "rows": rows,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Structured Lowering Seed Qualification",
        "",
        "This report qualifies structural-near direct structured lowering rows before maintained C promotion.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Rows | {summary['rows']} |",
        f"| Qualified seeds | {summary['qualified_seeds']} |",
        f"| Exact adapter controls | {summary['exact_adapter_controls']} |",
        f"| Promotion-ready | {summary['promotion_ready']} |",
        f"| Best LCS | {float_value(summary['best_lcs_target_ratio']):.4f} |",
        "",
        "## Next Gate",
        "",
        f"- Entry: `{summary['best_entry']}`",
        f"- Variant/profile: `{summary['best_variant']}` / `{summary['best_profile']}`",
        f"- Blockers: `{summary['best_blockers']}`",
        f"- Gate: {summary['next_gate']}",
        "",
        "## Rows",
        "",
        "| Entry | Variant | Profile | Qualified | Exact adapter | Promotion | LCS | Features | Branch | Blockers |",
        "| --- | --- | --- | --- | --- | --- | ---: | --- | --- | --- |",
    ]
    for row in data["rows"]:
        lines.append(
            f"| `{row['entry']}` | `{row['variant']}` | `{row['profile']}` | "
            f"{row['qualified_seed']} | {row['exact_adapter_control']} | {row['promotion_ready']} | "
            f"{float_value(row['lcs_target_ratio']):.4f} | `{row['matched_features']}` | "
            f"`{row['target_branch_shape']}/{row['compiled_branch_shape']}` | `{row['blockers']}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, default=DEFAULT_PROBE)
    parser.add_argument("--target-disasm", type=Path, default=DEFAULT_TARGET_DISASM)
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
        "category",
        "target_instruction_count",
        "compiled_instruction_count",
        "lcs_instruction_count",
        "lcs_target_ratio",
        "non_generic_lcs_matches",
        "matched_feature_count",
        "target_feature_count",
        "feature_coverage",
        "matched_features",
        "target_features",
        "compiled_features",
        "target_branch_shape",
        "compiled_branch_shape",
        "qualified_seed",
        "exact_adapter_control",
        "promotion_ready",
        "blockers",
        "next_gate",
        "dump",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct structured lowering seed qualification: "
        f"{summary['qualified_seeds']}/{summary['rows']} qualified, "
        f"{summary['promotion_ready']} promotion-ready"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
