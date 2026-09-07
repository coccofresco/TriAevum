#!/usr/bin/env python3
"""Map title-intro material-scalar trace PCs to native source routes.

This report is decompilation guidance only. It keeps emulator trace evidence as
validation/backtrace data and records which native OOT3D call routes are active
in the current title-intro capture.
"""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = ROOT.parents[2]
DEFAULT_SOURCE_TRACE = (
    REPO_ROOT
    / "captures"
    / "azahar_pica"
    / "title_intro_slot6_material_scalar_source_trace_20260707_01"
    / "derived"
    / "title_intro_material_scalar_source_trace.json"
)
DEFAULT_CODE_BIN = Path(r"E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin")
DEFAULT_OUT_JSON = ROOT / "analysis" / "title_intro_material_scalar_trace_routes.json"
DEFAULT_OUT_MD = ROOT / "analysis" / "title_intro_material_scalar_trace_routes.md"
DEFAULT_CODE_BASE = 0x00100000

ROUTES = {
    "direct_binder_dirty_clear_tail": {
        "entry": "0x00368704",
        "symbol": "FUN_00368704",
        "role": "direct material-scalar source binder dirty-clear tail",
        "pcs": ["0x00368750"],
        "status_if_seen": "trace_active_promoted_contract",
        "next_step_if_seen": (
            "Keep as the stable promoted binder contract; do not reimplement from trace values."
        ),
    },
    "actor_draw_direct_source_bind": {
        "entry": "0x002D5F68",
        "symbol": "FUN_002D5F68",
        "role": "actor/material draw path applying play+0x5FC8 source to packet+0x178",
        "pcs": ["0x002D6124"],
        "status_if_seen": "trace_active_current_intro",
        "next_step_if_seen": (
            "Promote actor draw material-scalar metadata: PlayState source offset literal "
            "0x5FC8, target packet offset +0x178, and the native flag gate."
        ),
    },
    "actor_shadow_direct_source_bind": {
        "entry": "0x0033E800",
        "symbol": "ActorShadow_Draw",
        "role": "actor shadow draw path applying play+0x5FC8 source after packet resolution",
        "pcs": ["0x0033EA1C"],
        "status_if_seen": "trace_active_current_intro",
        "next_step_if_seen": (
            "Promote actor shadow material-scalar metadata: PlayState source offset literal "
            "0x5FC8, actor packet-owner offset +0x194, and the native distance/color gates."
        ),
    },
    "alternate_runtime_source_object_binder": {
        "entry": "0x00479E90",
        "symbol": "FUN_00479E90",
        "role": "alternate runtime-list source-object binder using play+0x5FC8",
        "pcs": [
            "0x0047A0B4",
            "0x0047A140",
            "0x0047A154",
            "0x0047A160",
            "0x0047A16C",
            "0x0047A17C",
            "0x0047A2BC",
        ],
        "status_if_seen": "trace_active_current_intro",
        "status_if_absent": "static_supporting_route_absent_from_current_intro_trace",
        "next_step_if_seen": "Classify source-object selection for this active route.",
        "next_step_if_absent": (
            "Keep as supporting native evidence only; do not spend the current title-intro "
            "milestone on this route unless a future trace observes its PCs."
        ),
    },
}

CODE_LITERALS = [
    {
        "name": "actor_draw_play_material_source_offset",
        "address": 0x002D6424,
        "expected": 0x00005FC8,
        "meaning": "FUN_002D5F68 reads the material-scalar source from PlayState+0x5FC8.",
    },
    {
        "name": "actor_shadow_play_material_source_offset",
        "address": 0x0033EA70,
        "expected": 0x00005FC8,
        "meaning": "ActorShadow_Draw reads the material-scalar source from PlayState+0x5FC8.",
    },
    {
        "name": "alternate_binder_rgb_byte_to_float_scale",
        "address": 0x0047A2E0,
        "expected": 0x3B808081,
        "meaning": "FUN_00479E90 scales native RGB bytes by 1/255 before binding.",
    },
    {
        "name": "alternate_binder_runtime_list_head",
        "address": 0x0047A2F4,
        "expected": 0x00598B10,
        "meaning": "FUN_00479E90 references a runtime material list head.",
    },
    {
        "name": "alternate_binder_actor_effect_table",
        "address": 0x0047A300,
        "expected": 0x00598530,
        "meaning": "FUN_00479E90 references the secondary actor/effect table.",
    },
    {
        "name": "alternate_binder_runtime_list_end",
        "address": 0x0047A304,
        "expected": 0x00598CA8,
        "meaning": "FUN_00479E90 references the runtime material list end.",
    },
]


