#!/usr/bin/env python3
"""Mine target-shaped direct conversion anchors.

This pass bridges the imported N64 source and OOT3D code generation.  It reads
target assembly to find base-register windows such as `r5 = r0 + 0x28a0`,
then checks whether the maintained/imported C still uses absolute offsets that
could be rewritten against that same base.  The output is a batch conversion
guide for emitting target-shaped C directly.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]

REG = r"(?:r(?:1[0-2]|[0-9])|sp|lr)"
IMM = r"-?(?:0x[0-9a-fA-F]+|\d+)"

INSN_RE = re.compile(r"^[0-9a-fA-F]+:\s+(?P<op>.+?)\s*$")
ADD_RE = re.compile(rf"^add\w*\s+(?P<dst>{REG}),(?P<src>{REG}),#(?P<imm>{IMM})\b")
SUB_RE = re.compile(rf"^sub\w*\s+(?P<dst>{REG}),(?P<src>{REG}),#(?P<imm>{IMM})\b")
MOV_RE = re.compile(rf"^(?:mov|cpy)\w*\s+(?P<dst>{REG}),(?P<src>{REG})\b")
LOAD_DST_RE = re.compile(rf"^(?:ldr\w*|ldrb\w*|ldrh\w*|ldrsh\w*)\s+(?P<dst>{REG}),")
MEM_RE = re.compile(rf"\[(?P<base>{REG})(?:,#(?P<off>{IMM}))?\]")
DEFINE_RE = re.compile(r"^\s*#define\s+(OOT3D_[A-Z0-9_]+)\s+(0x[0-9a-fA-F]+|\d+)\b")

COMMENT_RE = re.compile(r"/\*.*?\*/|//[^\r\n]*", re.DOTALL)
STRING_RE = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
FUNC_RE_TEMPLATE = r"\b(?P<name>{name})\s*\((?P<params>[^;{{}}]*)\)\s*\{{"
CAST_OFFSET_RE = re.compile(
    rf"\(\s*(?:const\s+)?u8\s*\*\s*\)\s*(?P<base>[A-Za-z_][A-Za-z0-9_]*)\s*\+\s*(?P<off>{IMM})"
)
GENERIC_OFFSET_RE = re.compile(rf"\b(?P<base>[A-Za-z_][A-Za-z0-9_]*)\s*\+\s*(?P<off>{IMM})\b")
PTR_ADD_RE = re.compile(
    r"\boot3d_[A-Za-z0-9_]*\s*\(\s*(?P<base>[A-Za-z_][A-Za-z0-9_]*)\s*,\s*(?P<macro>OOT3D_[A-Z0-9_]+)\s*\)"
)
DIRECT_FIELD_RE = re.compile(
    r"\bOOT3D_DIRECT_FIELD\s*\(\s*(?P<base>[A-Za-z_][A-Za-z0-9_]*)\s*,\s*(?P<macro>OOT3D_[A-Z0-9_]+)\s*\)"
)
ALIAS_RE = re.compile(
    r"\b(?:const\s+)?u8\s*\*\s*(?P<alias>[A-Za-z_][A-Za-z0-9_]*)\s*=\s*"
    rf"\(\s*(?:const\s+)?u8\s*\*\s*\)\s*(?P<base>[A-Za-z_][A-Za-z0-9_]*)(?:\s*\+\s*(?P<off>{IMM}))?\b"
)
ALIAS_ASSIGN_RE = re.compile(
    r"\b(?P<alias>[A-Za-z_][A-Za-z0-9_]*)\s*=\s*"
    rf"\(\s*(?:const\s+)?u8\s*\*\s*\)\s*(?P<base>[A-Za-z_][A-Za-z0-9_]*)(?:\s*\+\s*(?P<off>{IMM}))?\b"
)


@dataclass(frozen=True)
class RegExpr:
    root: str
    offset: int


@dataclass(frozen=True)
class TargetAccess:
    reg: str
    root: str
    base_offset: int
    local_offset: int
    total_offset: int
    op: str
    line: int


@dataclass(frozen=True)
class SourceAccess:
    base: str
    offset: int
    line: int
    expr: str
    kind: str


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


def write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})


def code_only(text: str) -> str:
    return STRING_RE.sub("", COMMENT_RE.sub("", text))


def int_value(row: dict[str, Any], key: str) -> int:
    try:
        return int(row.get(key, 0) or 0)
    except ValueError:
        return 0


def hex_value(value: int) -> str:
    sign = "-" if value < 0 else ""
    return f"{sign}0x{abs(value):x}"


def read_defines(root: Path) -> dict[str, int]:
    defines: dict[str, int] = {}
    for path in sorted((root / "include" / "oot3d").glob("*.h")):
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            match = DEFINE_RE.match(line)
            if match:
                defines[match.group(1)] = int(match.group(2), 0)
    return defines


def target_path_for(entry: str) -> Path | None:
    matches = sorted((ROOT / "analysis" / "target_functions").glob(f"{entry.lower()}_*.target.s"))
    return matches[0] if matches else None


def load_rows(gate_path: Path, blueprints_path: Path) -> list[dict[str, Any]]:
    rows_by_entry: dict[str, dict[str, Any]] = {}

    gate = read_json(gate_path, {})
    for row in gate.get("rows", []) if isinstance(gate, dict) else []:
        entry = str(row.get("oot3d_entry", "")).lower()
        if not entry:
            continue
        rows_by_entry[entry] = {
            "oot3d_entry": entry,
            "oot3d_name": row.get("oot3d_name", ""),
            "unit": row.get("unit", ""),
            "port_file": row.get("port_file", ""),
            "source": row.get("source", ""),
            "category": row.get("category", ""),
            "map_status": row.get("map_status", ""),
            "n64_source": row.get("n64_source", ""),
            "n64_name": row.get("n64_name", ""),
            "domain": "",
            "lcs_instruction_count": row.get("lcs_instruction_count", 0),
            "target_instruction_count": row.get("target_instruction_count", 0),
            "compiled_instruction_count": row.get("compiled_instruction_count", 0),
        }

    for row in read_csv(blueprints_path):
        entry = str(row.get("oot3d_entry", "")).lower()
        if not entry:
            continue
        current = rows_by_entry.setdefault(
            entry,
            {
                "oot3d_entry": entry,
                "oot3d_name": row.get("oot3d_name", ""),
                "unit": "",
                "port_file": row.get("port_file", ""),
                "source": row.get("port_file", ""),
                "category": "",
                "map_status": row.get("status", ""),
                "n64_source": "",
                "n64_name": "",
                "domain": row.get("domain", ""),
                "lcs_instruction_count": 0,
                "target_instruction_count": 0,
                "compiled_instruction_count": 0,
            },
        )
        for key in ("oot3d_name", "port_file", "domain", "n64_extract"):
            if row.get(key) and not current.get(key):
                current[key] = row[key]
        current["domain"] = current.get("domain") or row.get("domain", "")
        lane = row.get("n64_lane", "")
        if "::" in lane and not current.get("n64_source"):
            current["n64_source"], current["n64_name"] = lane.split("::", 1)
        current["conversion_mode"] = row.get("conversion_mode", "")
        current["automation_readiness"] = row.get("automation_readiness", "")
        current["replicable_structures"] = row.get("replicable_structures", "")

    result = []
    for row in rows_by_entry.values():
        if row.get("oot3d_name") and target_path_for(str(row["oot3d_entry"])):
            result.append(row)
    return sorted(result, key=lambda item: str(item["oot3d_entry"]))


def parse_target_accesses(path: Path) -> list[TargetAccess]:
    regs: dict[str, RegExpr] = {
        "r0": RegExpr("param_0", 0),
        "r1": RegExpr("param_1", 0),
        "r2": RegExpr("param_2", 0),
        "r3": RegExpr("param_3", 0),
    }
    accesses: list[TargetAccess] = []

    for line_no, raw_line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1):
        insn = INSN_RE.match(raw_line)
        if not insn:
            continue
        op = insn.group("op").strip().lower()
        mnemonic = op.split()[0] if op.split() else ""

        for mem in MEM_RE.finditer(op):
            base = mem.group("base")
            expr = regs.get(base)
            if expr is None or not expr.root.startswith("param_"):
                continue
            local = int(mem.group("off") or "0", 0)
            if mnemonic.startswith(("ldr", "str", "vldr", "vstr")):
                accesses.append(
                    TargetAccess(
                        reg=base,
                        root=expr.root,
                        base_offset=expr.offset,
                        local_offset=local,
                        total_offset=expr.offset + local,
                        op=mnemonic,
                        line=line_no,
                    )
                )

        add = ADD_RE.match(op)
        if add and add.group("src") in regs:
            regs[add.group("dst")] = RegExpr(regs[add.group("src")].root, regs[add.group("src")].offset + int(add.group("imm"), 0))
            continue

        sub = SUB_RE.match(op)
        if sub and sub.group("src") in regs:
            regs[sub.group("dst")] = RegExpr(regs[sub.group("src")].root, regs[sub.group("src")].offset - int(sub.group("imm"), 0))
            continue

        mov = MOV_RE.match(op)
        if mov:
            src = mov.group("src")
            dst = mov.group("dst")
            if src in regs:
                regs[dst] = regs[src]
            else:
                regs.pop(dst, None)
            continue

        if mnemonic == "bl" or mnemonic.startswith("bl"):
            for clobbered in ("r0", "r1", "r2", "r3", "r12"):
                regs.pop(clobbered, None)
            continue

        load_dst = LOAD_DST_RE.match(op)
        if load_dst:
            regs.pop(load_dst.group("dst"), None)

    return accesses


def format_offsets(values: list[int], limit: int = 10) -> str:
    counts = Counter(values)
    return " ".join(f"{hex_value(value)}:{count}" for value, count in counts.most_common(limit))


def candidate_bases_for(accesses: list[TargetAccess]) -> set[int]:
    bases: set[int] = set()
    for access in accesses:
        if access.base_offset > 0:
            bases.add(access.base_offset)
        if access.total_offset >= 0x100:
            for mask in (0xFF, 0x3FF, 0x7FF, 0xFFF):
                base = access.total_offset & ~mask
                if base > 0:
                    bases.add(base)
    return bases


def build_windows(accesses: list[TargetAccess]) -> list[dict[str, Any]]:
    by_root: dict[str, list[TargetAccess]] = defaultdict(list)
    for access in accesses:
        if access.total_offset >= 0:
            by_root[access.root].append(access)

    windows: list[dict[str, Any]] = []
    for root, root_accesses in by_root.items():
        for base in candidate_bases_for(root_accesses):
            members = [access for access in root_accesses if 0 <= access.total_offset - base <= 0xFFF]
            unique_totals = sorted({access.total_offset for access in members})
            if len(members) < 3 and len(unique_totals) < 2:
                continue
            observed = any(access.base_offset == base for access in members)
            if not observed and len(members) < 8:
                continue
            local_offsets = [access.total_offset - base for access in members]
            span = max(unique_totals) - min(unique_totals) if len(unique_totals) > 1 else 0
            score = len(members) * 20 + len(unique_totals) * 12 + (80 if observed else 0) - min(span // 0x100, 20)
            windows.append(
                {
                    "root": root,
                    "target_base": base,
                    "member_key": " ".join(hex_value(value) for value in unique_totals),
                    "target_access_count": len(members),
                    "target_unique_offsets": len(unique_totals),
                    "target_span": span,
                    "observed_register_base": observed,
                    "target_regs": " ".join(f"{key}:{value}" for key, value in Counter(access.reg for access in members).most_common()),
                    "target_ops": " ".join(f"{key}:{value}" for key, value in Counter(access.op for access in members).most_common()),
                    "target_total_offsets": format_offsets([access.total_offset for access in members]),
                    "target_local_offsets": format_offsets(local_offsets),
                    "score": score,
                }
            )

    observed_member_keys = {
        (str(row["root"]), str(row["member_key"]))
        for row in windows
        if row["observed_register_base"]
    }
    windows = [
        row
        for row in windows
        if row["observed_register_base"] or (str(row["root"]), str(row["member_key"])) not in observed_member_keys
    ]
    windows.sort(
        key=lambda row: (
            -int(row["score"]),
            str(row["root"]),
            int(row["target_base"]),
        )
    )
    return windows[:10]


def extract_function_text(source: str, name: str) -> tuple[str, list[str]]:
    if not name:
        return source, []
    pattern = re.compile(FUNC_RE_TEMPLATE.format(name=re.escape(name)), re.DOTALL)
    match = pattern.search(source)
    if not match:
        return source, []

    open_index = match.end() - 1
    depth = 0
    for index in range(open_index, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[match.start() : index + 1], parse_param_names(match.group("params"))
    return source[match.start() :], parse_param_names(match.group("params"))


def parse_param_names(params: str) -> list[str]:
    names: list[str] = []
    for raw_part in params.split(","):
        part = raw_part.strip()
        if not part or part == "void":
            continue
        part = re.sub(r"\[[^\]]*\]", "", part)
        tokens = re.findall(r"[A-Za-z_][A-Za-z0-9_]*", part)
        if tokens:
            names.append(tokens[-1])
    return names


def source_base_for(root: str, param_names: list[str], domain: str) -> str:
    if root.startswith("param_"):
        index = int(root.split("_", 1)[1])
        if index < len(param_names):
            return param_names[index]
    if domain == "message" and root == "param_0":
        return "play"
    if domain == "boss_va" and root == "param_0":
        return "this"
    if domain == "player" and root == "param_0":
        return "this"
    if domain == "player" and root == "param_1":
        return "play"
    return root


def canonical_aliases(text: str) -> dict[str, tuple[str, int]]:
    aliases: dict[str, tuple[str, int]] = {}
    for regex in (ALIAS_RE, ALIAS_ASSIGN_RE):
        for match in regex.finditer(code_only(text)):
            base = match.group("base")
            base_name, base_offset = aliases.get(base, (base, 0))
            aliases[match.group("alias")] = (base_name, base_offset + int(match.group("off") or "0", 0))
    return aliases


def alias_assignment_ranges(code: str) -> list[tuple[int, int]]:
    ranges: list[tuple[int, int]] = []
    for regex in (ALIAS_RE, ALIAS_ASSIGN_RE):
        for match in regex.finditer(code):
            ranges.append(match.span())
    return ranges


def is_inside_ranges(index: int, ranges: list[tuple[int, int]]) -> bool:
    return any(start <= index < end for start, end in ranges)


def line_number_at(text: str, index: int) -> int:
    return text.count("\n", 0, index) + 1


def parse_source_accesses(text: str, defines: dict[str, int], param_names: list[str]) -> list[SourceAccess]:
    code = code_only(text)
    aliases = canonical_aliases(text)
    alias_ranges = alias_assignment_ranges(code)
    allowed_bases = set(param_names) | set(aliases) | {"this", "play", "player", "msgCtx", "base", "object"}
    seen: set[tuple[str, int, int]] = set()
    accesses: list[SourceAccess] = []

    def add(base: str, offset: int, index: int, expr: str, kind: str) -> None:
        if is_inside_ranges(index, alias_ranges):
            return
        canonical, alias_offset = aliases.get(base, (base, 0))
        offset += alias_offset
        if canonical not in allowed_bases and base not in allowed_bases:
            return
        key = (canonical, offset, line_number_at(code, index))
        if key in seen:
            return
        seen.add(key)
        accesses.append(SourceAccess(canonical, offset, key[2], expr.strip(), kind))

    for match in CAST_OFFSET_RE.finditer(code):
        add(match.group("base"), int(match.group("off"), 0), match.start(), match.group(0), "cast-offset")

    for match in GENERIC_OFFSET_RE.finditer(code):
        add(match.group("base"), int(match.group("off"), 0), match.start(), match.group(0), "base-offset")

    for regex, kind in ((PTR_ADD_RE, "offset-macro"), (DIRECT_FIELD_RE, "direct-field")):
        for match in regex.finditer(code):
            macro = match.group("macro")
            if macro in defines:
                add(match.group("base"), defines[macro], match.start(), match.group(0), kind)

    return accesses


def anchor_name(domain: str, source_base: str, target_base: int) -> str:
    if domain == "message" and source_base == "play" and target_base == 0x28A0:
        return "msgPlayBase"
    if domain == "message" and source_base == "play" and target_base == 0x2AA0:
        return "msgPlaySubBase"
    if domain == "message" and source_base.lower().startswith("msg"):
        return "msgCtxBase"
    if domain == "boss_va" and source_base in {"this", "base"}:
        return f"bossVa{target_base:X}Base"
    if domain == "player" and source_base in {"this", "player"}:
        return f"player{target_base:X}Base"
    if source_base == "play":
        return f"play{target_base:X}Base"
    clean = re.sub(r"[^A-Za-z0-9_]", "", source_base) or "param"
    return f"{clean}{target_base:X}Base"


def classify_family(domain: str, source_base: str, target_base: int) -> str:
    if domain == "message" and source_base == "play":
        return "message-play-window"
    if domain == "message":
        return "message-context-window"
    if domain == "boss_va" and source_base in {"this", "base"} and target_base >= 0xC00:
        return "boss-va-instance-high-bank"
    if domain == "boss_va" and source_base == "play":
        return "boss-va-play-subsystem-bank"
    if domain == "player" and source_base in {"this", "player"} and target_base >= 0x1000:
        return "player-instance-high-bank"
    if domain == "player" and source_base == "play":
        return "player-playstate-bank"
    if domain == "pause":
        return "pause-state-packet-bank"
    return "target-derived-base-window"


def rewrite_examples(accesses: list[SourceAccess], target_base: int, anchor: str, limit: int = 8) -> str:
    examples = []
    for access in sorted(accesses, key=lambda item: (item.offset, item.line)):
        if 0 <= access.offset - target_base <= 0xFFF:
            examples.append(f"{hex_value(access.offset)}->{anchor}+{hex_value(access.offset - target_base)}@L{access.line}")
        if len(examples) >= limit:
            break
    return " ".join(examples)


def analyze(args: argparse.Namespace) -> dict[str, Any]:
    defines = read_defines(ROOT)
    rows = load_rows(args.structured_gate, args.blueprints)
    source_cache: dict[str, tuple[str, list[str], list[SourceAccess]]] = {}
    anchors: list[dict[str, Any]] = []

    for row in rows:
        target_path = target_path_for(str(row["oot3d_entry"]))
        if target_path is None:
            continue
        accesses = parse_target_accesses(target_path)
        windows = build_windows(accesses)

        port_file = str(row.get("port_file", ""))
        source_path = ROOT / port_file
        source_text = source_path.read_text(encoding="utf-8", errors="replace") if source_path.is_file() else ""
        source_func, param_names = extract_function_text(source_text, str(row.get("oot3d_name", "")))
        if not param_names:
            _, param_names = extract_function_text(source_text, "")
        source_accesses = parse_source_accesses(source_func, defines, param_names) if source_func else []

        for window in windows:
            domain = str(row.get("domain", ""))
            source_base = source_base_for(str(window["root"]), param_names, domain)
            accepted_bases = {source_base}
            if source_base == "this":
                accepted_bases.add("base")
            matching_source = [
                access
                for access in source_accesses
                if access.base in accepted_bases and 0 <= access.offset - int(window["target_base"]) <= 0xFFF
            ] if window["observed_register_base"] else []
            anchor = anchor_name(domain, source_base, int(window["target_base"]))
            source_offsets = [access.offset for access in matching_source]
            source_rewrite_count = len(matching_source)
            score = int(window["score"]) + source_rewrite_count * 35
            if source_rewrite_count:
                action = "rewrite-current-c"
            elif window["observed_register_base"]:
                action = "emit-anchor-in-materialized-source"
            else:
                action = "candidate-window-review"

            anchors.append(
                {
                    "score": score,
                    "oot3d_entry": row.get("oot3d_entry", ""),
                    "oot3d_name": row.get("oot3d_name", ""),
                    "unit": row.get("unit", ""),
                    "domain": domain,
                    "category": row.get("category", ""),
                    "map_status": row.get("map_status", ""),
                    "conversion_mode": row.get("conversion_mode", ""),
                    "automation_readiness": row.get("automation_readiness", ""),
                    "family": classify_family(domain, source_base, int(window["target_base"])),
                    "root": window["root"],
                    "source_base": source_base,
                    "anchor_name": anchor,
                    "target_base": hex_value(int(window["target_base"])),
                    "target_access_count": window["target_access_count"],
                    "target_unique_offsets": window["target_unique_offsets"],
                    "target_regs": window["target_regs"],
                    "target_ops": window["target_ops"],
                    "target_total_offsets": window["target_total_offsets"],
                    "target_local_offsets": window["target_local_offsets"],
                    "observed_register_base": "yes" if window["observed_register_base"] else "no",
                    "source_rewrite_count": source_rewrite_count,
                    "source_offsets": format_offsets(source_offsets),
                    "rewrite_examples": rewrite_examples(matching_source, int(window["target_base"]), anchor),
                    "candidate_declaration": f"u8* {anchor} = (u8*){source_base} + {hex_value(int(window['target_base']))};",
                    "action": action,
                    "port_file": port_file,
                    "target_asm": rel(target_path),
                    "n64_lane": f"{row.get('n64_source', '')}::{row.get('n64_name', '')}".strip(":"),
                    "lcs_instruction_count": row.get("lcs_instruction_count", 0),
                    "target_instruction_count": row.get("target_instruction_count", 0),
                    "compiled_instruction_count": row.get("compiled_instruction_count", 0),
                }
            )

    anchors.sort(key=lambda row: (-int(row["score"]), row["oot3d_entry"], row["target_base"]))

    families: list[dict[str, Any]] = []
    by_family: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for anchor in anchors:
        by_family[str(anchor["family"])].append(anchor)
    for family, group in by_family.items():
        domains = Counter(str(row["domain"]) for row in group)
        actions = Counter(str(row["action"]) for row in group)
        families.append(
            {
                "family": family,
                "anchors": len(group),
                "functions": len({row["oot3d_entry"] for row in group}),
                "source_rewrite_sites": sum(int(row["source_rewrite_count"]) for row in group),
                "domains": " ".join(f"{key}:{value}" for key, value in domains.most_common()),
                "actions": " ".join(f"{key}:{value}" for key, value in actions.most_common()),
                "examples": " ".join(f"{row['oot3d_entry']}:{row['anchor_name']}@{row['target_base']}" for row in group[:5]),
            }
        )
    families.sort(key=lambda row: (-int(row["source_rewrite_sites"]), -int(row["anchors"]), row["family"]))

    summary = {
        "target_functions_scanned": len(rows),
        "shape_anchor_windows": len(anchors),
        "families": len(families),
        "anchors_with_source_rewrites": sum(1 for row in anchors if int(row["source_rewrite_count"]) > 0),
        "source_rewrite_sites": sum(int(row["source_rewrite_count"]) for row in anchors),
        "rewrite_current_c_actions": sum(1 for row in anchors if row["action"] == "rewrite-current-c"),
    }
    return {"summary": summary, "families": families, "anchors": anchors}


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Shape Anchor Mining",
        "",
        "This report mines target assembly for reusable base-register windows, then compares those windows with current OOT3D-shaped C. These anchors are the mechanical layer between imported N64 source and compilable matched C.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Target functions scanned | {summary['target_functions_scanned']} |",
        f"| Shape anchor windows | {summary['shape_anchor_windows']} |",
        f"| Anchor families | {summary['families']} |",
        f"| Anchors with current C rewrite sites | {summary['anchors_with_source_rewrites']} |",
        f"| Current C rewrite sites | {summary['source_rewrite_sites']} |",
        f"| Rewrite-current-C actions | {summary['rewrite_current_c_actions']} |",
        "",
        "## Verdict",
        "",
        "Yes: the imported and structured files share target-shaped offset windows. The direct converter should not only replace names; it should introduce the same base pointers the target assembly uses, then rewrite absolute struct/PlayState offsets relative to those bases.",
        "",
        "## Anchor Families",
        "",
        "| Family | Anchors | Functions | Rewrite sites | Domains | Actions | Examples |",
        "| --- | ---: | ---: | ---: | --- | --- | --- |",
    ]
    for row in data["families"]:
        lines.append(
            f"| `{row['family']}` | {row['anchors']} | {row['functions']} | {row['source_rewrite_sites']} | "
            f"`{row['domains']}` | `{row['actions']}` | `{row['examples']}` |"
        )

    lines.extend(
        [
            "",
            "## Highest Leverage Anchors",
            "",
            "| Rank | OOT3D | Family | Anchor | Target Uses | Source Rewrites | Declaration | Rewrite Examples | Action |",
            "| ---: | --- | --- | --- | ---: | ---: | --- | --- | --- |",
        ]
    )
    for index, row in enumerate(data["anchors"][:24], start=1):
        lines.append(
            f"| {index} | `{row['oot3d_entry']}` `{row['oot3d_name']}` | `{row['family']}` | "
            f"`{row['source_base']} + {row['target_base']}` | {row['target_access_count']} | "
            f"{row['source_rewrite_count']} | `{row['candidate_declaration']}` | "
            f"`{row['rewrite_examples']}` | `{row['action']}` |"
        )

    lines.extend(
        [
            "",
            "## Direct Conversion Rule",
            "",
            "1. For each lane target, mine the target `.target.s` and keep observed non-zero base-register windows.",
            "2. In the converted N64 C, introduce the mined base declaration before the first related access.",
            "3. Rewrite absolute offsets inside the window to `anchor + local_offset`; keep the original control flow from N64.",
            "4. Re-run the structured C gate. If prefix/LCS regresses, keep the anchor as materialized packet guidance rather than build source.",
            "5. Once one target in a lane matches, reuse its anchor family across the other targets in that N64 source lane.",
            "",
            "The immediate concrete case is `003438a4 oot3d_message_textbox_common`: the target builds a `play + 0x28a0` base and many current C accesses fall inside that same window.",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--structured-gate", type=Path, default=ROOT / "analysis" / "structured_c_match_gate.json")
    parser.add_argument(
        "--blueprints",
        type=Path,
        default=ROOT / "analysis" / "direct_conversion_function_blueprints.csv",
    )
    parser.add_argument("--out-json", type=Path, default=ROOT / "analysis" / "direct_shape_anchor_windows.json")
    parser.add_argument("--out-md", type=Path, default=ROOT / "analysis" / "direct_shape_anchor_windows.md")
    parser.add_argument("--out-anchors-csv", type=Path, default=ROOT / "analysis" / "direct_shape_anchor_windows.csv")
    parser.add_argument("--out-families-csv", type=Path, default=ROOT / "analysis" / "direct_shape_anchor_families.csv")
    args = parser.parse_args()

    data = analyze(args)
    write_json(args.out_json, data)
    write_markdown(args.out_md, data)
    write_csv(
        args.out_anchors_csv,
        data["anchors"],
        [
            "score",
            "oot3d_entry",
            "oot3d_name",
            "unit",
            "domain",
            "category",
            "map_status",
            "conversion_mode",
            "automation_readiness",
            "family",
            "root",
            "source_base",
            "anchor_name",
            "target_base",
            "target_access_count",
            "target_unique_offsets",
            "target_regs",
            "target_ops",
            "target_total_offsets",
            "target_local_offsets",
            "observed_register_base",
            "source_rewrite_count",
            "source_offsets",
            "rewrite_examples",
            "candidate_declaration",
            "action",
            "port_file",
            "target_asm",
            "n64_lane",
            "lcs_instruction_count",
            "target_instruction_count",
            "compiled_instruction_count",
        ],
    )
    write_csv(
        args.out_families_csv,
        data["families"],
        ["family", "anchors", "functions", "source_rewrite_sites", "domains", "actions", "examples"],
    )

    summary = data["summary"]
    print(
        "direct shape anchors: "
        f"{summary['shape_anchor_windows']} windows, "
        f"{summary['families']} families, "
        f"{summary['source_rewrite_sites']} current-C rewrite sites"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
