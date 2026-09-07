#!/usr/bin/env python3
"""Audit N64 source lanes that can be converted directly into OOT3D-shaped C."""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


ROOT = Path(__file__).resolve().parents[1]

COMMENT_RE = re.compile(r"/\*.*?\*/|//[^\r\n]*", re.DOTALL)
STRING_RE = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
FUN_RE = re.compile(r"\bFUN_[0-9a-fA-F]{8}\b")
DAT_RE = re.compile(r"\bDAT_[0-9a-fA-F]{8}\b")
TARGET_OFFSET_RE = re.compile(r"\bparam_\d+\s*\+\s*(0x[0-9a-fA-F]+|\d+)\b")
TARGET_STORE_RE = re.compile(r"\*\s*\([^)]*\)\s*\(\s*param_\d+\s*\+\s*(0x[0-9a-fA-F]+|\d+)\s*\)")
SOURCE_HELPER_RE = re.compile(r"\boot3d_[A-Za-z0-9_]+\b")
SOURCE_OFFSET_RE = re.compile(r"\bOOT3D_[A-Z0-9_]*OFFSET[A-Z0-9_]*\b")
SOURCE_FUN_RE = re.compile(r"\bFUN_[0-9a-fA-F]{8}\b")
SOURCE_DAT_RE = re.compile(r"\bDAT_[0-9a-fA-F]{8}\b")
MEMBER_RE = re.compile(
    r"\b(?:this|play|player|msgCtx|interfaceCtx|actor|boomerang|boomTarget|floorPoly|wallPoly)"
    r"->(?:[A-Za-z_][A-Za-z0-9_]*)(?:\.[A-Za-z_][A-Za-z0-9_]*)*"
)
CALL_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")
DEFINE_RE = re.compile(r"^\s*#define\s+(OOT3D_[A-Z0-9_]+)\s+(0x[0-9a-fA-F]+|\d+)\b")
TARGET_HEADER_RE = re.compile(r"^//\s+(?P<name>\S+)\s+@\s+(?P<addr>[0-9a-fA-F]+)\s*$")
TARGET_INSN_RE = re.compile(r"^[0-9a-fA-F]+:\s+(?P<op>.+?)\s*$")

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
class Adapter:
    id: str
    category: str
    domain: str
    match_mode: str
    n64_pattern: str
    oot3d_symbol: str
    confidence: str
    notes: str


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def slug(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value).strip("_")


def read_text(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def code_only(text: str) -> str:
    return STRING_RE.sub("", COMMENT_RE.sub("", text))


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})


def load_adapters(path: Path) -> dict[str, Adapter]:
    adapters: dict[str, Adapter] = {}
    for row in read_csv(path):
        adapter = Adapter(
            id=row.get("id", ""),
            category=row.get("category", ""),
            domain=row.get("domain", ""),
            match_mode=row.get("match_mode", ""),
            n64_pattern=row.get("n64_pattern", ""),
            oot3d_symbol=row.get("oot3d_symbol", ""),
            confidence=row.get("confidence", ""),
            notes=row.get("notes", ""),
        )
        if adapter.id:
            adapters[adapter.id] = adapter
    return adapters


def extract_path(row: dict[str, str], out_root: Path) -> Path:
    unit_dir = out_root / slug(row["port_file"])
    prefix = f"{row['oot3d_entry']}_{row['oot3d_name']}__n64_{row['n64_name']}"
    matches = sorted(unit_dir.glob(f"{slug(prefix)}*.c"))
    if matches:
        return matches[0]
    return unit_dir / f"{slug(prefix)}.c"


def target_decompile_path(row: dict[str, str], root: Path) -> Path | None:
    decompiled = root / "ghidra_export" / "decompiled"
    entry = row["oot3d_entry"].lower()
    name = row["oot3d_name"]
    matches = sorted(decompiled.glob(f"*_{entry}_{name}.c"))
    if matches:
        return matches[0]
    matches = sorted(decompiled.glob(f"*_{entry}_FUN_{entry}.c"))
    if matches:
        return matches[0]
    return None