def norm_pc(value: str | int | None) -> str:
    if value is None:
        return ""
    if isinstance(value, int):
        return f"0x{value:08X}"
    text = value.strip()
    if not text:
        return ""
    return f"0x{int(text, 16):08X}"


def rel(path: Path) -> str:
    try:
        return path.resolve().relative_to(REPO_ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def scan_writer_trace(path: Path, route_pcs: set[str]) -> tuple[Counter[str], Counter[tuple[str, str]]]:
    pc_counts: Counter[str] = Counter()
    pc_lr_counts: Counter[tuple[str, str]] = Counter()
    with path.open("r", newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            pc = norm_pc(row.get("pc"))
            if pc not in route_pcs:
                continue
            lr = norm_pc(row.get("lr"))
            pc_counts[pc] += 1
            pc_lr_counts[(pc, lr)] += 1
    return pc_counts, pc_lr_counts


def candidate_hits(source_trace: dict[str, Any], route_pc_sets: dict[str, set[str]]) -> dict[str, list[dict[str, Any]]]:
    hits: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for candidate in source_trace.get("candidates", []):
        for route_name, route_pcs in route_pc_sets.items():
            count = 0
            top_pairs = []
            for pc_lr in candidate.get("top_pc_lrs", []):
                pc = norm_pc(pc_lr.get("pc"))
                if pc not in route_pcs:
                    continue
                pc_count = int(pc_lr.get("count", 0))
                count += pc_count
                top_pairs.append(
                    {
                        "pc": pc,
                        "lr": norm_pc(pc_lr.get("lr")),
                        "count": pc_count,
                    }
                )
            if count:
                hits[route_name].append(
                    {
                        "candidate": int(candidate.get("candidate", -1)),
                        "state_base": candidate.get("state_base"),
                        "fog_rgb_float_ptr": candidate.get("fog_rgb_float_ptr"),
                        "count_from_candidate_top_pcs": count,
                        "top_pc_lrs": top_pairs,
                    }
                )
    return hits


def read_code_literals(code_bin: Path, code_base: int) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    exists = code_bin.is_file()
    blob = code_bin.read_bytes() if exists else b""
    for literal in CODE_LITERALS:
        address = int(literal["address"])
        offset = address - code_base
        value = None
        status = "missing_code_bin"
        if exists:
            if 0 <= offset <= len(blob) - 4:
                value = int.from_bytes(blob[offset : offset + 4], "little")
                status = "matched" if value == int(literal["expected"]) else "mismatch"
            else:
                status = "outside_code_bin"
        rows.append(
            {
                **literal,
                "address": f"0x{address:08X}",
                "file_offset": f"0x{offset:08X}",
                "expected": f"0x{int(literal['expected']):08X}",
                "value": f"0x{value:08X}" if value is not None else None,
                "status": status,
            }
        )
    return rows


def build_report(source_trace_path: Path, code_bin: Path, code_base: int) -> dict[str, Any]:
    source_trace = load_json(source_trace_path)
    writer_trace = Path(source_trace["writer_trace"])
    route_pc_sets = {name: {norm_pc(pc) for pc in route["pcs"]} for name, route in ROUTES.items()}
    all_route_pcs = {pc for pcs in route_pc_sets.values() for pc in pcs}
    pc_counts, pc_lr_counts = scan_writer_trace(writer_trace, all_route_pcs)
    hits = candidate_hits(source_trace, route_pc_sets)

    route_rows = []
    for route_name, route in ROUTES.items():
        pcs = route_pc_sets[route_name]
        observed_count = sum(pc_counts[pc] for pc in pcs)
        status = (
            route.get("status_if_seen", "trace_active_current_intro")
            if observed_count
            else route.get("status_if_absent", "trace_absent_current_intro")
        )
        next_step = (
            route.get("next_step_if_seen", "Promote this active route.")
            if observed_count
            else route.get("next_step_if_absent", "Keep as supporting evidence.")
        )
        route_rows.append(
            {
                "route": route_name,
                "entry": norm_pc(route["entry"]),
                "symbol": route["symbol"],
                "role": route["role"],
                "pcs": sorted(pcs),
                "observed_trace_rows": int(observed_count),
                "observed_pc_lrs": [
                    {"pc": pc, "lr": lr, "count": int(count)}
                    for (pc, lr), count in pc_lr_counts.most_common()
                    if pc in pcs
                ],
                "candidate_hits": hits.get(route_name, []),
                "status": status,
                "next_step": next_step,
            }
        )

    active_entries = sorted(
        {row["entry"] for row in route_rows if row["observed_trace_rows"] and row["status"] != "trace_active_promoted_contract"}
    )
    promoted_active_entries = sorted(
        {row["entry"] for row in route_rows if row["observed_trace_rows"] and row["status"] == "trace_active_promoted_contract"}
    )
    inactive_entries = sorted({row["entry"] for row in route_rows if not row["observed_trace_rows"]})
    literal_rows = read_code_literals(code_bin, code_base)
    literal_status = "matched" if all(row["status"] == "matched" for row in literal_rows) else "needs_review"

    return {
        "format": "oot3d_title_intro_material_scalar_trace_routes_v1",
        "policy": "trace_validation_only_not_runtime_replacement_data",
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "inputs": {
            "source_trace": rel(source_trace_path),
            "writer_trace": rel(writer_trace),
            "code_bin": str(code_bin),
            "code_base": f"0x{code_base:08X}",
        },
        "summary": {
            "source_trace_candidates": int(source_trace.get("candidate_count", 0)),
            "source_trace_rows": int(source_trace.get("row_count", 0)),
            "active_current_intro_entries": active_entries,
            "active_promoted_contract_entries": promoted_active_entries,
            "inactive_current_intro_entries": inactive_entries,
            "code_literal_status": literal_status,
            "next_gate": (
                "Promote the trace-active actor/material source binders before spending more time "
                "on static-only alternate routes."
            ),
        },
        "routes": route_rows,
        "code_literals": literal_rows,
    }


def write_json(path: Path, report: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")


def write_markdown(path: Path, report: dict[str, Any]) -> None:
    summary = report["summary"]
    lines = [
        "# Title Intro Material-Scalar Trace Routes",
        "",
        "This report maps emulator writer PCs back to native OOT3D functions. It is evidence for decompilation/import work, not replacement runtime data.",
        "",
        "## Summary",
        "",
        f"- Source trace rows: `{summary['source_trace_rows']}`",
        f"- Source trace candidates: `{summary['source_trace_candidates']}`",
        f"- Active current-intro entries: `{summary['active_current_intro_entries']}`",
        f"- Active promoted contracts: `{summary['active_promoted_contract_entries']}`",
        f"- Inactive current-intro entries: `{summary['inactive_current_intro_entries']}`",
        f"- Code literal status: `{summary['code_literal_status']}`",
        f"- Next gate: {summary['next_gate']}",
        "",
        "## Routes",
        "",
        "| Status | Entry | Symbol | Rows | PCs | Role | Next step |",
        "| --- | --- | --- | ---: | --- | --- | --- |",
    ]
    for route in report["routes"]:
        lines.append(
            f"| `{route['status']}` | `{route['entry']}` | `{route['symbol']}` | "
            f"{route['observed_trace_rows']} | `{', '.join(route['pcs'])}` | "
            f"{route['role']} | {route['next_step']} |"
        )
    lines.extend(
        [
            "",
            "## Candidate Hits",
            "",
            "| Route | Candidate | State | Fog RGB Ptr | Count |",
            "| --- | ---: | --- | --- | ---: |",
        ]
    )
    for route in report["routes"]:
        for hit in route["candidate_hits"]:
            lines.append(
                f"| `{route['route']}` | {hit['candidate']} | `{hit['state_base']}` | "
                f"`{hit['fog_rgb_float_ptr']}` | {hit['count_from_candidate_top_pcs']} |"
            )
    lines.extend(
        [
            "",
            "## Code Literals",
            "",
            "| Status | Name | Address | Offset | Value | Meaning |",
            "| --- | --- | --- | --- | --- | --- |",
        ]
    )
    for literal in report["code_literals"]:
        lines.append(
            f"| `{literal['status']}` | `{literal['name']}` | `{literal['address']}` | "
            f"`{literal['file_offset']}` | `{literal['value']}` | {literal['meaning']} |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-trace", type=Path, default=DEFAULT_SOURCE_TRACE)
    parser.add_argument("--code-bin", type=Path, default=DEFAULT_CODE_BIN)
    parser.add_argument("--code-base", type=lambda text: int(text, 0), default=DEFAULT_CODE_BASE)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    args = parser.parse_args()

    report = build_report(args.source_trace, args.code_bin, args.code_base)
    write_json(args.out_json, report)
    write_markdown(args.out_md, report)
    print(json.dumps({"out_json": str(args.out_json), "out_md": str(args.out_md)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
