#!/usr/bin/env python3
"""Build a single queue for C-convertible N64-derived OOT3D ports.

This audit joins the structured compile gate, direct-conversion rows, matched
baseline results, materialized workorders, and source implementation style.  It
answers the practical question: which N64-derived ports are real C promotion
candidates now, which inline asm exact matches are maintained C, and which
normal functions still contain full inline asm exact seeds awaiting C replacement?
"""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_GATE = ROOT / "analysis" / "structured_c_match_gate.csv"
DEFAULT_DIRECT_ROWS = ROOT / "analysis" / "direct_source_conversion_rows.csv"
DEFAULT_N64_MAP = ROOT / "metadata" / "n64_port_map.csv"
DEFAULT_MATCHED_SOURCES = ROOT / "metadata" / "matched_sources.txt"
DEFAULT_MATCHED_COMPARE = ROOT / "build" / "matched" / "compare_matched_objects.json"
DEFAULT_MATERIALIZED = ROOT / "analysis" / "direct_conversion_materialized"
DEFAULT_IN_SOURCE_PROBE = ROOT / "analysis" / "in_source_c_reconstruction_probe.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "c_conversion_readiness.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "c_conversion_readiness.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "c_conversion_readiness.md"


STYLE_ORDER = {
    "plain-c": 0,
    "c-inline-asm": 1,
    "naked-asm": 2,
    "missing-source": 3,
    "unknown": 4,
}

BASELINE_DEFINES = {"__arm__", "__GNUC__"}


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def read_json(path: Path, default: Any) -> Any:
    if not path.is_file():
        return default
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def read_source_list(path: Path) -> set[str]:
    sources: set[str] = set()
    if not path.is_file():
        return sources
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        if line and not line.startswith("#"):
            sources.add(line.replace("\\", "/"))
    return sources


def int_value(row: dict[str, Any], key: str) -> int:
    try:
        return int(row.get(key, 0) or 0)
    except (TypeError, ValueError):
        return 0


def float_value(row: dict[str, Any], key: str) -> float:
    try:
        return float(row.get(key, 0) or 0)
    except (TypeError, ValueError):
        return 0.0


def matched_compare_index(path: Path) -> dict[str, dict[str, Any]]:
    rows = read_json(path, [])
    if not isinstance(rows, list):
        return {}
    return {str(row.get("name", "")): row for row in rows if row.get("name")}


def gate_index(rows: list[dict[str, str]]) -> dict[str, dict[str, str]]:
    return {str(row.get("oot3d_entry", "")).lower(): row for row in rows if row.get("oot3d_entry")}


def n64_map_index(rows: list[dict[str, str]]) -> dict[str, dict[str, str]]:
    return {str(row.get("oot3d_entry", "")).lower(): row for row in rows if row.get("oot3d_entry")}


def materialized_index(root: Path) -> dict[str, dict[str, str]]:
    index: dict[str, dict[str, str]] = {}
    if not root.is_dir():
        return index
    for report_path in root.glob("*/*/rewrite_report.json"):
        report = read_json(report_path, {})
        if not isinstance(report, dict):
            continue
        entry = str(report.get("entry", "")).lower()
        if not entry:
            continue
        index[entry] = {
            "materialized_status": str(report.get("status", "")),
            "materialized_output": str(report.get("output", "")),
            "materialized_rewrites": str(report.get("rewrite_count", "")),
            "materialized_adapters": str(report.get("adapter_count", "")),
            "materialized_shape_anchors": str(len(report.get("shape_anchors", []) or [])),
        }
    return index


def in_source_probe_index(path: Path) -> dict[str, dict[str, str]]:
    data = read_json(path, {})
    if not isinstance(data, dict):
        return {}
    rows = data.get("rows", [])
    if not isinstance(rows, list):
        return {}
    index: dict[str, dict[str, str]] = {}
    for row in rows:
        if not isinstance(row, dict):
            continue
        function = str(row.get("function", ""))
        if not function:
            continue
        index[function] = {
            "in_source_probe_category": str(row.get("category", "")),
            "in_source_probe_define": str(row.get("define", "")),
            "in_source_probe_source": str(row.get("variant_source", "")),
            "in_source_probe_style": str(row.get("variant_style", "")),
            "in_source_probe_lcs": str(row.get("lcs_instruction_count", "")),
            "in_source_probe_ratio": str(row.get("lcs_target_ratio", "")),
        }
    return index