def read_defines(root: Path) -> dict[str, int]:
    defines: dict[str, int] = {}
    for path in sorted((root / "include" / "oot3d").glob("*.h")):
        for line in read_text(path).splitlines():
            match = DEFINE_RE.match(line)
            if not match:
                continue
            defines[match.group(1)] = int(match.group(2), 0)
    return defines


def parse_top_adapters(value: str) -> Counter[str]:
    counter: Counter[str] = Counter()
    for item in value.split():
        if ":" not in item:
            continue
        key, raw_count = item.rsplit(":", 1)
        try:
            counter[key] += int(raw_count)
        except ValueError:
            continue
    return counter


def read_disassembly(path: Path) -> dict[str, dict[str, Any]]:
    functions: dict[str, dict[str, Any]] = {}
    current_name: str | None = None
    current_entry: str | None = None
    ops: list[str] = []

    def flush() -> None:
        if current_name is None or current_entry is None:
            return
        first_op = ops[0] if ops else ""
        functions[current_name] = {
            "entry": current_entry,
            "instruction_count": len(ops),
            "first_op": first_op,
            "saved_registers": saved_registers(first_op),
        }

    for raw_line in read_text(path).splitlines():
        header = TARGET_HEADER_RE.match(raw_line)
        if header:
            flush()
            current_name = header.group("name")
            current_entry = header.group("addr").lower()
            ops = []
            continue
        if current_name is None:
            continue
        if not raw_line.strip():
            flush()
            current_name = None
            current_entry = None
            ops = []
            continue
        insn = TARGET_INSN_RE.match(raw_line)
        if insn:
            ops.append(insn.group("op").strip().lower())
    flush()
    return functions


def saved_registers(first_op: str) -> str:
    match = re.search(r"\{([^{}]+)\}", first_op)
    if not match:
        return ""
    return re.sub(r"\s+", "", match.group(1))


def count_values(values: Iterable[str]) -> Counter[str]:
    counter: Counter[str] = Counter()
    for value in values:
        counter[value] += 1
    return counter


def classify_row(row: dict[str, Any]) -> str:
    status = str(row["status"])
    readiness = str(row["adapter_readiness"])
    target_offsets = int(row["target_direct_offsets"])
    source_helpers = int(row["source_helper_refs"])
    struct_fields = int(row["struct_field_hits"])
    engine_calls = int(row["engine_call_hits"])
    source_lane_group = int(row["same_n64_function_rows"])

    if status == "matched-c":
        return "matched-template"
    if status == "exact-seed-started":
        if source_lane_group > 1:
            return "split-lane-candidate"
        if readiness in {"batch-rich", "adapter-ready"} or (target_offsets >= 50 and (struct_fields >= 8 or engine_calls >= 8)):
            return "exact-seed-direct-candidate"
    if readiness in {"batch-rich", "adapter-ready"} and target_offsets >= 8 and struct_fields >= 8:
        return "direct-batch-candidate"
    if target_offsets >= 5 and source_helpers >= 10 and (struct_fields >= 4 or engine_calls >= 4):
        return "helper-to-direct-candidate"
    if readiness in {"partial", "adapter-ready"} and (struct_fields >= 4 or engine_calls >= 4):
        return "adapter-completion-candidate"
    if source_lane_group > 1:
        return "source-lane-candidate"
    return "manual-reverse-needed"


def row_reason(row: dict[str, Any]) -> str:
    reasons: list[str] = []
    if int(row["target_direct_offsets"]):
        reasons.append(f"{row['target_direct_offsets']} target offsets")
    if int(row["struct_field_hits"]):
        reasons.append(f"{row['struct_field_hits']} struct-field hits")
    if int(row["engine_call_hits"]):
        reasons.append(f"{row['engine_call_hits']} engine-call hits")
    if int(row["data_symbol_hits"]):
        reasons.append(f"{row['data_symbol_hits']} data-symbol hits")
    if int(row["source_helper_refs"]):
        reasons.append(f"{row['source_helper_refs']} source helper refs")
    if int(row["same_n64_function_rows"]) > 1:
        reasons.append(f"{row['same_n64_function_rows']} rows share N64 function")
    return "; ".join(reasons)


