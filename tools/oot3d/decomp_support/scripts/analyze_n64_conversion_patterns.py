#!/usr/bin/env python3
"""Analyze recurring N64->OOT3D conversion structures in imported port units."""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


ROOT = Path(__file__).resolve().parents[1]

CALL_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")
MACRO_RE = re.compile(r"\b[A-Z][A-Z0-9_]{2,}\b")
MEMBER_RE = re.compile(
    r"\b(?:this|play|player|msgCtx|interfaceCtx|actor|boomerang|boomTarget|floorPoly|wallPoly)"
    r"->(?:[A-Za-z_][A-Za-z0-9_]*)(?:\.[A-Za-z_][A-Za-z0-9_]*)*"
)
TYPE_RE = re.compile(r"\b([A-Z][A-Za-z0-9_]*)(?:\s*\*+\s*|\s+)[A-Za-z_][A-Za-z0-9_]*\b")
OOT_HELPER_RE = re.compile(r"\boot3d_[A-Za-z0-9_]+\b")
OOT_OFFSET_RE = re.compile(r"\bOOT3D_[A-Z0-9_]+\b")
FUN_RE = re.compile(r"\bFUN_[0-9a-fA-F]{8}\b")
DAT_RE = re.compile(r"\bDAT_[0-9a-fA-F]{8}\b")
COMMENT_RE = re.compile(r"/\*.*?\*/|//[^\r\n]*", re.DOTALL)
STRING_RE = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')

IGNORE_CALLS = {
    "ABS",
    "ARRAY_COUNT",
    "CHECK_BTN_ALL",
    "COLPOLY_GET_NORMAL",
    "CONVEYOR_DIRECTION_TO_BINANG",
    "FALLTHROUGH",
    "PRINTF",
    "PRINTF_COLOR_GREEN",
    "PRINTF_RST",
    "SFX_PLAY_CENTERED",
    "T",
    "for",
    "if",
    "sizeof",
    "switch",
    "while",
}


@dataclass(frozen=True)
class PatternSpec:
    name: str
    category: str
    n64_regexes: tuple[str, ...]
    oot3d_regexes: tuple[str, ...]
    adapter: str
    next_step: str