def skip_ws(text: str, index: int) -> int:
    while index < len(text) and text[index].isspace():
        index += 1
    return index


def matching_paren(text: str, open_index: int) -> int:
    depth = 0
    index = open_index
    while index < len(text):
        char = text[index]
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return index
        index += 1
    return -1


def matching_brace(text: str, open_index: int) -> int:
    depth = 0
    index = open_index
    while index < len(text):
        char = text[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return index
        index += 1
    return -1


def find_function_definition(text: str, name: str) -> tuple[str, str] | None:
    pattern = re.compile(rf"\b{re.escape(name)}\s*\(")
    for match in pattern.finditer(text):
        open_paren = text.find("(", match.start())
        close_paren = matching_paren(text, open_paren)
        if close_paren < 0:
            continue
        after = skip_ws(text, close_paren + 1)
        if after >= len(text) or text[after] == ";":
            continue
        if text[after : after + 11].startswith("__asm__"):
            continue
        if text[after] != "{":
            continue
        close_brace = matching_brace(text, after)
        if close_brace < 0:
            continue
        header_start = text.rfind("\n", 0, match.start()) + 1
        header_prefix = text[max(0, header_start - 240) : after]
        return header_prefix, text[after : close_brace + 1]
    return None


def directive_condition(line: str, active_defines: set[str]) -> bool | None:
    stripped = line.strip()
    match = re.match(r"#\s*if\s+defined\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)", stripped)
    if match:
        return match.group(1) in active_defines
    match = re.match(r"#\s*if\s+!\s*defined\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)", stripped)
    if match:
        return match.group(1) not in active_defines
    match = re.match(r"#\s*ifdef\s+([A-Za-z_][A-Za-z0-9_]*)", stripped)
    if match:
        return match.group(1) in active_defines
    match = re.match(r"#\s*ifndef\s+([A-Za-z_][A-Za-z0-9_]*)", stripped)
    if match:
        return match.group(1) not in active_defines
    return None


def strip_inactive_default_preprocessor(text: str, active_defines: set[str]) -> str:
    frames: list[dict[str, bool]] = []
    output: list[str] = []
    current_active = True

    for line in text.splitlines(keepends=True):
        condition = directive_condition(line, active_defines)
        stripped = line.strip()
        if condition is not None:
            frames.append({"parent_active": current_active, "condition": condition, "active": current_active and condition})
            current_active = frames[-1]["active"]
            if current_active:
                output.append(line)
            continue
        if re.match(r"#\s*else\b", stripped):
            if frames:
                frame = frames[-1]
                frame["active"] = frame["parent_active"] and not frame["condition"]
                current_active = frame["active"]
                if current_active:
                    output.append(line)
            continue
        if re.match(r"#\s*endif\b", stripped):
            if frames:
                frames.pop()
                current_active = frames[-1]["active"] if frames else True
                if current_active:
                    output.append(line)
            continue
        if current_active:
            output.append(line)
    return "".join(output)


def expand_local_includes(
    text: str,
    source_path: Path,
    active_defines: set[str],
    seen: set[Path] | None = None,
) -> str:
    if seen is None:
        seen = set()
    output: list[str] = []
    include_re = re.compile(r'^\s*#\s*include\s+"([^"]+)"')
    for line in text.splitlines(keepends=True):
        match = include_re.match(line)
        if not match:
            output.append(line)
            continue
        include_path = (source_path.parent / match.group(1)).resolve()
        if include_path in seen or not include_path.is_file():
            output.append(line)
            continue
        seen.add(include_path)
        included = include_path.read_text(encoding="utf-8", errors="replace")
        included = strip_inactive_default_preprocessor(included, active_defines)
        output.append(expand_local_includes(included, include_path, active_defines, seen))
    return "".join(output)


def cflag_defines(extra_cflag: str) -> set[str]:
    defines: set[str] = set()
    for token in str(extra_cflag or "").split():
        if not token.startswith("-D"):
            continue
        name = token[2:].split("=", 1)[0]
        if name:
            defines.add(name)
    return defines


def source_style(source: str, function_name: str, active_defines: set[str] | None = None) -> str:
    path = ROOT / source
    if not source or not path.is_file():
        return "missing-source"
    defines = set(BASELINE_DEFINES if active_defines is None else active_defines)
    text = path.read_text(encoding="utf-8", errors="replace")
    text = strip_inactive_default_preprocessor(text, defines)
    text = expand_local_includes(text, path.resolve(), defines)
    definition = find_function_definition(text, function_name)
    if definition is None:
        return "unknown"
    header, body = definition
    declaration = header[header.rfind("\n") + 1 :]
    if "OOT3D_NAKED" in declaration or "__attribute__((naked))" in declaration:
        return "naked-asm"
    if "__asm__" in body or re.search(r"(?<![A-Za-z0-9_])asm\s*(?:volatile)?\s*(?:\(|$)", body):
        return "c-inline-asm"
    return "plain-c"


def implementation_note(style: str) -> str:
    if style == "plain-c":
        return "plain compiler-shaped C"
    if style == "c-inline-asm":
        return "C body with inline asm constraints for codegen"
    if style == "naked-asm":
        return "exact seed / naked asm, not reconstructed C"
    if style == "missing-source":
        return "source file missing"
    return "function definition not found in source"


def classify(row: dict[str, Any]) -> tuple[str, str]:
    direct_class = str(row.get("direct_class", ""))
    map_status = str(row.get("map_status", ""))
    gate_category = str(row.get("gate_category", ""))
    in_matched = row.get("in_matched_sources") == "yes"
    baseline_exact = row.get("matched_baseline_exact") == "yes"
    style = str(row.get("implementation_style", ""))
    materialized_status = str(row.get("materialized_status", ""))
    source_helpers = int_value(row, "source_helper_refs")
    exact_seed_class = (
        direct_class
        in {
            "exact-seed-direct-candidate",
            "split-lane-candidate",
            "target-split-exact-seed",
        }
        or map_status == "target-split-exact-seed"
        or (map_status == "structured-port-started" and gate_category != "exact-c")
    )

    if gate_category == "exact-c" and not in_matched:
        return "promote-now", "Exact compiled C is outside the matched baseline; add the file to matched_sources and rebuild."

    if baseline_exact and style in {"plain-c", "c-inline-asm"}:
        if style == "plain-c":
            return "matched-c", "Already exact in the matched baseline as compiler-generated C."
        if exact_seed_class:
            return (
                "matched-inline-asm-seed",
                "Already exact as a normal function, but still backed by a full inline asm exact seed.",
            )
        return "matched-c-inline-asm", "Already exact, but still uses inline asm constraints; keep as matched while reducing asm later."

    if baseline_exact and style == "naked-asm":
        return "matched-exact-seed-asm", "Already exact only because a naked asm seed is present; reconstruct target-shaped C before counting it as C-converted."

    if direct_class in {"direct-batch-candidate", "helper-to-direct-candidate"}:
        if materialized_status == "ok":
            return "batch-lowering-candidate", "Materialized adapter packet exists; lower helper/member accesses into target-shaped direct offsets, then compile-gate the lane."
        return "materialize-first", "Generate the direct conversion packet before source promotion."

    if direct_class in {"exact-seed-direct-candidate", "split-lane-candidate"}:
        return "exact-seed-c-reconstruction", "The target is matched or seeded by exact asm; replace the seed with N64-derived target-shaped C in the same lane."

    if gate_category in {"codegen-near", "structural-near"}:
        return "near-candidate", "Run source-shape rewrites, prologue/profile search, then re-run the structured C gate."

    if gate_category == "semantic-started" and source_helpers > 0:
        return "helper-lowering-needed", "Structured source compiles but helper abstraction is still too high for target codegen."

    if direct_class == "adapter-completion-candidate":
        return "adapter-completion-needed", "Complete missing high-hit adapters before another C conversion attempt."

    return "not-convertible-yet", "Needs semantic reconstruction or more N64/OOT3D mapping evidence before C promotion."


def blocker(row: dict[str, Any], status: str) -> str:
    first_difference = str(row.get("first_difference", ""))
    if status in {"matched-c", "matched-c-inline-asm", "promote-now"}:
        return "none"
    if status == "matched-inline-asm-seed":
        if row.get("in_source_probe_category"):
            return f"in-source C probe is {row['in_source_probe_category']}; full inline asm remains exact baseline"
        return "full inline asm exact seed hides missing C reconstruction"
    if status == "matched-exact-seed-asm":
        if row.get("in_source_probe_category"):
            return f"in-source C probe is {row['in_source_probe_category']}; naked asm remains exact baseline"
        return "naked asm exact seed hides missing C reconstruction"
    if status == "batch-lowering-candidate":
        return "target-shaped direct-offset lowering not applied to maintained source"
    if status == "exact-seed-c-reconstruction":
        return "exact seed must be replaced by N64-derived target-shaped C"
    if "vpush" in first_difference:
        return "compiler prologue/register allocation still diverges after shape anchors"
    if int_value(row, "shape_rewrite_sites") > 0:
        return "shape anchors are known but not fully expressed in maintained C"
    if status == "adapter-completion-needed":
        return "missing adapters for high-hit N64 fields/calls/data"
    return "semantic or type model gap"


def next_action(row: dict[str, Any], status: str) -> str:
    if status == "promote-now":
        return "Promote the source into metadata/matched_sources.txt and rebuild matched objects."
    if status in {"matched-c", "matched-c-inline-asm"}:
        return "No promotion action; keep exact baseline and optionally reduce inline asm later."
    if status == "matched-inline-asm-seed":
        if row.get("in_source_probe_category"):
            return "Refine the guarded in-source C reconstruction against ABI/frame shape before replacing the seed."
        return "Use the materialized packet/extracted N64 source to replace the full inline asm seed with target-shaped C."
    if status == "matched-exact-seed-asm":
        if row.get("in_source_probe_category"):
            return "Refine the guarded in-source C reconstruction against ABI/frame shape before replacing the seed."
        return "Use the materialized packet/extracted N64 source to replace the naked seed with target-shaped C."
    if status == "batch-lowering-candidate":
        return "Apply field/call/data adapters to maintained C, emit target base anchors, compile with the structured gate."
    if status == "exact-seed-c-reconstruction":
        return "Port the N64 lane into C against the seeded target, then compare before deleting the seed."
    if status == "near-candidate":
        return "Run profile/prologue search and local source-shape rewrites."
    if status == "adapter-completion-needed":
        return "Add missing adapter rows for top uncovered symbols, re-materialize, then gate."
    return "Keep in analysis queue until semantic mapping improves."


def build_rows(args: argparse.Namespace) -> list[dict[str, Any]]:
    gate_by_entry = gate_index(read_csv(args.structured_gate))
    n64_map_by_entry = n64_map_index(read_csv(args.n64_map))
    matched_sources = read_source_list(args.matched_sources)
    matched_by_name = matched_compare_index(args.matched_compare)
    materialized = materialized_index(args.materialized_dir)
    in_source_probes = in_source_probe_index(args.in_source_probe)

    rows: list[dict[str, Any]] = []
    seen_entries: set[str] = set()
    for direct in read_csv(args.direct_rows):
        entry = str(direct.get("oot3d_entry", "")).lower()
        if not entry:
            continue
        gate = gate_by_entry.get(entry, {})
        name = direct.get("oot3d_name", "")
        source = direct.get("port_file", "").replace("\\", "/")
        matched = matched_by_name.get(name, {})
        matched_exact = bool(matched.get("exact_match"))
        mapped = n64_map_by_entry.get(entry, {})
        style_defines = set(BASELINE_DEFINES)
        if not matched_exact:
            style_defines.update(cflag_defines(gate.get("extra_cflag", "")))
        materialized_row = materialized.get(entry, {})
        in_source_probe = in_source_probes.get(name, {})
        row: dict[str, Any] = {
            "oot3d_entry": entry,
            "oot3d_name": name,
            "n64_name": direct.get("n64_name", ""),
            "port_file": source,
            "direct_class": direct.get("direct_class", ""),
            "map_status": mapped.get("status", ""),
            "adapter_readiness": direct.get("adapter_readiness", ""),
            "gate_category": gate.get("category", ""),
            "in_matched_sources": "yes" if source in matched_sources else "no",
            "matched_baseline_exact": "yes" if matched_exact else "no",
            "implementation_style": source_style(source, name, style_defines),
            "target_instruction_count": gate.get("target_instruction_count")
            or matched.get("target_instruction_count")
            or direct.get("target_instruction_count", "0"),
            "compiled_instruction_count": gate.get("compiled_instruction_count")
            or matched.get("compiled_instruction_count")
            or direct.get("compiled_instruction_count", "0"),
            "lcs_instruction_count": gate.get("lcs_instruction_count")
            or matched.get("lcs_instruction_count")
            or direct.get("lcs_instruction_count", "0"),
            "lcs_target_ratio": gate.get("lcs_target_ratio") or direct.get("lcs_target_ratio", ""),
            "first_difference": gate.get("first_difference", ""),
            "shape_rewrite_sites": direct.get("target_direct_stores", "0"),
            "source_helper_refs": direct.get("source_helper_refs", "0"),
            "materialized_status": materialized_row.get("materialized_status", ""),
            "materialized_output": materialized_row.get("materialized_output", ""),
            "materialized_rewrites": materialized_row.get("materialized_rewrites", ""),
            "materialized_adapters": materialized_row.get("materialized_adapters", ""),
            "materialized_shape_anchors": materialized_row.get("materialized_shape_anchors", ""),
            "in_source_probe_category": in_source_probe.get("in_source_probe_category", ""),
            "in_source_probe_define": in_source_probe.get("in_source_probe_define", ""),
            "in_source_probe_source": in_source_probe.get("in_source_probe_source", ""),
            "in_source_probe_style": in_source_probe.get("in_source_probe_style", ""),
            "in_source_probe_lcs": in_source_probe.get("in_source_probe_lcs", ""),
            "in_source_probe_ratio": in_source_probe.get("in_source_probe_ratio", ""),
            "n64_extract": direct.get("n64_extract", ""),
        }
        status, reason = classify(row)
        row["conversion_status"] = status
        row["reason"] = reason
        row["blocker"] = blocker(row, status)
        row["next_action"] = next_action(row, status)
        row["implementation_note"] = implementation_note(str(row["implementation_style"]))
        rows.append(row)
        seen_entries.add(entry)

    for mapped in n64_map_by_entry.values():
        entry = str(mapped.get("oot3d_entry", "")).lower()
        if not entry or entry in seen_entries or mapped.get("status") != "target-split-exact-seed":
            continue
        name = mapped.get("oot3d_name", "")
        source = mapped.get("port_file", "").replace("\\", "/")
        matched = matched_by_name.get(name, {})
        matched_exact = bool(matched.get("exact_match"))
        in_source_probe = in_source_probes.get(name, {})
        row = {
            "oot3d_entry": entry,
            "oot3d_name": name,
            "n64_name": mapped.get("n64_name", ""),
            "port_file": source,
            "direct_class": "target-split-exact-seed",
            "map_status": mapped.get("status", ""),
            "adapter_readiness": "",
            "gate_category": "",
            "in_matched_sources": "yes" if source in matched_sources else "no",
            "matched_baseline_exact": "yes" if matched_exact else "no",
            "implementation_style": source_style(source, name),
            "target_instruction_count": matched.get("target_instruction_count", "0"),
            "compiled_instruction_count": matched.get("compiled_instruction_count", "0"),
            "lcs_instruction_count": matched.get("lcs_instruction_count", "0"),
            "lcs_target_ratio": matched.get("lcs_target_ratio", ""),
            "first_difference": matched.get("first_difference", ""),
            "shape_rewrite_sites": "0",
            "source_helper_refs": "0",
            "materialized_status": "",
            "materialized_output": "",
            "materialized_rewrites": "",
            "materialized_adapters": "",
            "materialized_shape_anchors": "",
            "in_source_probe_category": in_source_probe.get("in_source_probe_category", ""),
            "in_source_probe_define": in_source_probe.get("in_source_probe_define", ""),
            "in_source_probe_source": in_source_probe.get("in_source_probe_source", ""),
            "in_source_probe_style": in_source_probe.get("in_source_probe_style", ""),
            "in_source_probe_lcs": in_source_probe.get("in_source_probe_lcs", ""),
            "in_source_probe_ratio": in_source_probe.get("in_source_probe_ratio", ""),
            "n64_extract": "",
        }
        status, reason = classify(row)
        row["conversion_status"] = status
        row["reason"] = reason
        row["blocker"] = blocker(row, status)
        row["next_action"] = next_action(row, status)
        row["implementation_note"] = implementation_note(str(row["implementation_style"]))
        rows.append(row)
        seen_entries.add(entry)

    for entry, gate in sorted(gate_by_entry.items()):
        if entry in seen_entries:
            continue
        source = gate.get("port_file", "").replace("\\", "/")
        name = gate.get("oot3d_name", "")
        matched = matched_by_name.get(name, {})
        matched_exact = bool(matched.get("exact_match"))
        mapped = n64_map_by_entry.get(entry, {})
        style_defines = set(BASELINE_DEFINES)
        if not matched_exact:
            style_defines.update(cflag_defines(gate.get("extra_cflag", "")))
        in_source_probe = in_source_probes.get(name, {})
        row = {
            "oot3d_entry": entry,
            "oot3d_name": name,
            "n64_name": gate.get("n64_name", ""),
            "port_file": source,
            "direct_class": "",
            "map_status": mapped.get("status", ""),
            "adapter_readiness": "",
            "gate_category": gate.get("category", ""),
            "in_matched_sources": "yes" if source in matched_sources else "no",
            "matched_baseline_exact": "yes" if matched_exact else "no",
            "implementation_style": source_style(source, name, style_defines),
            "target_instruction_count": gate.get("target_instruction_count", "0"),
            "compiled_instruction_count": gate.get("compiled_instruction_count", "0"),
            "lcs_instruction_count": gate.get("lcs_instruction_count", "0"),
            "lcs_target_ratio": gate.get("lcs_target_ratio", ""),
            "first_difference": gate.get("first_difference", ""),
            "shape_rewrite_sites": "0",
            "source_helper_refs": "0",
            "materialized_status": "",
            "materialized_output": "",
            "materialized_rewrites": "",
            "materialized_adapters": "",
            "materialized_shape_anchors": "",
            "in_source_probe_category": in_source_probe.get("in_source_probe_category", ""),
            "in_source_probe_define": in_source_probe.get("in_source_probe_define", ""),
            "in_source_probe_source": in_source_probe.get("in_source_probe_source", ""),
            "in_source_probe_style": in_source_probe.get("in_source_probe_style", ""),
            "in_source_probe_lcs": in_source_probe.get("in_source_probe_lcs", ""),
            "in_source_probe_ratio": in_source_probe.get("in_source_probe_ratio", ""),
            "n64_extract": "",
        }
        status, reason = classify(row)
        row["conversion_status"] = status
        row["reason"] = reason
        row["blocker"] = blocker(row, status)
        row["next_action"] = next_action(row, status)
        row["implementation_note"] = implementation_note(str(row["implementation_style"]))
        rows.append(row)

    return sorted(rows, key=sort_key)


def sort_key(row: dict[str, Any]) -> tuple[int, int, int, str]:
    order = {
        "promote-now": 0,
        "batch-lowering-candidate": 1,
        "near-candidate": 2,
        "adapter-completion-needed": 3,
        "exact-seed-c-reconstruction": 4,
        "matched-exact-seed-asm": 5,
        "matched-inline-asm-seed": 6,
        "matched-c-inline-asm": 7,
        "matched-c": 8,
        "helper-lowering-needed": 9,
        "materialize-first": 10,
        "not-convertible-yet": 11,
    }
    return (
        order.get(str(row.get("conversion_status", "")), 99),
        STYLE_ORDER.get(str(row.get("implementation_style", "")), 99),
        -int_value(row, "target_instruction_count"),
        str(row.get("oot3d_entry", "")),
    )


def summarize(rows: list[dict[str, Any]]) -> dict[str, Any]:
    status_counts = Counter(str(row["conversion_status"]) for row in rows)
    style_counts = Counter(str(row["implementation_style"]) for row in rows)

    def file_count(statuses: set[str]) -> int:
        return len({str(row.get("port_file", "")) for row in rows if row["conversion_status"] in statuses})

    return {
        "rows": len(rows),
        "status_counts": dict(sorted(status_counts.items())),
        "implementation_style_counts": dict(sorted(style_counts.items())),
        "promote_now_functions": status_counts["promote-now"],
        "promote_now_files": file_count({"promote-now"}),
        "matched_c_functions": status_counts["matched-c"] + status_counts["matched-c-inline-asm"],
        "matched_c_files": file_count({"matched-c", "matched-c-inline-asm"}),
        "matched_exact_seed_asm_functions": status_counts["matched-exact-seed-asm"]
        + status_counts["matched-inline-asm-seed"],
        "matched_exact_seed_asm_files": file_count({"matched-exact-seed-asm", "matched-inline-asm-seed"}),
        "matched_inline_seed_asm_functions": status_counts["matched-inline-asm-seed"],
        "matched_inline_seed_asm_files": file_count({"matched-inline-asm-seed"}),
        "batch_lowering_functions": status_counts["batch-lowering-candidate"],
        "batch_lowering_files": file_count({"batch-lowering-candidate"}),
        "exact_seed_c_reconstruction_functions": status_counts["exact-seed-c-reconstruction"],
        "exact_seed_c_reconstruction_files": file_count({"exact-seed-c-reconstruction"}),
        "in_source_probe_functions": sum(1 for row in rows if row.get("in_source_probe_category")),
        "in_source_probe_files": len(
            {str(row.get("port_file", "")) for row in rows if row.get("in_source_probe_category")}
        ),
    }


def write_csv_report(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "conversion_status",
        "oot3d_entry",
        "oot3d_name",
        "n64_name",
        "port_file",
        "direct_class",
        "map_status",
        "adapter_readiness",
        "gate_category",
        "in_matched_sources",
        "matched_baseline_exact",
        "implementation_style",
        "target_instruction_count",
        "compiled_instruction_count",
        "lcs_instruction_count",
        "lcs_target_ratio",
        "materialized_status",
        "materialized_rewrites",
        "materialized_adapters",
        "materialized_shape_anchors",
        "materialized_output",
        "in_source_probe_category",
        "in_source_probe_define",
        "in_source_probe_source",
        "in_source_probe_style",
        "in_source_probe_lcs",
        "in_source_probe_ratio",
        "blocker",
        "next_action",
        "n64_extract",
    ]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})


