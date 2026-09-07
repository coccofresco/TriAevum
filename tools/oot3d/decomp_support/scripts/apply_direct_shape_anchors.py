#!/usr/bin/env python3
"""Apply target-derived shape anchors to structured C ports."""

from __future__ import annotations

import argparse
import ast
import csv
import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import mine_direct_shape_anchors as shape


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ANCHORS = ROOT / "analysis" / "direct_shape_anchor_windows.csv"
DEFAULT_CONVERTIBLE = ROOT / "analysis" / "convertible_ports.csv"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_shape_anchor_apply_plan.json"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_shape_anchor_apply_plan.md"

HELPER_RE = re.compile(
    r"\b(?P<helper>oot3d_(?P<domain>boss_va|player)_(?P<kind>u8|s8|u16|s16|u32|s32|f32|float|ptr|vec3f|ptr_add))"
    r"\s*\(\s*(?P<base>[A-Za-z_][A-Za-z0-9_]*)\s*,\s*(?P<offset>[^()]+?)\s*\)"
)
CAST_OFFSET_RE = re.compile(
    r"\(\s*(?P<type>u8|s8|u16|s16|u32|s32|float)\s*\*\s*\)\s*"
    r"(?P<base>[A-Za-z_][A-Za-z0-9_]*)\s*\+\s*(?P<offset>0x[0-9a-fA-F]+|\d+)"
)

TYPE_BY_KIND = {
    "u8": "u8*",
    "s8": "s8*",
    "u16": "u16*",
    "s16": "s16*",
    "u32": "u32*",
    "s32": "s32*",
    "f32": "float*",
    "float": "float*",
    "ptr": "void**",
    "vec3f": "Oot3dVec3f*",
}


@dataclass(frozen=True)
class Anchor:
    entry: str
    function: str
    port_file: Path
    source_base: str
    name: str
    target_base: int
    observed_offsets: set[int]
    action: str


@dataclass
class Replacement:
    start: int
    end: int
    old: str
    new: str
    anchor: Anchor
    offset: int


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


def convertible_entries(path: Path, statuses: set[str]) -> set[str]:
    entries: set[str] = set()
    for row in read_csv(path):
        if statuses and row.get("status", "") not in statuses:
            continue
        entry = row.get("oot3d_entry", "").lower()
        if entry:
            entries.add(entry)
    return entries


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def target_observed_offsets(entry: str) -> dict[tuple[str, int], set[int]]:
    target_path = shape.target_path_for(entry)
    if target_path is None:
        return {}
    observed: dict[tuple[str, int], set[int]] = {}
    for access in shape.parse_target_accesses(target_path):
        if access.base_offset <= 0:
            continue
        observed.setdefault((access.root, access.base_offset), set()).add(access.total_offset)
    return observed


def load_anchors(path: Path, entries: set[str], actions: set[str]) -> dict[str, list[Anchor]]:
    rows = read_csv(path)
    by_entry: dict[str, list[Anchor]] = {}
    observed_cache: dict[str, dict[tuple[str, int], set[int]]] = {}
    for row in rows:
        entry = str(row.get("oot3d_entry", "")).lower()
        if entries and entry not in entries:
            continue
        if actions and row.get("action", "") not in actions:
            continue
        try:
            target_base = int(row.get("target_base", "0"), 0)
        except ValueError:
            continue
        if entry not in observed_cache:
            observed_cache[entry] = target_observed_offsets(entry)
        root = str(row.get("root", "") or row.get("source_root", "") or row.get("target_root", ""))
        observed_offsets = observed_cache[entry].get((root, target_base), set())
        if not observed_offsets:
            continue
        port_file = ROOT / row.get("port_file", "")
        by_entry.setdefault(entry, []).append(
            Anchor(
                entry=entry,
                function=row.get("oot3d_name", ""),
                port_file=port_file,
                source_base=row.get("source_base", ""),
                name=row.get("anchor_name", ""),
                target_base=target_base,
                observed_offsets=observed_offsets,
                action=row.get("action", ""),
            )
        )
    for anchors in by_entry.values():
        anchors.sort(key=lambda item: (item.source_base, item.target_base))
    return by_entry


def find_function_span(source: str, name: str) -> tuple[int, int] | None:
    pattern = re.compile(shape.FUNC_RE_TEMPLATE.format(name=re.escape(name)), re.DOTALL)
    match = pattern.search(source)
    if not match:
        return None
    depth = 1
    cursor = match.end()
    while cursor < len(source):
        char = source[cursor]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return match.start(), cursor + 1
        cursor += 1
    return None


