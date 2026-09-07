#!/usr/bin/env python3
"""Correlate 00473ef8 state/mode handlers with N64 source windows."""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import Counter
from pathlib import Path
from typing import Any

from probe_direct_source_subregions import rel


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_HANDLER_CONTEXT = ROOT / "analysis" / "direct_state_mode_handler_context.json"
DEFAULT_N64_SOURCE = (
    ROOT
    / "analysis"
    / "n64_port_units"
    / "src_n64_large_direct_exact.c"
    / "00473ef8_oot3d_player_action_swing_bottle__n64_Player_Action_SwingBottle.c"
)
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_state_mode_source_correlation.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_state_mode_source_correlation.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_state_mode_source_correlation.md"

FUNCTION_RE = re.compile(r"^(?:static\s+)?(?:[A-Za-z_][\w\s\*]+)\s+(?P<name>Player_[A-Za-z0-9_]+|func_808[0-9A-Fa-f]+)\s*\(")
TOKEN_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*|0x[0-9A-Fa-f]+")

FEATURE_TOKENS: dict[str, dict[str, int]] = {
    "message_continue": {
        "Message_ContinueTextbox": 6,
        "Message_StartTextbox": 4,
        "Message_GetState": 4,
        "TEXT_STATE_CLOSING": 4,
        "msgCtx": 3,
        "textId": 2,
        "choiceIndex": 2,
    },
    "state_transition": {
        "func_8083C0E8": 4,
        "func_80853080": 4,
        "func_80839FFC": 4,
        "Camera_SetFinishedFlag": 4,
        "Play_GetCamera": 3,
        "Player_SetupAction": 3,
        "actionVar1": 2,
        "actionVar2": 2,
    },
    "state_reset_wrapper": {
        "func_80839FFC": 7,
        "func_80853080": 5,
        "Camera_SetFinishedFlag": 4,
        "Play_GetCamera": 3,
        "Player_Action": 1,
    },
    "item_record_fetch": {
        "sBottleCatchInfo": 5,
        "sGetItemTable": 5,
        "D_80854528": 4,
        "GetItemEntry": 4,
        "BottleCatchInfo": 4,
        "exchangeItemId": 4,
        "interactRangeActor": 3,
    },
    "item_record": {
        "itemAction": 5,
        "heldItemAction": 3,
        "exchangeItemId": 4,
        "actor": 2,
        "textId": 2,
        "Item_Give": 4,
        "PLAYER_IA": 3,
        "BottleCatchInfo": 3,
    },
    "item_action_fields": {
        "itemAction": 6,
        "heldItemAction": 4,
        "PLAYER_IA": 4,
        "PLAYER_MWA": 3,
        "bottleCatchType": 3,
    },
    "screen_effect_or_cutscene_gate": {
        "Interface": 4,
        "Sfx": 4,
        "Audio": 4,
        "transitionTrigger": 4,
        "transitionType": 4,
        "Play_Trigger": 4,
        "Camera": 3,
        "respawn": 3,
        "fw": 2,
    },
    "item_or_magic_substate": {
        "magicState": 6,
        "Player_SpawnMagicSpell": 6,
        "Magic_Reset": 6,
        "D_80854A58": 4,
        "D_80854A64": 4,
        "D_80854A70": 4,
        "NAYRUS": 3,
        "FARORES": 3,
        "RESPAWN_MODE_TOP": 3,
    },
    "timer_or_substate_byte": {
        "actionVar2": 5,
        "actionVar1": 4,
        "DECR": 3,
        "timer": 3,
        "curFrame": 3,
        "LinkAnimation_Update": 3,
        "LinkAnimation_OnFrame": 3,
        "LinkAnimation_Play": 3,
        "Player_ProcessAnimSfxList": 3,
    },
    "state_byte": {
        "actionVar2": 4,
        "actionVar1": 4,
        "Player_Action": 2,
    },
    "float_or_vector_math": {
        "Vec3f": 5,
        "Math_": 4,
        "world": 3,
        "pos": 3,
        "rot": 2,
    },
    "unclassified": {
        "Player_Action": 1,
        "LinkAnimation_Update": 1,
    },
}