def write_md(path: Path, rows: list[dict[str, Any]], summary: dict[str, Any]) -> None:
    status_text = ", ".join(f"{name}: {count}" for name, count in summary["status_counts"].items()) or "none"
    style_text = ", ".join(
        f"{name}: {count}" for name, count in summary["implementation_style_counts"].items()
    ) or "none"
    lines = [
        "# C Conversion Readiness",
        "",
        "This report is the actionable queue for replacing N64-derived exact seeds and helper-heavy ports with maintained, compilable C.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Functions audited | {summary['rows']} |",
        f"| Promote-now functions | {summary['promote_now_functions']} |",
        f"| Promote-now files | {summary['promote_now_files']} |",
        f"| Already matched C functions | {summary['matched_c_functions']} |",
        f"| Already matched C files | {summary['matched_c_files']} |",
        f"| Matched exact-seed asm functions | {summary['matched_exact_seed_asm_functions']} |",
        f"| Matched exact-seed asm files | {summary['matched_exact_seed_asm_files']} |",
        f"| Matched inline exact-seed asm functions | {summary['matched_inline_seed_asm_functions']} |",
        f"| Matched inline exact-seed asm files | {summary['matched_inline_seed_asm_files']} |",
        f"| Batch-lowering candidate functions | {summary['batch_lowering_functions']} |",
        f"| Batch-lowering candidate files | {summary['batch_lowering_files']} |",
        f"| Exact-seed C reconstruction functions | {summary['exact_seed_c_reconstruction_functions']} |",
        f"| Exact-seed C reconstruction files | {summary['exact_seed_c_reconstruction_files']} |",
        f"| In-source C probe functions | {summary['in_source_probe_functions']} |",
        f"| In-source C probe files | {summary['in_source_probe_files']} |",
        "",
        f"- Status counts: {status_text}",
        f"- Implementation styles: {style_text}",
        "",
        "## Conversion Queue",
        "",
        "| Status | OOT3D | N64 | File | Gate | Style | Matched | Target | LCS | C evidence | Blocker | Next action |",
        "| --- | --- | --- | --- | --- | --- | --- | ---: | ---: | --- | --- | --- |",
    ]
    for row in rows:
        c_evidence = "-"
        if row.get("materialized_status"):
            c_evidence = (
                f"{row['materialized_status']} / rewrites {row.get('materialized_rewrites', '')} / "
                f"anchors {row.get('materialized_shape_anchors', '')}"
            )
        elif row.get("in_source_probe_category"):
            c_evidence = (
                f"in-source {row['in_source_probe_category']} / "
                f"LCS {row.get('in_source_probe_lcs', '')} / ratio {row.get('in_source_probe_ratio', '')}"
            )
        lines.append(
            f"| `{row['conversion_status']}` | `{row['oot3d_entry']}` `{row['oot3d_name']}` | "
            f"`{row['n64_name']}` | `{row['port_file']}` | `{row['gate_category']}` | "
            f"`{row['implementation_style']}` | `{row['matched_baseline_exact']}` | "
            f"{row.get('target_instruction_count', '')} | {row.get('lcs_instruction_count', '')} | "
            f"{c_evidence} | {row['blocker']} | {row['next_action']} |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--structured-gate", type=Path, default=DEFAULT_GATE)
    parser.add_argument("--direct-rows", type=Path, default=DEFAULT_DIRECT_ROWS)
    parser.add_argument("--n64-map", type=Path, default=DEFAULT_N64_MAP)
    parser.add_argument("--matched-sources", type=Path, default=DEFAULT_MATCHED_SOURCES)
    parser.add_argument("--matched-compare", type=Path, default=DEFAULT_MATCHED_COMPARE)
    parser.add_argument("--materialized-dir", type=Path, default=DEFAULT_MATERIALIZED)
    parser.add_argument("--in-source-probe", type=Path, default=DEFAULT_IN_SOURCE_PROBE)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    args = parser.parse_args()

    rows = build_rows(args)
    summary = summarize(rows)
    data = {"summary": summary, "rows": rows}
    write_json(args.out_json, data)
    write_csv_report(args.out_csv, rows)
    write_md(args.out_md, rows, summary)

    print(
        "C conversion readiness: "
        f"{summary['promote_now_functions']} promote-now, "
        f"{summary['batch_lowering_functions']} batch-lowering, "
        f"{summary['matched_exact_seed_asm_functions']} matched asm seeds, "
        f"{summary['matched_c_functions']} matched C"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