def eval_offset(expr: str, defines: dict[str, int]) -> int | None:
    prepared = expr.strip()
    for name, value in sorted(defines.items(), key=lambda item: -len(item[0])):
        prepared = re.sub(rf"\b{re.escape(name)}\b", str(value), prepared)
    if not re.fullmatch(r"[0-9xXa-fA-F\s()+\-]+", prepared):
        return None
    try:
        node = ast.parse(prepared, mode="eval")
        return int(eval_ast(node.body))
    except Exception:
        return None


def eval_ast(node: ast.AST) -> int:
    if isinstance(node, ast.Constant) and isinstance(node.value, int):
        return int(node.value)
    if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.UAdd, ast.USub)):
        value = eval_ast(node.operand)
        return value if isinstance(node.op, ast.UAdd) else -value
    if isinstance(node, ast.BinOp) and isinstance(node.op, (ast.Add, ast.Sub)):
        left = eval_ast(node.left)
        right = eval_ast(node.right)
        return left + right if isinstance(node.op, ast.Add) else left - right
    raise ValueError("unsupported offset expression")


def choose_anchor(anchors: list[Anchor], base: str, offset: int) -> Anchor | None:
    candidates = [
        anchor
        for anchor in anchors
        if anchor.source_base == base and offset in anchor.observed_offsets and 0 <= offset - anchor.target_base <= 0xFFF
    ]
    if not candidates:
        return None
    return sorted(candidates, key=lambda item: (offset - item.target_base, -item.target_base))[0]


def collect_replacements(function_text: str, anchors: list[Anchor], defines: dict[str, int]) -> list[Replacement]:
    replacements: list[Replacement] = []

    for match in HELPER_RE.finditer(function_text):
        base = match.group("base")
        offset = eval_offset(match.group("offset"), defines)
        if offset is None:
            continue
        anchor = choose_anchor(anchors, base, offset)
        if anchor is None:
            continue
        kind = match.group("kind")
        local = offset - anchor.target_base
        if kind == "ptr_add":
            new = f"({anchor.name} + {shape.hex_value(local)})"
        else:
            new = f"({TYPE_BY_KIND[kind]})({anchor.name} + {shape.hex_value(local)})"
        replacements.append(Replacement(match.start(), match.end(), match.group(0), new, anchor, offset))

    for match in CAST_OFFSET_RE.finditer(function_text):
        base = match.group("base")
        offset = int(match.group("offset"), 0)
        anchor = choose_anchor(anchors, base, offset)
        if anchor is None:
            continue
        local = offset - anchor.target_base
        new = f"({match.group('type')}*)({anchor.name} + {shape.hex_value(local)})"
        replacements.append(Replacement(match.start(), match.end(), match.group(0), new, anchor, offset))

    replacements.sort(key=lambda item: item.start)
    filtered: list[Replacement] = []
    last_end = -1
    for replacement in replacements:
        if replacement.start < last_end:
            continue
        filtered.append(replacement)
        last_end = replacement.end
    return filtered


def insert_anchor_declarations(function_text: str, anchors: list[Anchor]) -> str:
    used = []
    for anchor in anchors:
        if anchor.name in used:
            continue
        if re.search(rf"\b{re.escape(anchor.name)}\b\s*=", function_text):
            continue
        used.append(anchor.name)
    if not used:
        return function_text

    brace = function_text.find("{")
    if brace < 0:
        return function_text
    indent_match = re.search(r"\n([ \t]*)\S", function_text[brace + 1 :])
    indent = indent_match.group(1) if indent_match else "    "
    declarations = []
    anchor_by_name = {anchor.name: anchor for anchor in anchors}
    for name in used:
        anchor = anchor_by_name[name]
        declarations.append(f"{indent}u8* {anchor.name} = (u8*){anchor.source_base} + {shape.hex_value(anchor.target_base)};")
    return function_text[: brace + 1] + "\n" + "\n".join(declarations) + function_text[brace + 1 :]


def apply_replacements(function_text: str, replacements: list[Replacement]) -> str:
    out = function_text
    for replacement in sorted(replacements, key=lambda item: item.start, reverse=True):
        out = out[: replacement.start] + replacement.new + out[replacement.end :]
    return out