PATTERNS: tuple[PatternSpec, ...] = (
    PatternSpec(
        "actor_state_action_fields",
        "struct-offset",
        (r"\bthis->actor\.", r"\bthis->(?:actionFunc|burst|timer2|isDead)\b"),
        (r"OOT3D_ACTOR_OFFSET_", r"OOT3D_BOSS_VA_OFFSET_(?:ACTION_FUNC|BURST|TIMER2|IS_DEAD)", r"oot3d_boss_va_set_"),
        "Expose per-actor offset accessors and action setters, then mechanically rewrite N64 state fields.",
        "Promote actor-specific field maps into adapter headers before porting more BossVa/BossMo/Player states.",
    ),
    PatternSpec(
        "skel_animation_api",
        "engine-api",
        (r"\bSkelAnime_Update\b", r"\bAnimation_(?:Change|GetLastFrame)\b", r"\bthis->skelAnime\b"),
        (r"\bFUN_003731e0\b", r"\bFUN_00375c08\b", r"\bFUN_0036ae14\b", r"OOT3D_BOSS_VA_OFFSET_SKEL"),
        "Map SkelAnime and Animation calls to recovered OOT3D animation entry points and skel offsets.",
        "Create named wrappers for FUN_003731e0/FUN_00375c08/FUN_0036ae14 and use them in structured ports.",
    ),
    PatternSpec(
        "math_step_api",
        "engine-api",
        (r"\bMath_(?:SmoothStepToS|SmoothStepToF|ScaledStepToS|StepToF)\b",),
        (r"\bFUN_00375a18\b", r"\bFUN_0036e168\b"),
        "Replace N64 math step calls with recovered OOT3D step functions, preserving call argument order.",
        "Name the OOT3D math-step callees and add a rewrite table for common Math_* functions.",
    ),
    PatternSpec(
        "vec3_yaw_pitch_api",
        "engine-api",
        (r"\bMath_Vec3f_(?:Copy|Yaw|Pitch|DistXYZ|Sum)\b", r"\bVec3f\b"),
        (r"\boot3d_vec3f_copy\b", r"\bFUN_003758b0\b", r"\bFUN_0037587c\b", r"OOT3D_ACTOR_OFFSET_WORLD_POS"),
        "Rewrite N64 Vec3f helpers to OOT3D vector helpers and actor/world-position offsets.",
        "Add named wrappers for the yaw/pitch callees, then let converter preserve N64 vector expressions.",
    ),
    PatternSpec(
        "player_and_playstate_layout",
        "struct-offset",
        (r"\bGET_PLAYER\b", r"\bPlayer\b", r"\bPlayState\b", r"\bplay->(?:colCtx|msgCtx|state)\b"),
        (r"\boot3d_play_player_actor\b", r"OOT3D_PLAY_OFFSET_", r"OOT3D_PLAYER_OFFSET_", r"oot3d_player_"),
        "Represent PlayState/Player with offset accessors; many N64 field names survive as OOT3D offsets.",
        "Move stable player/play offsets into a generated field-map table keyed by N64 member path.",
    ),
    PatternSpec(
        "collision_surface_api",
        "engine-api",
        (r"\b(?:Actor_UpdateBgCheckInfo|SurfaceType_|BgCheck_|DynaPoly_|CollisionCheck_)\w*\b", r"\bBGCHECK"),
        (r"OOT3D_BGCHECK", r"OOT3D_ACTOR_OFFSET_(?:BG_CHECK|FLOOR|WALL)", r"\bFUN_003761"),
        "Keep N64 collision flow and rewrite common surface/bgcheck calls to OOT3D function addresses.",
        "Recover names for the collision callees before attempting direct Player collision conversion.",
    ),
    PatternSpec(
        "message_context_flow",
        "subsystem-flow",
        (r"\bMessage_(?:StartTextbox|CloseTextbox|ContinueTextbox|ShouldAdvance)\b", r"\bmsgCtx\b", r"\bMSGMODE_"),
        (r"\boot3d_message_", r"OOT3D_MESSAGE", r"\bFUN_00343f0c\b"),
        "Split N64 Message_Update into OOT3D message context helpers and offset-backed state writes.",
        "Use the existing textbox split as a template for more Message_Update sub-block extraction.",
    ),
    PatternSpec(
        "pause_regs_ui_flow",
        "subsystem-flow",
        (r"\bRegs_InitDataImpl\b", r"\bgSaveContext\b", r"\b(?:Kaleido|Interface|Pause)\w*\b"),
        (r"\boot3d_pause_", r"\boot3d_pause_detail_", r"\bDAT_00[0-9a-fA-F]{6}\b"),
        "Treat N64 Regs_InitDataImpl as a source lane and extract OOT3D split UI helpers by global state/data refs.",
        "Continue splitting Regs_InitDataImpl by target address and promote repeated pause detail/group adapters.",
    ),
    PatternSpec(
        "boss_va_effect_collision_flow",
        "actor-subsystem",
        (r"\bBossVa_(?:Spark|Tumor|SetSparkEnv|SetDeathEnv|SpawnZapperCharge)\b", r"\bCollisionCheck_SetA[CT]\b"),
        (r"\bFUN_0031f5a8\b", r"\bFUN_0031c7d4\b", r"\bFUN_003761(?:68|f0)\b", r"OOT3D_PLAY_OFFSET_COLCHK_CTX"),
        "Map BossVa effect and lightning-collision helpers to observed OOT3D variadic/effect callees.",
        "Name the BossVa effect callees and wrap their varargs before reshaping attack/enraged/death.",
    ),
    PatternSpec(
        "random_and_sfx_api",
        "engine-api",
        (r"\bRand_(?:ZeroOne|CenteredFloat)\b", r"\bActor_PlaySfx\b"),
        (r"\bFUN_003759d0\b", r"\bFUN_003738a8\b", r"\bFUN_00375bcc\b"),
        "Rewrite random and actor-sfx helper calls to recovered OOT3D engine functions.",
        "Give these callees semantic names; this reduces ambiguity in generated exact seeds and structured ports.",
    ),
    PatternSpec(
        "global_state_arrays",
        "data-symbol",
        (r"\bs(?:FightPhase|BodyState|CsState|Effects)\b", r"\bgSaveContext\b"),
        (r"\bDAT_0039(?:8924|92ec|96c8|9c20)\b", r"\bDAT_003985(?:78|88)\b", r"\bDAT_00[0-9a-fA-F]{6}\b"),
        "Model OOT3D globals as named state packets so N64 global-state logic can be rewritten directly.",
        "Promote repeated DAT_* state arrays to named data symbols before further direct conversion.",
    ),
)


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def slug(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value).strip("_")


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def read_text(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def extract_path(row: dict[str, str], out_root: Path) -> Path:
    unit_dir = out_root / slug(row["port_file"])
    prefix = f"{row['oot3d_entry']}_{row['oot3d_name']}__n64_{row['n64_name']}"
    matches = sorted(unit_dir.glob(f"{slug(prefix)}*.c"))
    if matches:
        return matches[0]
    return unit_dir / f"{slug(prefix)}.c"


def count_matches(regexes: Iterable[str], text: str) -> int:
    return sum(len(re.findall(regex, text)) for regex in regexes)


def code_only(text: str) -> str:
    return STRING_RE.sub("", COMMENT_RE.sub("", text))


def top(counter: Counter[str], limit: int) -> list[dict[str, object]]:
    return [{"name": name, "count": count} for name, count in counter.most_common(limit)]


def source_domain(port_file: str) -> str:
    if "Boss_Va" in port_file:
        return "boss_va"
    if "message" in port_file:
        return "message"
    if "kaleido" in port_file:
        return "pause"
    if "player" in port_file:
        return "player"
    if "large_direct" in port_file:
        return "large_direct_exact_seed"
    if "split_kaleido" in port_file:
        return "split_kaleido_exact_seed"
    return "other"


def update_counter(counter: Counter[str], values: Iterable[str]) -> None:
    for value in values:
        counter[value] += 1


def analyze_rows(map_rows: list[dict[str, str]], n64_out_root: Path) -> dict[str, object]:
    port_source_cache: dict[str, str] = {}
    port_code_cache: dict[str, str] = {}
    seen_oot_sources: set[str] = set()
    unit_rows: list[dict[str, object]] = []
    n64_calls: Counter[str] = Counter()
    n64_macros: Counter[str] = Counter()
    n64_members: Counter[str] = Counter()
    n64_types: Counter[str] = Counter()
    oot_helpers: Counter[str] = Counter()
    oot_offsets: Counter[str] = Counter()
    oot_fun: Counter[str] = Counter()
    oot_dat: Counter[str] = Counter()
    domain_counts: dict[str, Counter[str]] = defaultdict(Counter)
    n64_groups: Counter[tuple[str, str]] = Counter()

    row_texts: list[tuple[dict[str, str], str, str]] = []

    for row in map_rows:
        n64_path = extract_path(row, n64_out_root)
        n64_text = read_text(n64_path)
        n64_code = code_only(n64_text)
        source_path = ROOT / row["port_file"]
        oot_text = port_source_cache.setdefault(row["port_file"], read_text(source_path))
        oot_code = port_code_cache.setdefault(row["port_file"], code_only(oot_text))
        domain = source_domain(row["port_file"])
        row_texts.append((row, n64_code, oot_code))

        calls = [call for call in CALL_RE.findall(n64_code) if call not in IGNORE_CALLS]
        update_counter(n64_calls, calls)
        update_counter(n64_macros, MACRO_RE.findall(n64_code))
        update_counter(n64_members, MEMBER_RE.findall(n64_code))
        update_counter(n64_types, TYPE_RE.findall(n64_code))
        if row["port_file"] not in seen_oot_sources:
            seen_oot_sources.add(row["port_file"])
            update_counter(oot_helpers, OOT_HELPER_RE.findall(oot_code))
            update_counter(oot_offsets, OOT_OFFSET_RE.findall(oot_code))
            update_counter(oot_fun, FUN_RE.findall(oot_code))
            update_counter(oot_dat, DAT_RE.findall(oot_code))
        domain_counts[domain]["mapped"] += 1
        domain_counts[domain][row["status"]] += 1
        n64_groups[(row["n64_source"], row["n64_name"])] += 1

        unit_rows.append(
            {
                "oot3d_entry": row["oot3d_entry"],
                "oot3d_name": row["oot3d_name"],
                "n64_source": row["n64_source"],
                "n64_name": row["n64_name"],
                "port_file": row["port_file"],
                "status": row["status"],
                "domain": domain,
                "n64_extract": rel(n64_path),
                "n64_calls": len(calls),
                "n64_member_paths": len(MEMBER_RE.findall(n64_code)),
                "oot3d_helpers_in_port_file": len(set(OOT_HELPER_RE.findall(oot_code))),
                "oot3d_offsets_in_port_file": len(set(OOT_OFFSET_RE.findall(oot_code))),
            }
        )

    pattern_rows = []
    for spec in PATTERNS:
        n64_count = 0
        oot_count = 0
        n64_function_hits: set[str] = set()
        oot_source_hits: set[str] = set()
        counted_oot_sources: set[str] = set()
        statuses: Counter[str] = Counter()
        domains: Counter[str] = Counter()
        both_hits = 0

        for row, n64_text, oot_text in row_texts:
            row_n64_count = count_matches(spec.n64_regexes, n64_text)
            row_oot_count = count_matches(spec.oot3d_regexes, oot_text)
            n64_count += row_n64_count
            if row_oot_count and row["port_file"] not in counted_oot_sources:
                counted_oot_sources.add(row["port_file"])
                oot_count += row_oot_count
            if row_n64_count:
                n64_function_hits.add(f"{row['oot3d_entry']} {row['oot3d_name']} <= {row['n64_name']}")
            if row_oot_count:
                oot_source_hits.add(row["port_file"])
            if row_n64_count or row_oot_count:
                statuses[row["status"]] += 1
                domains[source_domain(row["port_file"])] += 1
            if row_n64_count and row_oot_count:
                both_hits += 1

        if both_hits >= 2:
            confidence = "high"
        elif n64_count and oot_count:
            confidence = "medium"
        elif n64_count:
            confidence = "n64-only"
        else:
            confidence = "low"

        pattern_rows.append(
            {
                "name": spec.name,
                "category": spec.category,
                "confidence": confidence,
                "rows_with_both_sides": both_hits,
                "n64_occurrences": n64_count,
                "oot3d_occurrences": oot_count,
                "domains": dict(sorted(domains.items())),
                "statuses": dict(sorted(statuses.items())),
                "adapter": spec.adapter,
                "next_step": spec.next_step,
                "n64_evidence": sorted(n64_function_hits)[:12],
                "oot3d_evidence": sorted(oot_source_hits)[:12],
            }
        )

    duplicated_n64 = [
        {
            "n64_source": source,
            "n64_name": name,
            "mapped_rows": count,
        }
        for (source, name), count in n64_groups.most_common()
        if count > 1
    ]

    return {
        "summary": {
            "mapped_rows": len(map_rows),
            "port_files": len({row["port_file"] for row in map_rows}),
            "n64_sources": len({row["n64_source"] for row in map_rows}),
            "domains": {domain: dict(counter) for domain, counter in sorted(domain_counts.items())},
        },
        "unit_rows": unit_rows,
        "duplicated_n64_sources": duplicated_n64,
        "patterns": sorted(
            pattern_rows,
            key=lambda row: (
                {"high": 0, "medium": 1, "n64-only": 2, "low": 3}.get(str(row["confidence"]), 9),
                -int(row["rows_with_both_sides"]),
                str(row["name"]),
            ),
        ),
        "top_n64_calls": top(n64_calls, 30),
        "top_n64_macros": top(n64_macros, 30),
        "top_n64_members": top(n64_members, 30),
        "top_n64_types": top(n64_types, 30),
        "top_oot3d_helpers": top(oot_helpers, 30),
        "top_oot3d_offsets": top(oot_offsets, 30),
        "top_oot3d_fun_symbols": top(oot_fun, 30),
        "top_oot3d_data_symbols": top(oot_dat, 30),
    }


def write_json(path: Path, data: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def write_patterns_csv(path: Path, data: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "name",
        "category",
        "confidence",
        "rows_with_both_sides",
        "n64_occurrences",
        "oot3d_occurrences",
        "domains",
        "statuses",
        "adapter",
        "next_step",
    ]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in data["patterns"]:  # type: ignore[index]
            writer.writerow({key: json.dumps(row[key], sort_keys=True) if key in {"domains", "statuses"} else row[key] for key in fieldnames})


def table_counter(lines: list[str], title: str, rows: list[dict[str, object]], name_header: str = "Name") -> None:
    lines.extend([f"## {title}", "", f"| {name_header} | Count |", "| --- | ---: |"])
    if not rows:
        lines.append("| - | 0 |")
    for row in rows:
        lines.append(f"| `{row['name']}` | {row['count']} |")
    lines.append("")


def write_markdown(path: Path, data: dict[str, object]) -> None:
    summary = data["summary"]  # type: ignore[index]
    patterns = data["patterns"]  # type: ignore[index]
    duplicated = data["duplicated_n64_sources"]  # type: ignore[index]

    lines = [
        "# N64 to OOT3D Conversion Pattern Analysis",
        "",
        "Generated from `metadata/n64_port_map.csv`, `analysis/n64_port_units/`, and maintained OOT3D sources.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Mapped OOT3D/N64 rows | {summary['mapped_rows']} |",
        f"| OOT3D port files | {summary['port_files']} |",
        f"| N64 source files | {summary['n64_sources']} |",
        "",
        "## Domain Coverage",
        "",
        "| Domain | Mapped | matched-c | structured-port-started | exact-seed-started |",
        "| --- | ---: | ---: | ---: | ---: |",
    ]
    for domain, counts in summary["domains"].items():  # type: ignore[index]
        lines.append(
            f"| `{domain}` | {counts.get('mapped', 0)} | {counts.get('matched-c', 0)} | "
            f"{counts.get('structured-port-started', 0)} | {counts.get('exact-seed-started', 0)} |"
        )

    lines.extend(
        [
            "",
            "## Reused N64 Source Lanes",
            "",
            "| N64 source | N64 function | Mapped OOT3D rows |",
            "| --- | --- | ---: |",
        ]
    )
    if not duplicated:
        lines.append("| - | - | 0 |")
    for row in duplicated:
        lines.append(f"| `{row['n64_source']}` | `{row['n64_name']}` | {row['mapped_rows']} |")

    lines.extend(
        [
            "",
            "## Replicable Conversion Structures",
            "",
            "| Pattern | Category | Confidence | Rows both sides | N64 hits | OOT3D hits | Adapter |",
            "| --- | --- | --- | ---: | ---: | ---: | --- |",
        ]
    )
    for row in patterns:
        lines.append(
            f"| `{row['name']}` | `{row['category']}` | `{row['confidence']}` | "
            f"{row['rows_with_both_sides']} | {row['n64_occurrences']} | {row['oot3d_occurrences']} | "
            f"{row['adapter']} |"
        )

    lines.extend(["", "## Recommended Direct-Conversion Work", ""])
    for row in patterns:
        if row["confidence"] not in {"high", "medium"}:
            continue
        lines.append(f"- `{row['name']}`: {row['next_step']}")

    lines.extend(["", "## Pattern Evidence", ""])
    for row in patterns:
        if row["confidence"] not in {"high", "medium"}:
            continue
        lines.append(f"### `{row['name']}`")
        lines.append("")
        lines.append(f"- N64 evidence: {', '.join(f'`{item}`' for item in row['n64_evidence']) or '-'}")
        lines.append(f"- OOT3D evidence: {', '.join(f'`{item}`' for item in row['oot3d_evidence']) or '-'}")
        lines.append("")

    table_counter(lines, "Top N64 Calls", data["top_n64_calls"])  # type: ignore[arg-type]
    table_counter(lines, "Top N64 Member Paths", data["top_n64_members"])  # type: ignore[arg-type]
    table_counter(lines, "Top N64 Types", data["top_n64_types"])  # type: ignore[arg-type]
    table_counter(lines, "Top OOT3D Helpers", data["top_oot3d_helpers"])  # type: ignore[arg-type]
    table_counter(lines, "Top OOT3D Offsets", data["top_oot3d_offsets"])  # type: ignore[arg-type]
    table_counter(lines, "Top OOT3D Callee Symbols", data["top_oot3d_fun_symbols"])  # type: ignore[arg-type]
    table_counter(lines, "Top OOT3D Data Symbols", data["top_oot3d_data_symbols"])  # type: ignore[arg-type]

    lines.extend(
        [
            "## Interpretation",
            "",
            "The imported corpus does contain reusable conversion structure. The strongest repeated shape is not a",
            "single whole-file mechanical conversion yet; it is an adapter stack: N64 function/source lanes, OOT3D",
            "offset accessor families, recovered engine-call aliases, and named global state packets. Applying that",
            "stack per subsystem should make future ports closer to direct source conversion and reduce one-off",
            "Ghidra inspection.",
            "",
        ]
    )

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map", type=Path, default=ROOT / "metadata" / "n64_port_map.csv")
    parser.add_argument("--n64-out-root", type=Path, default=ROOT / "analysis" / "n64_port_units")
    parser.add_argument("--out-json", type=Path, default=ROOT / "analysis" / "n64_conversion_patterns.json")
    parser.add_argument("--out-md", type=Path, default=ROOT / "analysis" / "n64_conversion_patterns.md")
    parser.add_argument("--out-csv", type=Path, default=ROOT / "analysis" / "n64_conversion_patterns.csv")
    args = parser.parse_args()

    data = analyze_rows(read_csv(args.map), args.n64_out_root)
    write_json(args.out_json, data)
    write_markdown(args.out_md, data)
    write_patterns_csv(args.out_csv, data)

    summary = data["summary"]
    high = sum(1 for row in data["patterns"] if row["confidence"] == "high")  # type: ignore[index]
    medium = sum(1 for row in data["patterns"] if row["confidence"] == "medium")  # type: ignore[index]
    print(
        f"analyzed {summary['mapped_rows']} mapped rows; "
        f"{high} high-confidence and {medium} medium-confidence conversion patterns"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