CALL_TOKENS: dict[str, dict[str, int]] = {
    "Message_ContinueTextbox": {"Message_ContinueTextbox": 8, "Message_GetState": 4, "TEXT_STATE_CLOSING": 4},
    "Audio_PlaySoundGeneral": {"Audio": 5, "Sfx": 4, "NA_SE": 3},
    "Interface_ChangeAlpha": {"Interface": 5, "Interface_ChangeAlpha": 8},
    "FUN_003725e0": {"func_80839FFC": 7, "func_80853080": 4, "Camera_SetFinishedFlag": 3},
    "FUN_00340bdc": {"actionVar2": 2, "func_8083C0E8": 2, "func_80853080": 2, "Camera_SetFinishedFlag": 2},
    "FUN_003371d8": {"sBottleCatchInfo": 3, "sGetItemTable": 3, "GetItemEntry": 3, "exchangeItemId": 3},
    "FUN_003523dc": {"transitionTrigger": 2, "Interface": 2, "screen": 1},
}


def read_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    return value if isinstance(value, dict) else {}


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


def tokenize(text: str) -> Counter[str]:
    tokens: Counter[str] = Counter()
    for token in TOKEN_RE.findall(text):
        tokens[token] += 1
        if "_" in token:
            for part in token.split("_"):
                if part:
                    tokens[part] += 1
    return tokens


def extract_functions(source_path: Path) -> list[dict[str, Any]]:
    lines = source_path.read_text(encoding="utf-8", errors="replace").splitlines()
    candidates: list[tuple[str, int]] = []
    for index, line in enumerate(lines):
        match = FUNCTION_RE.match(line.strip())
        if match:
            candidates.append((match.group("name"), index + 1))
    functions: list[dict[str, Any]] = []
    for index, (name, start) in enumerate(candidates):
        end = candidates[index + 1][1] - 1 if index + 1 < len(candidates) else len(lines)
        body_lines = lines[start - 1 : end]
        functions.append({"name": name, "start_line": start, "end_line": end, "lines": body_lines})
    return functions


def build_source_windows(functions: list[dict[str, Any]], window_size: int, stride: int) -> list[dict[str, Any]]:
    windows: list[dict[str, Any]] = []
    for function in functions:
        body_lines = function["lines"]
        if not body_lines:
            continue
        starts = list(range(0, max(1, len(body_lines)), stride))
        if len(body_lines) <= window_size:
            starts = [0]
        for local_start in starts:
            local_end = min(len(body_lines), local_start + window_size)
            if local_start >= local_end:
                continue
            source_start = int(function["start_line"]) + local_start
            source_end = int(function["start_line"]) + local_end - 1
            text = "\n".join(body_lines[local_start:local_end])
            windows.append(
                {
                    "function": function["name"],
                    "source_range": f"{source_start}-{source_end}",
                    "start_line": source_start,
                    "end_line": source_end,
                    "line_count": local_end - local_start,
                    "text": text,
                    "tokens": tokenize(text),
                }
            )
    return windows