def count_existing_anchor_sites(function_text: str, anchors: list[Anchor]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for anchor in anchors:
        count = 0
        for offset in anchor.observed_offsets:
            if 0 <= offset - anchor.target_base <= 0xFFF:
                local = shape.hex_value(offset - anchor.target_base)
                count += len(re.findall(rf"\b{re.escape(anchor.name)}\s*\+\s*{re.escape(local)}\b", function_text))
        if count:
            counts[anchor.name] = count
    return counts


def process_entry(entry: str, anchors: list[Anchor], apply: bool) -> dict[str, Any]:
    if not anchors:
        return {"entry": entry, "status": "no-anchors", "replacements": 0}
    port_file = anchors[0].port_file
    function = anchors[0].function
    source = port_file.read_text(encoding="utf-8", errors="replace")
    span = find_function_span(source, function)
    if span is None:
        return {"entry": entry, "status": "missing-function", "port_file": rel(port_file), "function": function, "replacements": 0}

    defines = shape.read_defines(ROOT)
    function_text = source[span[0] : span[1]]
    replacements = collect_replacements(function_text, anchors, defines)
    existing_anchor_sites = count_existing_anchor_sites(function_text, anchors)
    used_anchor_names = sorted({replacement.anchor.name for replacement in replacements} | set(existing_anchor_sites))
    used_anchors = [anchor for anchor in anchors if anchor.name in used_anchor_names]
    rewritten_function = apply_replacements(function_text, replacements)
    rewritten_function = insert_anchor_declarations(rewritten_function, used_anchors)

    changed = rewritten_function != function_text
    if apply and changed:
        port_file.write_text(source[: span[0]] + rewritten_function + source[span[1] :], encoding="utf-8")

    return {
        "entry": entry,
        "status": "changed" if changed else ("already-shaped" if existing_anchor_sites else "no-change"),
        "port_file": rel(port_file),
        "function": function,
        "anchors": used_anchor_names,
        "anchor_count": len(used_anchor_names),
        "replacements": len(replacements),
        "already_shaped_sites": sum(existing_anchor_sites.values()),
        "apply": apply,
        "examples": [
            {
                "offset": shape.hex_value(replacement.offset),
                "old": replacement.old,
                "new": replacement.new,
                "anchor": replacement.anchor.name,
            }
            for replacement in replacements[:12]
        ],
    }


def write_md(path: Path, rows: list[dict[str, Any]]) -> None:
    lines = [
        "# Direct Shape Anchor Apply Plan",
        "",
        "| Entry | Function | Status | Anchors | Replacements | Already shaped | Source |",
        "| --- | --- | --- | ---: | ---: | ---: | --- |",
    ]
    for row in rows:
        lines.append(
            f"| `{row.get('entry', '')}` | `{row.get('function', '')}` | `{row.get('status', '')}` | "
            f"{row.get('anchor_count', 0)} | {row.get('replacements', 0)} | "
            f"{row.get('already_shaped_sites', 0)} | `{row.get('port_file', '')}` |"
        )
    lines.extend(["", "## Examples"])
    for row in rows:
        examples = row.get("examples", [])
        if not examples:
            continue
        lines.extend(["", f"### `{row['entry']}` `{row['function']}`", ""])
        for example in examples:
            lines.append(f"- `{example['offset']}` via `{example['anchor']}`: `{example['old']}` -> `{example['new']}`")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--anchors", type=Path, default=DEFAULT_ANCHORS)
    parser.add_argument("--convertible-audit", type=Path, default=DEFAULT_CONVERTIBLE)
    parser.add_argument("--entry", action="append", default=[], help="OOT3D entry to process; repeatable")
    parser.add_argument("--action", action="append", default=["rewrite-current-c"], help="Anchor action to apply")
    parser.add_argument(
        "--status",
        action="append",
        default=["shape-convertible"],
        help="Convertible audit status to process when --entry is omitted; repeatable",
    )
    parser.add_argument("--all", action="store_true", help="Process all matching anchor rows instead of the convertible audit queue")
    parser.add_argument("--apply", action="store_true", help="Modify source files; default writes only a plan")
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    args = parser.parse_args()

    entries = {entry.lower() for entry in args.entry}
    if not entries and not args.all:
        entries = convertible_entries(args.convertible_audit, set(args.status))
    actions = set(args.action)
    anchors_by_entry = load_anchors(args.anchors, entries, actions)
    rows = [process_entry(entry, anchors, args.apply) for entry, anchors in sorted(anchors_by_entry.items())]
    write_json(args.out_json, {"apply": args.apply, "rows": rows})
    write_md(args.out_md, rows)

    print(
        "shape anchor apply "
        f"{'applied' if args.apply else 'planned'}: "
        f"{sum(row.get('replacements', 0) for row in rows)} replacements across {len(rows)} functions"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