def classify_priority(row: dict[str, Any]) -> int:
    class_weight = {
        "direct-batch-candidate": 5,
        "exact-seed-direct-candidate": 5,
        "helper-to-direct-candidate": 4,
        "split-lane-candidate": 4,
        "adapter-completion-candidate": 3,
        "source-lane-candidate": 2,
        "matched-template": 1,
        "manual-reverse-needed": 0,
    }.get(str(row["direct_class"]), 0)
    return (
        class_weight * 1000
        + int(row["distinct_text_adapters"]) * 20
        + int(row["target_direct_offsets"]) * 5
        + int(row["struct_field_hits"])
        + int(row["engine_call_hits"])
    )


def structure_support(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    specs = [
        {
            "id": "member_field_to_direct_offset",
            "title": "N64 member path -> OOT3D direct offset expression",
            "predicate": lambda row: int(row["struct_field_hits"]) >= 4 and int(row["target_direct_offsets"]) >= 3,
            "rewrite": "Rewrite `this->field`/`play->field` with typed `(base + literal_offset)` expressions, not broad helper chains.",
        },
        {
            "id": "engine_call_remap",
            "title": "N64 engine call -> recovered OOT3D callee",
            "predicate": lambda row: int(row["engine_call_hits"]) >= 3 and int(row["target_fun_refs"]) >= 1,
            "rewrite": "Replace stable N64 calls such as Math_*/SkelAnime_* with recovered `FUN_*` callees and OOT3D argument order.",
        },
        {
            "id": "state_packet_global",
            "title": "N64 global/static state -> OOT3D DAT packet",
            "predicate": lambda row: int(row["data_symbol_hits"]) >= 3 and int(row["target_dat_refs"]) >= 1,
            "rewrite": "Promote repeated `DAT_*` packets to named state arrays, then preserve N64 state-machine conditions.",
        },
        {
            "id": "shared_source_lane_split",
            "title": "One N64 function splits into multiple OOT3D target functions",
            "predicate": lambda row: int(row["same_n64_function_rows"]) > 1,
            "rewrite": "Use the N64 function as a lane and slice by target entry, globals, and target decompiled call/offset footprint.",
        },
        {
            "id": "helper_to_decompiler_shape",
            "title": "Current maintained C is too abstract for target codegen",
            "predicate": lambda row: int(row["source_helper_refs"]) >= 10 and int(row["target_direct_offsets"]) >= 5,
            "rewrite": "Lower helper-heavy structured C into target-shaped C before compiler matching attempts.",
        },
        {
            "id": "action_state_slot",
            "title": "Actor action state pointer and small state fields",
            "predicate": lambda row: (
                "action" in str(row["top_adapter_ids"])
                or "burst" in str(row["top_adapter_ids"])
                or int(row["target_direct_stores"]) >= 1
            ),
            "rewrite": "Emit direct stores to action/state slots after naming target action function pointers.",
        },
    ]

    result: list[dict[str, Any]] = []
    for spec in specs:
        supported = [row for row in rows if spec["predicate"](row)]
        matched = [row for row in supported if row["status"] == "matched-c"]
        candidates = [row for row in supported if row["direct_class"] != "matched-template"]
        domains = Counter(str(row["n64_domain"]) for row in supported)
        result.append(
            {
                "id": spec["id"],
                "title": spec["title"],
                "rows": len(supported),
                "matched_templates": len(matched),
                "candidate_rows": len(candidates),
                "domains": " ".join(f"{key}:{value}" for key, value in domains.most_common()),
                "rewrite": spec["rewrite"],
                "examples": ", ".join(f"{row['oot3d_entry']} {row['oot3d_name']}" for row in supported[:5]),
            }
        )
    return result


def analyze(
    map_rows: list[dict[str, str]],
    coverage_rows: list[dict[str, str]],
    adapters: dict[str, Adapter],
    defines: dict[str, int],
    target_disassembly: dict[str, dict[str, Any]],
    n64_out_root: Path,
) -> dict[str, Any]:
    coverage_by_entry = {row["oot3d_entry"].lower(): row for row in coverage_rows}
    source_cache: dict[str, str] = {}
    source_code_cache: dict[str, str] = {}
    n64_group_counts = Counter((row["n64_source"], row["n64_name"]) for row in map_rows)
    gate_by_name = {
        row.get("oot3d_name", ""): row for row in read_csv(ROOT / "analysis" / "structured_c_match_gate.csv")
    }
    exact_compare_rows = {
        row.get("name", ""): row
        for row in json.loads(read_text(ROOT / "build" / "matched" / "compare_matched_objects.json") or "[]")
    }

    rows: list[dict[str, Any]] = []
    recipe_hits: Counter[str] = Counter()
    missing_high_value_adapters: Counter[str] = Counter()
    direct_field_symbols: Counter[str] = Counter()

    for map_row in map_rows:
        entry = map_row["oot3d_entry"].lower()
        coverage = coverage_by_entry.get(entry, {})
        adapter_counts = parse_top_adapters(coverage.get("top_adapters", ""))
        adapter_categories: Counter[str] = Counter()
        high_hits = 0
        known_direct_symbols = 0

        for adapter_id, count in adapter_counts.items():
            adapter = adapters.get(adapter_id)
            if adapter is None:
                missing_high_value_adapters[adapter_id] += count
                continue
            adapter_categories[adapter.category] += count
            if adapter.confidence == "high":
                high_hits += count
            if adapter.category == "struct-field":
                if adapter.oot3d_symbol in defines or adapter.oot3d_symbol.startswith("OOT3D_"):
                    known_direct_symbols += count
                    direct_field_symbols[adapter.oot3d_symbol] += count

        n64_path = extract_path(map_row, n64_out_root)
        n64_code = code_only(read_text(n64_path))
        source_text = source_cache.setdefault(map_row["port_file"], read_text(ROOT / map_row["port_file"]))
        source_code = source_code_cache.setdefault(map_row["port_file"], code_only(source_text))
        target_path = target_decompile_path(map_row, ROOT)
        target_code = code_only(read_text(target_path)) if target_path else ""
        target_offsets = TARGET_OFFSET_RE.findall(target_code)
        target_disasm = target_disassembly.get(map_row["oot3d_name"], {})
        gate = gate_by_name.get(map_row["oot3d_name"], {})
        default_compare = exact_compare_rows.get(map_row["oot3d_name"], {})

        n64_calls = [call for call in CALL_RE.findall(n64_code) if call not in IGNORE_CALLS]
        n64_members = MEMBER_RE.findall(n64_code)
        source_helpers = SOURCE_HELPER_RE.findall(source_code)
        source_offsets = SOURCE_OFFSET_RE.findall(source_code)
        source_fun = SOURCE_FUN_RE.findall(source_code)
        source_dat = SOURCE_DAT_RE.findall(source_code)
        if map_row["status"] == "exact-seed-started":
            source_helpers = []
            source_offsets = []
            source_fun = []
            source_dat = []
        target_fun = FUN_RE.findall(target_code)
        target_dat = DAT_RE.findall(target_code)
        source_lane_rows = n64_group_counts[(map_row["n64_source"], map_row["n64_name"])]

        row: dict[str, Any] = {
            "oot3d_entry": map_row["oot3d_entry"],
            "oot3d_name": map_row["oot3d_name"],
            "n64_source": map_row["n64_source"],
            "n64_name": map_row["n64_name"],
            "port_file": map_row["port_file"],
            "status": map_row["status"],
            "n64_domain": coverage.get("n64_domain", ""),
            "adapter_readiness": coverage.get("readiness", "none"),
            "text_hits": int(coverage.get("text_hits", 0) or 0),
            "distinct_text_adapters": int(coverage.get("distinct_text_adapters", 0) or 0),
            "high_confidence_hits": int(coverage.get("high_confidence_hits", high_hits) or 0),
            "struct_field_hits": adapter_categories["struct-field"],
            "engine_call_hits": adapter_categories["engine-call"],
            "subsystem_call_hits": adapter_categories["subsystem-call"],
            "data_symbol_hits": adapter_categories["data-symbol"],
            "source_lane_hits": int(coverage.get("source_lane_hits", 0) or 0),
            "known_direct_field_hits": known_direct_symbols,
            "target_decompile": rel(target_path) if target_path else "",
            "target_direct_offsets": len(target_offsets),
            "target_unique_offsets": len(set(target_offsets)),
            "target_direct_stores": len(TARGET_STORE_RE.findall(target_code)),
            "target_fun_refs": len(target_fun),
            "target_dat_refs": len(target_dat),
            "target_instruction_count": int(
                gate.get("target_instruction_count")
                or default_compare.get("target_instruction_count")
                or target_disasm.get("instruction_count")
                or 0
            ),
            "target_saved_registers": target_disasm.get("saved_registers", ""),
            "compiled_instruction_count": int(gate.get("compiled_instruction_count") or default_compare.get("compiled_instruction_count") or 0),
            "lcs_instruction_count": int(gate.get("lcs_instruction_count") or default_compare.get("lcs_instruction_count") or 0),
            "exact_match": str(gate.get("exact_match") or default_compare.get("exact_match") or "").lower() == "true",
            "n64_call_refs": len(n64_calls),
            "n64_member_refs": len(n64_members),
            "source_helper_refs": len(source_helpers),
            "source_offset_refs": len(source_offsets),
            "source_fun_refs": len(source_fun),
            "source_dat_refs": len(source_dat),
            "same_n64_function_rows": source_lane_rows,
            "top_adapter_ids": " ".join(f"{key}:{value}" for key, value in adapter_counts.most_common(8)),
            "top_target_offsets": " ".join(f"{key}:{value}" for key, value in Counter(target_offsets).most_common(8)),
            "n64_extract": rel(n64_path),
        }
        row["direct_class"] = classify_row(row)
        row["reason"] = row_reason(row)
        row["priority"] = classify_priority(row)
        rows.append(row)

        if row["direct_class"] != "manual-reverse-needed":
            recipe_hits[row["direct_class"]] += 1

    rows.sort(key=lambda row: (-int(row["priority"]), row["oot3d_entry"]))
    structures = structure_support(rows)
    return {
        "summary": {
            "mapped_rows": len(map_rows),
            "target_decompiles_found": sum(1 for row in rows if row["target_decompile"]),
            "rows_with_target_direct_offsets": sum(1 for row in rows if int(row["target_direct_offsets"]) > 0),
            "rows_with_known_direct_field_hits": sum(1 for row in rows if int(row["known_direct_field_hits"]) > 0),
            "direct_batch_candidates": sum(1 for row in rows if row["direct_class"] == "direct-batch-candidate"),
            "exact_seed_direct_candidates": sum(1 for row in rows if row["direct_class"] == "exact-seed-direct-candidate"),
            "helper_to_direct_candidates": sum(1 for row in rows if row["direct_class"] == "helper-to-direct-candidate"),
            "split_lane_candidates": sum(1 for row in rows if row["direct_class"] == "split-lane-candidate"),
            "matched_templates": sum(1 for row in rows if row["direct_class"] == "matched-template"),
            "recipe_classes": dict(sorted(recipe_hits.items())),
        },
        "structures": structures,
        "rows": rows,
        "direct_field_symbols": [
            {"symbol": key, "hits": value, "offset": f"0x{defines[key]:x}" if key in defines else ""}
            for key, value in direct_field_symbols.most_common()
        ],
        "missing_high_value_adapters": [
            {"adapter": key, "hits": value} for key, value in missing_high_value_adapters.most_common()
        ],
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    structures = data["structures"]
    rows = data["rows"]
    priority_rows = [row for row in rows if row["direct_class"] not in {"matched-template", "manual-reverse-needed"}][:12]
    matched_rows = [row for row in rows if row["direct_class"] == "matched-template"][:8]

    lines = [
        "# Direct N64 Source Conversion Audit",
        "",
        "This audit checks whether imported N64 source can be lowered into OOT3D-shaped C directly, using adapter coverage, Ghidra target decompiles, target disassembly, and current maintained C.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Mapped N64/OOT3D rows | {summary['mapped_rows']} |",
        f"| Target decompiles found | {summary['target_decompiles_found']} |",
        f"| Rows with target direct offsets | {summary['rows_with_target_direct_offsets']} |",
        f"| Rows with known direct field hits | {summary['rows_with_known_direct_field_hits']} |",
        f"| Direct batch candidates | {summary['direct_batch_candidates']} |",
        f"| Exact-seed direct candidates | {summary['exact_seed_direct_candidates']} |",
        f"| Helper-to-direct candidates | {summary['helper_to_direct_candidates']} |",
        f"| Split-lane candidates | {summary['split_lane_candidates']} |",
        f"| Matched templates | {summary['matched_templates']} |",
        "",
        "## Common Replicable Structures",
        "",
        "| Structure | Rows | Matched templates | Candidate rows | Domains | Rewrite direction | Examples |",
        "| --- | ---: | ---: | ---: | --- | --- | --- |",
    ]

    for structure in structures:
        lines.append(
            "| `{id}` {title} | {rows} | {matched_templates} | {candidate_rows} | `{domains}` | {rewrite} | {examples} |".format(
                **structure
            )
        )

    lines.extend(
        [
            "",
            "## Highest Priority Direct-Conversion Rows",
            "",
            "| Class | Readiness | OOT3D | N64 source lane | Target offsets | Struct | Engine | Data | Source helpers | Match | Reason |",
            "| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | --- | --- |",
        ]
    )

    for row in priority_rows:
        if row["status"] == "exact-seed-started":
            match_text = "seed exact only"
        elif row["target_instruction_count"]:
            match_text = f"{row['lcs_instruction_count']}/{row['target_instruction_count']}"
        else:
            match_text = ""
        lines.append(
            "| `{direct_class}` | `{adapter_readiness}` | `{oot3d_entry}` `{oot3d_name}` | `{n64_name}` | "
            "{target_direct_offsets} | {struct_field_hits} | {engine_call_hits} | {data_symbol_hits} | "
            "{source_helper_refs} | {match_text} | {reason} |".format(match_text=match_text, **row)
        )

    lines.extend(
        [
            "",
            "## Matched Templates To Mine",
            "",
            "| OOT3D | N64 source lane | Target offsets | Struct | Engine | Data | Direct class |",
            "| --- | --- | ---: | ---: | ---: | ---: | --- |",
        ]
    )

    for row in matched_rows:
        lines.append(
            "| `{oot3d_entry}` `{oot3d_name}` | `{n64_name}` | {target_direct_offsets} | "
            "{struct_field_hits} | {engine_call_hits} | {data_symbol_hits} | `{direct_class}` |".format(**row)
        )

    lines.extend(
        [
            "",
            "## Concrete Converter Shape",
            "",
            "The fastest path is a target-shaped converter, not a pretty C converter:",
            "",
            "1. Start from the extracted N64 function in `analysis/n64_port_units/`.",
            "2. Apply adapter-token rewrites for known engine calls, data packets, and struct fields.",
            "3. Lower struct fields to typed direct offset expressions matching the Ghidra target form (`param + literal_offset`) when the target decompile confirms the offset.",
            "4. Keep same-N64-function lanes as split candidates and slice by target offset/call/DAT footprint.",
            "5. Run the existing compiler gate; only then polish names or wrappers if they do not disturb codegen.",
            "",
            "### Case Study: `00399178 oot3d_boss_va_zapper_damaged`",
            "",
            "- N64 logic ports directly from `BossVa_ZapperDamaged`.",
            "- Target decompile uses direct offsets such as `param_1 + 0xfea`, `0xfe8`, `0xff0`, `0xfee`, `0xff6`, `0x21c`, `0x1a4`, `0xf95`, and `0xf90`.",
            "- Current maintained C uses helper/offset wrappers heavily; the compiled prologue saves extra registers compared with target.",
            "- Therefore this is a `helper-to-direct` proving row: lower the helper-heavy BossVa source into decompiler-shaped C before trying more compiler-profile searches.",
            "",
        ]
    )

    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port-map", type=Path, default=ROOT / "metadata" / "n64_port_map.csv")
    parser.add_argument("--coverage", type=Path, default=ROOT / "analysis" / "n64_adapter_coverage.csv")
    parser.add_argument("--adapters", type=Path, default=ROOT / "metadata" / "n64_to_oot3d_adapters.csv")
    parser.add_argument("--n64-out-root", type=Path, default=ROOT / "analysis" / "n64_port_units")
    parser.add_argument("--target-disassembly", type=Path, default=ROOT / "ghidra_export" / "disassembly.txt")
    parser.add_argument("--out-json", type=Path, default=ROOT / "analysis" / "direct_source_conversion_audit.json")
    parser.add_argument("--out-md", type=Path, default=ROOT / "analysis" / "direct_source_conversion_audit.md")
    parser.add_argument("--out-rows-csv", type=Path, default=ROOT / "analysis" / "direct_source_conversion_rows.csv")
    parser.add_argument(
        "--out-structures-csv",
        type=Path,
        default=ROOT / "analysis" / "direct_source_conversion_structures.csv",
    )
    args = parser.parse_args()

    data = analyze(
        read_csv(args.port_map),
        read_csv(args.coverage),
        load_adapters(args.adapters),
        read_defines(ROOT),
        read_disassembly(args.target_disassembly),
        args.n64_out_root,
    )

    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    args.out_json.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    write_markdown(args.out_md, data)
    write_csv(
        args.out_rows_csv,
        data["rows"],
        [
            "direct_class",
            "priority",
            "adapter_readiness",
            "oot3d_entry",
            "oot3d_name",
            "n64_source",
            "n64_name",
            "status",
            "n64_domain",
            "text_hits",
            "distinct_text_adapters",
            "high_confidence_hits",
            "struct_field_hits",
            "engine_call_hits",
            "subsystem_call_hits",
            "data_symbol_hits",
            "known_direct_field_hits",
            "target_direct_offsets",
            "target_unique_offsets",
            "target_direct_stores",
            "target_fun_refs",
            "target_dat_refs",
            "target_instruction_count",
            "target_saved_registers",
            "compiled_instruction_count",
            "lcs_instruction_count",
            "source_helper_refs",
            "source_offset_refs",
            "source_fun_refs",
            "source_dat_refs",
            "same_n64_function_rows",
            "top_adapter_ids",
            "top_target_offsets",
            "reason",
            "port_file",
            "n64_extract",
            "target_decompile",
        ],
    )
    write_csv(
        args.out_structures_csv,
        data["structures"],
        ["id", "title", "rows", "matched_templates", "candidate_rows", "domains", "rewrite", "examples"],
    )

    summary = data["summary"]
    print(
        "direct audit: "
        f"{summary['direct_batch_candidates']} direct batch, "
        f"{summary['exact_seed_direct_candidates']} exact-seed direct, "
        f"{summary['helper_to_direct_candidates']} helper-to-direct, "
        f"{summary['split_lane_candidates']} split-lane candidates"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
