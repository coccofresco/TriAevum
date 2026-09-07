#!/usr/bin/env python3
"""Isolate blockers inside qualified direct structured lowering seeds."""

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
DEFAULT_QUALIFICATION = ROOT / "analysis" / "direct_structured_lowering_seed_qualification.json"
DEFAULT_TARGET_DISASM = ROOT / "ghidra_export" / "disassembly.txt"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_structured_lowering_isolation.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_structured_lowering_isolation.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_structured_lowering_isolation.md"

SPECIFIC_PATTERNS = {
    "battle_state_immediate": re.compile(r"#13\b"),
    "anim_base_offset": re.compile(r"r4,#420\b"),
    "vfp_frame_transfer": re.compile(r"^vmov s0,r0$"),
    "vfp_frame_subtract": re.compile(r"^vsub\.f32\b"),
    "joint_table_load": re.compile(r"\[r4,#540\]"),
    "joint_read_offset": re.compile(r"r0,#156\b"),
    "action_store": re.compile(r"\[r4,#3984\]"),
    "actor_flags": re.compile(r"\[r4,#4\]"),
    "smoothstep_limit": re.compile(r"#750\b"),
    "zero_stack_arg": re.compile(r"str r\d+,\[sp\]"),
}

SEGMENTS = [
    {
        "name": "prologue_attach",
        "target": (0, 4),
        "compiled": (0, 4),
        "blocker": "stack-frame-delta",
        "gate": "Match prologue stack allocation only after the body layout is stable.",
    },
    {
        "name": "state_gate",
        "target": (4, 12),
        "compiled": (4, 10),
        "blocker": "state-branch-shape-delta",
        "gate": "Force or source-match the INTRO title/brighten/finish cmpne-chain instead of a range check.",
    },
    {
        "name": "battle_anim_envelope",
        "target": (12, 24),
        "compiled": (10, 21),
        "blocker": "vfp-subtract-register-shape-delta",
        "gate": "Preserve target VFP temporary register shape around frame conversion and subtract.",
    },
    {
        "name": "flag_action_store",
        "target": (24, 29),
        "compiled": (21, 26),
        "blocker": "store-order-delta",
        "gate": "Recover target store order: actor flags clear before action function pointer store.",
    },
    {
        "name": "rot_smoothstep",
        "target": (29, 41),
        "compiled": (26, 35),
        "blocker": "offset-materialization-and-load-sign-delta",
        "gate": "Match unsigned shape rotation loads and split high-offset destination materialization.",
    },
    {
        "name": "joint_read",
        "target": (41, 46),
        "compiled": (35, 40),
        "blocker": "stack-frame-delta",
        "gate": "Align joint-read scratch stack offset with the target frame size.",
    },
    {
        "name": "joint_smoothstep",
        "target": (46, 54),
        "compiled": (40, 46),
        "blocker": "stack-frame-and-offset-materialization-delta",
        "gate": "Match target stack readback and split high-offset destination materialization.",
    },
    {
        "name": "epilogue_update",
        "target": (54, 61),
        "compiled": (46, 53),
        "blocker": "state-update-block-layout-delta",
        "gate": "Recover target out-of-line SkelAnime_Update block placement after the epilogue.",
    },
]


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


def slice_ops(ops: list[str], span: tuple[int, int]) -> list[str]:
    start, end = span
    return ops[start:end]


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


def branch_shape(ops: list[str]) -> str:
    text = " ".join(ops)
    if "cmpne" in text:
        return "cmpne-chain"
    if "ble <target>" in text:
        return "range-check"
    if any(op.startswith("b") for op in ops):
        return "branching"
    return "linear"


def store_order(ops: list[str]) -> str:
    stores: list[str] = []
    for op in ops:
        if op.startswith("str ") and "[r4,#4]" in op:
            stores.append("actor_flags")
        elif op.startswith("str ") and "[r4,#3984]" in op:
            stores.append("action_store")
    return ">".join(stores) if stores else "none"


def first_difference(compare: dict[str, Any]) -> str:
    diff = compare.get("first_difference")
    if not isinstance(diff, dict):
        return ""
    return f"{diff.get('index')}: {diff.get('target')} != {diff.get('compiled')}"


def segment_row(
    seed: dict[str, Any],
    segment: dict[str, Any],
    target_ops_all: list[str],
    compiled_ops_all: list[str],
) -> dict[str, Any]:
    target_span = segment["target"]
    compiled_span = segment["compiled"]
    target_ops = slice_ops(target_ops_all, target_span)
    compiled_ops = slice_ops(compiled_ops_all, compiled_span)
    compare = compare_ops(target_ops, compiled_ops)
    pairs = lcs_pairs(target_ops, compiled_ops)
    matched_features = feature_hits([op for _, _, op in pairs])
    target_features = feature_hits(target_ops)
    compiled_features = feature_hits(compiled_ops)
    missing_features = sorted(target_features - matched_features)
    lcs_ratio = float_value(compare.get("lcs_target_ratio"))
    blocked = lcs_ratio < 1.0 or bool(missing_features)
    blocker_class = segment["blocker"] if blocked else ""
    if blocked and segment["name"] == "state_gate" and branch_shape(target_ops) == branch_shape(compiled_ops):
        blocker_class = "state-register-shape-delta"
    return {
        "entry": seed.get("entry", ""),
        "oot3d_name": seed.get("oot3d_name", ""),
        "variant": seed.get("variant", ""),
        "profile": seed.get("profile", ""),
        "segment": segment["name"],
        "target_start_index": target_span[0],
        "target_end_index": target_span[1],
        "compiled_start_index": compiled_span[0],
        "compiled_end_index": compiled_span[1],
        "target_instruction_count": len(target_ops),
        "compiled_instruction_count": len(compiled_ops),
        "lcs_instruction_count": compare.get("lcs_instruction_count", 0),
        "lcs_target_ratio": round(lcs_ratio, 4),
        "longest_common_run": compare.get("longest_common_run", 0),
        "target_branch_shape": branch_shape(target_ops),
        "compiled_branch_shape": branch_shape(compiled_ops),
        "target_store_order": store_order(target_ops),
        "compiled_store_order": store_order(compiled_ops),
        "target_features": ";".join(sorted(target_features)),
        "compiled_features": ";".join(sorted(compiled_features)),
        "matched_features": ";".join(sorted(matched_features)),
        "missing_target_features": ";".join(missing_features),
        "first_difference": first_difference(compare),
        "blocker_class": blocker_class,
        "blocked": blocked,
        "next_gate": segment["gate"] if blocked else "Segment matches; keep as control evidence.",
    }