def handler_query(row: dict[str, Any]) -> tuple[Counter[str], list[str]]:
    query: Counter[str] = Counter()
    evidence: list[str] = []
    features = [part for part in str(row.get("feature_tags", "")).split(";") if part]
    calls = [part for part in str(row.get("calls", "")).split(";") if part]
    offsets = [part for part in str(row.get("offsets", "")).split(";") if part]
    for feature in features:
        for token, weight in FEATURE_TOKENS.get(feature, {}).items():
            query[token] += weight
        evidence.append(f"feature:{feature}")
    for call in calls:
        for token, weight in CALL_TOKENS.get(call, {}).items():
            query[token] += weight
        if call.startswith("Message_") or call.startswith("Audio_") or call.startswith("Interface_"):
            query[call] += 8
        evidence.append(f"call:{call}")
    for offset in offsets:
        if offset in {"r4#768", "r4#769"}:
            query["actionVar2"] += 2
            query["magicState"] += 2
        elif offset == "r4#723":
            query["actionVar2"] += 2
            query["timer"] += 1
        elif offset in {"r4#220", "r4#224", "r4#226"}:
            query["itemAction"] += 2
        elif offset == "r4#480":
            query["GetItemEntry"] += 1
            query["itemAction"] += 1
        evidence.append(f"offset:{offset}")
    states = str(row.get("states", ""))
    if states:
        evidence.append(f"states:{states}")
    return query, evidence


def score_window(query: Counter[str], window_tokens: Counter[str]) -> tuple[float, int, list[str]]:
    if not query:
        return 0.0, 0, []
    max_weight = sum(query.values())
    matched_weight = 0
    matched_tokens: list[str] = []
    for token, weight in query.items():
        if token in window_tokens:
            matched_weight += weight
            matched_tokens.append(token)
    density = matched_weight / max(1, max_weight)
    token_count = sum(window_tokens.values())
    compactness = min(1.0, 90.0 / max(1.0, float(token_count)))
    score = density * (0.75 + compactness * 0.25)
    return round(score, 4), matched_weight, sorted(matched_tokens)


def confidence(score: float, matched_weight: int, matched_tokens: list[str]) -> str:
    if score >= 0.32 and matched_weight >= 14 and len(matched_tokens) >= 4:
        return "candidate-source-window"
    if score >= 0.18 and matched_weight >= 8 and len(matched_tokens) >= 3:
        return "weak-source-window"
    return "unmatched"


