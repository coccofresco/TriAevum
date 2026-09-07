#!/usr/bin/env python3
"""Build focused workorders for the title-intro material-scalar fog state gap."""

from __future__ import annotations

import argparse
import csv
import json
import re
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT_JSON = ROOT / "analysis" / "title_intro_material_scalar_state_workorders.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "title_intro_material_scalar_state_workorders.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "title_intro_material_scalar_state_workorders.md"
DEFAULT_TRACE_ROUTES = ROOT / "analysis" / "title_intro_material_scalar_trace_routes.json"

PATTERNS = {
    "gameplay_draw_fog_call": "FUN_00464b2c",
    "runtime_list_binder": "FUN_002d960c",
    "direct_source_binder": "FUN_00368704",
    "runtime_packet_resolver": "FUN_003687a8",
    "pica_packet_fog_emit": "FUN_0047d6ac",
    "pica_fog_register_emit": "FUN_0047fe44",
    "environment_runtime_update": "FUN_0045dd50",
    "draw_effect_state_update": "FUN_0045d038",
    "final_fog_play_offset": "0xa82",
    "fog_source_play_offset": "0x5fc8",
    "material_list0_play_offset": "0x4c30",
    "material_list1_play_offset": "0x500c",
}

PROMOTED_NATIVE_ADDRESSES = {
    "002d960c",
    "00368704",
    "003687a8",
    "00464b2c",
    "0047d6ac",
    "0047fe44",
}

FOCUS_ADDRESSES = {
    "002e25f0": "Gameplay_Draw: native order/timing around 00464B2C and 002D960C.",
    "0045dd50": "Runtime environment/final fog writer feeding play+0x0A82 branch.",
    "0045d038": "Draw/effect state update uses material packet resolver paths in title-intro traces.",
    "00479e90": "Alternate source-object binder path using play+0x5FC8.",
    "0030f4d0": "Material submit path calls 0047D6AC with prepared packet state.",
    "003fbba8": "Render context light packet submit calls 0047D6AC.",
}


def rel(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT.parent.parent.parent).as_posix()
    except ValueError:
        return path.as_posix()


def decompiled_roots() -> list[Path]:
    roots = [ROOT / "ghidra_export" / "decompiled"]
    for path in (ROOT / "analysis").glob("*ghidra_export/decompiled"):
        roots.append(path)
    seen: set[Path] = set()
    result: list[Path] = []
    for root in roots:
        if root.is_dir() and root.resolve() not in seen:
            seen.add(root.resolve())
            result.append(root)
    return result


def parse_symbol(path: Path) -> tuple[str, str]:
    match = re.match(r"^\d+_([0-9a-fA-F]{8})_(.+)\.c$", path.name)
    if match:
        return match.group(1).lower(), match.group(2)
    return "", path.stem