def select_seed(data: dict[str, Any]) -> dict[str, Any] | None:
    rows = [row for row in data.get("rows", []) if isinstance(row, dict) and bool_value(row.get("qualified_seed"))]
    rows.sort(key=lambda row: float_value(row.get("lcs_target_ratio")), reverse=True)
    return rows[0] if rows else None


def build_report(args: argparse.Namespace) -> dict[str, Any]:
    qualification = read_json(args.qualification, {})
    seed = select_seed(qualification)
    if seed is None:
        return {
            "format": "oot3d_direct_structured_lowering_isolation_v1",
            "inputs": {"qualification": rel(args.qualification), "target_disasm": rel(args.target_disasm)},
            "summary": {
                "segments": 0,
                "blocked_segments": 0,
                "top_blocker": "",
                "top_segment": "",
                "best_entry": "",
                "next_gate": "No qualified structured lowering seed to isolate.",
            },
            "rows": [],
        }

    target_functions = read_target_functions(args.target_disasm)
    target = target_functions.get(str(seed.get("oot3d_name", "")), {})
    target_ops = list(target.get("ops", []))
    dump_path = ROOT / str(seed.get("dump", ""))
    compiled_ops: list[str] = []
    if dump_path.is_file():
        compiled = read_objdump_file(dump_path).get("BossVa_ZapperIntro", {})
        compiled_ops = list(compiled.get("ops", []))

    rows = [segment_row(seed, segment, target_ops, compiled_ops) for segment in SEGMENTS]
    blocked_rows = [row for row in rows if row.get("blocked")]
    blocked_rows.sort(
        key=lambda row: (
            float_value(row.get("lcs_target_ratio")),
            -int(row.get("target_instruction_count", 0) or 0),
        )
    )
    top = blocked_rows[0] if blocked_rows else (rows[0] if rows else {})
    summary = {
        "segments": len(rows),
        "blocked_segments": len(blocked_rows),
        "best_entry": seed.get("entry", ""),
        "best_variant": seed.get("variant", ""),
        "best_profile": seed.get("profile", ""),
        "target_instruction_count": len(target_ops),
        "compiled_instruction_count": len(compiled_ops),
        "top_segment": top.get("segment", ""),
        "top_blocker": top.get("blocker_class", ""),
        "top_lcs_target_ratio": top.get("lcs_target_ratio", 0),
        "next_gate": top.get("next_gate", ""),
    }
    return {
        "format": "oot3d_direct_structured_lowering_isolation_v1",
        "inputs": {"qualification": rel(args.qualification), "target_disasm": rel(args.target_disasm)},
        "summary": summary,
        "rows": rows,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Structured Lowering Isolation",
        "",
        "This report isolates the qualified structured-lowering seed into target/compiled sub-blocks.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Segments | {summary['segments']} |",
        f"| Blocked segments | {summary['blocked_segments']} |",
        f"| Target instructions | {summary['target_instruction_count']} |",
        f"| Compiled instructions | {summary['compiled_instruction_count']} |",
        f"| Top segment | `{summary['top_segment']}` |",
        f"| Top blocker | `{summary['top_blocker']}` |",
        f"| Top segment LCS | {float_value(summary['top_lcs_target_ratio']):.4f} |",
        "",
        "## Next Gate",
        "",
        f"{summary['next_gate']}",
        "",
        "## Segments",
        "",
        "| Segment | Target | Compiled | LCS | Branch | Store order | Missing features | Blocker |",
        "| --- | ---: | ---: | ---: | --- | --- | --- | --- |",
    ]
    for row in data["rows"]:
        lines.append(
            f"| `{row['segment']}` | {row['target_instruction_count']} | {row['compiled_instruction_count']} | "
            f"{float_value(row['lcs_target_ratio']):.4f} | "
            f"`{row['target_branch_shape']}/{row['compiled_branch_shape']}` | "
            f"`{row['target_store_order']}/{row['compiled_store_order']}` | "
            f"`{row['missing_target_features']}` | `{row['blocker_class']}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qualification", type=Path, default=DEFAULT_QUALIFICATION)
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
        "segment",
        "target_start_index",
        "target_end_index",
        "compiled_start_index",
        "compiled_end_index",
        "target_instruction_count",
        "compiled_instruction_count",
        "lcs_instruction_count",
        "lcs_target_ratio",
        "longest_common_run",
        "target_branch_shape",
        "compiled_branch_shape",
        "target_store_order",
        "compiled_store_order",
        "target_features",
        "compiled_features",
        "matched_features",
        "missing_target_features",
        "first_difference",
        "blocker_class",
        "blocked",
        "next_gate",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct structured lowering isolation: "
        f"{summary['blocked_segments']}/{summary['segments']} blocked; "
        f"top={summary['top_segment']} {summary['top_blocker']}"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