def correlate_handlers(context: dict[str, Any], windows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for handler in context.get("rows", []):
        if not isinstance(handler, dict):
            continue
        query, evidence = handler_query(handler)
        scored: list[dict[str, Any]] = []
        for window in windows:
            score, matched_weight, matched_tokens = score_window(query, window["tokens"])
            if matched_weight == 0:
                continue
            scored.append(
                {
                    "function": window["function"],
                    "source_range": window["source_range"],
                    "score": score,
                    "matched_weight": matched_weight,
                    "matched_tokens": ";".join(matched_tokens[:18]),
                    "confidence": confidence(score, matched_weight, matched_tokens),
                    "source_excerpt": " | ".join(line.strip() for line in window["text"].splitlines()[:6] if line.strip()),
                }
            )
        scored.sort(key=lambda row: (row["score"], row["matched_weight"]), reverse=True)
        best = scored[0] if scored else {}
        if best.get("confidence") == "unmatched":
            best = {}
        alternatives = [
            f"{row['function']}:{row['source_range']}:{row['score']:.4f}"
            for row in scored[1:4]
            if row["confidence"] != "unmatched"
        ]
        result.append(
            {
                "handler": handler.get("handler", ""),
                "range": handler.get("range", ""),
                "states": handler.get("states", ""),
                "feature_tags": handler.get("feature_tags", ""),
                "best_source_function": best.get("function", ""),
                "best_source_range": best.get("source_range", ""),
                "best_score": best.get("score", 0.0),
                "matched_weight": best.get("matched_weight", 0),
                "matched_tokens": best.get("matched_tokens", ""),
                "confidence": best.get("confidence", "unmatched"),
                "alternatives": ";".join(alternatives),
                "query_evidence": ";".join(evidence[:28]),
                "source_excerpt": best.get("source_excerpt", ""),
            }
        )
    return result


def build_report(args: argparse.Namespace) -> dict[str, Any]:
    context = read_json(args.handler_context)
    functions = extract_functions(args.n64_source)
    windows = build_source_windows(functions, args.window_size, args.stride)
    rows = correlate_handlers(context, windows)
    confidence_counts = Counter(str(row["confidence"]) for row in rows)
    function_counts = Counter(
        str(row["best_source_function"])
        for row in rows
        if row["confidence"] != "unmatched" and row["best_source_function"]
    )
    summary = {
        "handlers": len(rows),
        "candidate_source_windows": confidence_counts.get("candidate-source-window", 0),
        "weak_source_windows": confidence_counts.get("weak-source-window", 0),
        "unmatched_handlers": confidence_counts.get("unmatched", 0),
        "candidate_functions": len(function_counts),
        "top_candidate_functions": dict(function_counts.most_common(8)),
        "source_windows": len(windows),
        "source_functions": len(functions),
        "promotion_ready_handlers": 0,
        "next_gate": "Manually inspect candidate source windows and isolate branch/body probes before assigning state names.",
    }
    return {
        "format": "oot3d_direct_state_mode_source_correlation_v1",
        "inputs": {
            "handler_context": rel(args.handler_context),
            "n64_source": rel(args.n64_source),
            "window_size": args.window_size,
            "stride": args.stride,
        },
        "summary": summary,
        "rows": rows,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct State/Mode Source Correlation",
        "",
        "This report correlates OOT3D `00473ef8` state/mode switch handlers with N64 source windows.",
        "Scores are orientation evidence only; this report does not make any handler promotion-ready.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Handlers | {summary['handlers']} |",
        f"| Candidate source windows | {summary['candidate_source_windows']} |",
        f"| Weak source windows | {summary['weak_source_windows']} |",
        f"| Unmatched handlers | {summary['unmatched_handlers']} |",
        f"| Candidate functions | {summary['candidate_functions']} |",
        f"| Source windows | {summary['source_windows']} |",
        f"| Promotion-ready handlers | {summary['promotion_ready_handlers']} |",
        "",
        f"Next gate: {summary['next_gate']}",
        "",
        "## Top Candidate Functions",
        "",
        "| Function | Handler count |",
        "| --- | ---: |",
    ]
    for function, count in summary["top_candidate_functions"].items():
        lines.append(f"| `{function}` | {count} |")
    lines.extend(
        [
            "",
            "## Handler Correlations",
            "",
            "| Handler | States | Features | Confidence | Best source | Score | Matched tokens |",
            "| --- | --- | --- | --- | --- | ---: | --- |",
        ]
    )
    for row in data["rows"]:
        best = ""
        if row["best_source_function"]:
            best = f"{row['best_source_function']}:{row['best_source_range']}"
        lines.append(
            f"| `{row['handler']}` | `{row['states']}` | `{row['feature_tags']}` | "
            f"`{row['confidence']}` | `{best}` | {float(row['best_score']):.4f} | "
            f"`{row['matched_tokens']}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--handler-context", type=Path, default=DEFAULT_HANDLER_CONTEXT)
    parser.add_argument("--n64-source", type=Path, default=DEFAULT_N64_SOURCE)
    parser.add_argument("--window-size", type=int, default=34)
    parser.add_argument("--stride", type=int, default=12)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    args = parser.parse_args()
    if not args.handler_context.is_file():
        raise SystemExit(f"missing handler context: {args.handler_context}")
    if not args.n64_source.is_file():
        raise SystemExit(f"missing N64 source: {args.n64_source}")
    data = build_report(args)
    fields = [
        "handler",
        "range",
        "states",
        "feature_tags",
        "best_source_function",
        "best_source_range",
        "best_score",
        "matched_weight",
        "matched_tokens",
        "confidence",
        "alternatives",
        "query_evidence",
        "source_excerpt",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct state/mode source correlation: "
        f"{summary['handlers']} handlers, "
        f"{summary['candidate_source_windows']} candidate, "
        f"{summary['weak_source_windows']} weak, "
        f"{summary['unmatched_handlers']} unmatched"
    )
    print(str(args.out_md.relative_to(ROOT)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