def excerpt_matches(text: str, patterns: dict[str, str]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    lines = text.splitlines()
    for index, line in enumerate(lines, start=1):
        lowered = line.lower()
        matched = [name for name, needle in patterns.items() if needle.lower() in lowered]
        if matched:
            rows.append({"line": index, "patterns": matched, "text": line.strip()[:220]})
    return rows[:12]


def load_trace_routes(path: Path) -> dict[str, dict[str, Any]]:
    if not path.is_file():
        return {}
    data = json.loads(path.read_text(encoding="utf-8"))
    routes: dict[str, dict[str, Any]] = {}
    for route in data.get("routes", []):
        entry_text = str(route.get("entry", "")).lower().removeprefix("0x")
        if not entry_text:
            continue
        routes[entry_text] = route
    return routes


def classify(
    entry: str,
    symbol: str,
    text: str,
    matched_names: set[str],
    trace_route: dict[str, Any] | None,
) -> tuple[int, str, str, str]:
    score = 0
    reasons: list[str] = []
    next_step = "Keep as supporting callsite evidence."

    if entry in FOCUS_ADDRESSES:
        score += 120
        reasons.append(FOCUS_ADDRESSES[entry])

    if "gameplay_draw_fog_call" in matched_names and "runtime_list_binder" in matched_names:
        score += 100
        reasons.append("contains both the source update call and runtime list binder calls")
        next_step = (
            "Promote the draw-order contract: active source update, list binding, and PICA packet "
            "emission timing for the title-intro frame."
        )
    elif "gameplay_draw_fog_call" in matched_names:
        score += 70
        reasons.append("updates material-scalar source via 00464B2C")

    if "environment_runtime_update" in matched_names or entry == "0045dd50":
        score += 80
        reasons.append("updates runtime environment/final fog color state")
        next_step = (
            "Cross-check the final fog writer against the material-source trace timing before using "
            "it as render input."
        )

    if "direct_source_binder" in matched_names and "runtime_packet_resolver" in matched_names:
        score += 60
        reasons.append("resolves runtime object then applies direct source binder")
        next_step = "Classify whether this caller can select the title-intro active source object."

    if "pica_packet_fog_emit" in matched_names or "pica_fog_register_emit" in matched_names:
        score += 50
        reasons.append("emits prepared material fog packet to PICA registers")
        next_step = "Use as backend validation; do not treat this as the state producer."

    if "final_fog_play_offset" in matched_names:
        score += 40
        reasons.append("references play+0x0A82 final fog RGB")

    if "fog_source_play_offset" in matched_names:
        score += 35
        reasons.append("references play+0x5FC8 material-scalar source")

    if entry in PROMOTED_NATIVE_ADDRESSES:
        next_step = "Already promoted into fog_material_scalar support; use as stable contract evidence."

    if trace_route is not None:
        trace_status = str(trace_route.get("status", ""))
        trace_rows = int(trace_route.get("observed_trace_rows", 0))
        trace_next_step = str(trace_route.get("next_step", "")).strip()
        if trace_status == "trace_active_current_intro":
            score += 180
            reasons.insert(0, f"title-intro trace-active route with {trace_rows} observed writer rows")
            if trace_next_step:
                next_step = trace_next_step
        elif trace_status == "trace_active_promoted_contract":
            score += 30
            reasons.append(f"title-intro trace-active promoted contract with {trace_rows} observed writer rows")
        elif trace_status == "static_supporting_route_absent_from_current_intro_trace":
            reasons.append("not observed in the current title-intro material-scalar trace")
            if trace_next_step:
                next_step = trace_next_step
            score = min(score, 85)

    if not reasons:
        reasons.append("matched low-priority material/fog symbol")

    priority = "P0" if score >= 140 else "P1" if score >= 90 else "P2" if score >= 50 else "P3"
    return score, priority, "; ".join(reasons), next_step


def collect_rows(roots: list[Path], trace_routes: dict[str, dict[str, Any]]) -> list[dict[str, Any]]:
    by_function: dict[tuple[str, str], dict[str, Any]] = {}
    for root in roots:
        for path in sorted(root.glob("*.c")):
            text = path.read_text(encoding="utf-8", errors="replace")
            lowered = text.lower()
            matched_names = {name for name, needle in PATTERNS.items() if needle.lower() in lowered}
            entry, symbol = parse_symbol(path)
            if entry in FOCUS_ADDRESSES:
                matched_names.add("focus_address")
            if not matched_names:
                continue
            trace_route = trace_routes.get(entry)
            score, priority, reason, next_step = classify(entry, symbol, text, matched_names, trace_route)
            key = (entry, symbol)
            source_path = rel(path)
            row = {
                "priority": priority,
                "score": score,
                "entry": f"0x{entry}" if entry else "",
                "symbol": symbol,
                "path": source_path,
                "source_paths": [source_path],
                "matched_patterns": sorted(matched_names),
                "already_promoted": entry in PROMOTED_NATIVE_ADDRESSES,
                "trace_status": trace_route.get("status") if trace_route else "",
                "trace_observed_rows": int(trace_route.get("observed_trace_rows", 0)) if trace_route else 0,
                "trace_role": trace_route.get("role") if trace_route else "",
                "trace_pcs": trace_route.get("pcs", []) if trace_route else [],
                "reason": reason,
                "next_step": next_step,
                "matches": excerpt_matches(text, PATTERNS),
            }
            existing = by_function.get(key)
            if existing is None:
                by_function[key] = row
                continue
            existing["source_paths"].append(source_path)
            existing["matched_patterns"] = sorted(
                set(existing.get("matched_patterns", [])) | set(row["matched_patterns"])
            )
            if int(row["score"]) > int(existing["score"]):
                for field in ["priority", "score", "path", "reason", "next_step", "matches"]:
                    existing[field] = row[field]
            for field in ["trace_status", "trace_observed_rows", "trace_role", "trace_pcs"]:
                existing[field] = row[field]
    rows = list(by_function.values())
    rows.sort(key=lambda row: (-int(row["score"]), str(row["entry"]), row["path"]))
    return rows


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = [
        "priority",
        "score",
        "entry",
        "symbol",
        "already_promoted",
        "trace_status",
        "trace_observed_rows",
        "trace_role",
        "matched_patterns",
        "reason",
        "next_step",
        "path",
    ]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fields})


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    rows = data["rows"]
    lines = [
        "# Title Intro Material-Scalar State Workorders",
        "",
        "These workorders are derived from existing Ghidra exports and title-intro traces.",
        "They identify the remaining native state/timing path without using emulator RGB as runtime data.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Decompiled roots scanned | {summary['decompiled_roots_scanned']} |",
        f"| Matched functions | {summary['matched_functions']} |",
        f"| P0 workorders | {summary['p0_workorders']} |",
        f"| Already promoted contracts | {summary['already_promoted_contracts']} |",
        f"| Trace-active current-intro workorders | {summary['trace_active_current_intro_workorders']} |",
        "",
        f"Next gate: {summary['next_gate']}",
        "",
        "## Priority Workorders",
        "",
        "| Priority | Entry | Symbol | Promoted | Trace | Rows | Reason | Next step |",
        "| --- | --- | --- | --- | --- | ---: | --- | --- |",
    ]
    for row in rows[:24]:
        lines.append(
            f"| `{row['priority']}` | `{row['entry']}` | `{row['symbol']}` | "
            f"`{row['already_promoted']}` | `{row.get('trace_status', '')}` | "
            f"{row.get('trace_observed_rows', 0)} | {row['reason']} | {row['next_step']} |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--trace-routes", type=Path, default=DEFAULT_TRACE_ROUTES)
    args = parser.parse_args()

    roots = decompiled_roots()
    trace_routes = load_trace_routes(args.trace_routes)
    rows = collect_rows(roots, trace_routes)
    summary = {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "decompiled_roots_scanned": len(roots),
        "matched_functions": len(rows),
        "p0_workorders": sum(1 for row in rows if row["priority"] == "P0"),
        "already_promoted_contracts": sum(1 for row in rows if row["already_promoted"]),
        "trace_active_current_intro_workorders": sum(
            1 for row in rows if row.get("trace_status") == "trace_active_current_intro"
        ),
        "next_gate": (
            "Promote trace-active non-promoted state/timing workorders first: actor/material draw "
            "and actor-shadow source binders that apply play+0x5FC8 before spending more time on "
            "static-only alternate routes."
        ),
    }
    data = {
        "format": "oot3d_title_intro_material_scalar_state_workorders_v1",
        "inputs": {
            "decompiled_roots": [rel(root) for root in roots],
            "patterns": PATTERNS,
            "promoted_native_addresses": sorted(f"0x{addr}" for addr in PROMOTED_NATIVE_ADDRESSES),
            "trace_routes": rel(args.trace_routes) if args.trace_routes.is_file() else None,
        },
        "summary": summary,
        "rows": rows,
    }
    write_json(args.out_json, data)
    write_csv(args.out_csv, rows)
    write_markdown(args.out_md, data)
    print(json.dumps({"out_json": str(args.out_json), "out_csv": str(args.out_csv), "out_md": str(args.out_md)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
