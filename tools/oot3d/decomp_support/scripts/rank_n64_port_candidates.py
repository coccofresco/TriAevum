#!/usr/bin/env python3
"""Rank N64 OoT source functions as porting candidates for OOT3D functions.

This is a triage accelerator: it indexes zeldaret/oot source functions and
Ghidra-decompiled OOT3D functions, scores shared constants/tokens/call hints,
and emits a review queue. It does not promote symbols by itself.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path

from symbol_overlay import (
    identifier_overlay,
    load_manual_symbols,
    manual_entries,
    names_by_entry as manual_names_by_entry,
    overlay_identifier_text,
)


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
GHIDRA_DECOMPILED = ROOT / "ghidra_export" / "decompiled"
FUNCTIONS_CSV = ROOT / "ghidra_export" / "functions.csv"
MANUAL_SYMBOLS = ROOT / "symbols" / "manual_symbols.csv"
DEFAULT_N64_ROOT = ROOT.parent / "external" / "oot"

COMMENT_RE = re.compile(r"/\*.*?\*/|//.*?$", re.DOTALL | re.MULTILINE)
STRING_RE = re.compile(r'"(?:\\.|[^"\\])*"')
IDENT_RE = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")
CALL_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")
HEX_RE = re.compile(r"\b0x[0-9A-Fa-f]+\b")
DEC_RE = re.compile(r"(?<![A-Za-z0-9_])\d{2,}(?![A-Za-z0-9_])")
FUNC_HEAD_RE = re.compile(
    r"(?P<head>^[A-Za-z_][A-Za-z0-9_\s\*\(\),]*?\s+"
    r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\([^;{}]*\)\s*)\{",
    re.MULTILINE,
)

STOP_WORDS = {
    "auto",
    "break",
    "case",
    "char",
    "const",
    "continue",
    "default",
    "do",
    "double",
    "else",
    "enum",
    "extern",
    "float",
    "for",
    "goto",
    "if",
    "inline",
    "int",
    "long",
    "register",
    "return",
    "short",
    "signed",
    "sizeof",
    "static",
    "struct",
    "switch",
    "typedef",
    "union",
    "unsigned",
    "void",
    "volatile",
    "while",
    "undefined",
    "undefined4",
    "undefined8",
    "byte",
    "word",
    "dword",
    "true",
    "false",
    "null",
    "local",
    "param",
    "var",
    "extraout",
    "unaff",
    "in",
    "out",
}


@dataclass(frozen=True)
class FunctionRecord:
    platform: str
    name: str
    path: str
    entry: str | None
    line_count: int
    tokens: frozenset[str]
    calls: frozenset[str]
    constants: frozenset[str]
    strings: frozenset[str]


def rel(path: Path, root: Path = ROOT) -> str:
    try:
        return str(path.relative_to(root)).replace("\\", "/")
    except ValueError:
        try:
            return str(path.relative_to(ROOT.parent)).replace("\\", "/")
        except ValueError:
            return str(path).replace("\\", "/")


def strip_comments(text: str) -> str:
    return COMMENT_RE.sub(" ", text)


def split_identifier(identifier: str) -> list[str]:
    parts = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", identifier).lower().split("_")
    out = []
    for part in parts:
        part = part.strip()
        if len(part) < 3 or part in STOP_WORDS:
            continue
        if part.startswith(("fun", "dat", "lab", "unk", "var", "temp")):
            continue
        out.append(part)
    return out


def normalize_constant(value: str, include_small: bool) -> str | None:
    try:
        number = int(value, 16) if value.lower().startswith("0x") else int(value, 10)
    except ValueError:
        return None
    if number in {0, 1, 2, 3, 4, 8, 16, 32, 64, 255, 256, 512, 1024}:
        return None
    if not include_small and number < 0x100:
        return None
    return f"0x{number:x}"


def extract_features(
    text: str,
    name: str,
    include_small_constants: bool,
) -> tuple[frozenset[str], frozenset[str], frozenset[str], frozenset[str]]:
    clean = strip_comments(text)
    strings = frozenset(s.strip('"').lower() for s in STRING_RE.findall(clean) if len(s) > 4)
    calls = frozenset(match.group(1) for match in CALL_RE.finditer(clean))

    constants: set[str] = set()
    for value in [*HEX_RE.findall(clean), *DEC_RE.findall(clean)]:
        normalized = normalize_constant(value, include_small_constants)
        if normalized:
            constants.add(normalized)

    token_counts: Counter[str] = Counter()
    for identifier in [name, *IDENT_RE.findall(clean)]:
        for token in split_identifier(identifier):
            token_counts[token] += 1

    tokens = frozenset(token for token, count in token_counts.items() if count >= 2 or token in split_identifier(name))
    return tokens, frozenset(calls), frozenset(constants), strings


def find_matching_brace(text: str, open_index: int) -> int | None:
    depth = 0
    in_string = False
    in_char = False
    escape = False
    for index in range(open_index, len(text)):
        char = text[index]
        if escape:
            escape = False
            continue
        if char == "\\" and (in_string or in_char):
            escape = True
            continue
        if char == '"' and not in_char:
            in_string = not in_string
            continue
        if char == "'" and not in_string:
            in_char = not in_char
            continue
        if in_string or in_char:
            continue
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return index + 1
    return None


def iter_c_functions(path: Path) -> list[tuple[str, str]]:
    text = path.read_text(encoding="utf-8", errors="replace")
    rows: list[tuple[str, str]] = []
    for match in FUNC_HEAD_RE.finditer(text):
        name = match.group("name")
        if name in {"if", "for", "while", "switch"}:
            continue
        close = find_matching_brace(text, match.end() - 1)
        if close is None:
            continue
        body = text[match.start() : close]
        rows.append((name, body))
    return rows


def load_n64_functions(
    n64_root: Path,
    source_globs: list[str],
    include_small_constants: bool,
) -> list[FunctionRecord]:
    files: list[Path] = []
    for pattern in source_globs:
        files.extend(n64_root.glob(pattern))
    records: list[FunctionRecord] = []
    for path in sorted(set(files)):
        if not path.is_file() or path.suffix != ".c":
            continue
        for name, body in iter_c_functions(path):
            tokens, calls, constants, strings = extract_features(body, name, include_small_constants)
            records.append(
                FunctionRecord(
                    platform="n64",
                    name=name,
                    path=rel(path, n64_root),
                    entry=None,
                    line_count=body.count("\n") + 1,
                    tokens=tokens,
                    calls=calls,
                    constants=constants,
                    strings=strings,
                )
            )
    return records


def load_function_entries(use_symbol_overlay: bool) -> dict[str, str]:
    with FUNCTIONS_CSV.open(newline="", encoding="utf-8") as handle:
        names = {row["entry"].lower(): row["name"] for row in csv.DictReader(handle)}
    if use_symbol_overlay:
        names.update(manual_names_by_entry(load_manual_symbols(MANUAL_SYMBOLS)))
    return names


def load_manual_entries() -> set[str]:
    return manual_entries(load_manual_symbols(MANUAL_SYMBOLS))


def load_oot3d_functions(
    include_named: bool,
    include_small_constants: bool,
    use_symbol_overlay: bool,
) -> list[FunctionRecord]:
    symbols = load_manual_symbols(MANUAL_SYMBOLS)
    names_by_entry = load_function_entries(use_symbol_overlay)
    named_entries = manual_entries(symbols)
    name_overlay = identifier_overlay(symbols) if use_symbol_overlay else {}
    records: list[FunctionRecord] = []
    for path in sorted(GHIDRA_DECOMPILED.glob("*.c")):
        entry_match = re.search(r"_([0-9a-fA-F]{8})_", path.name)
        if not entry_match:
            continue
        entry = entry_match.group(1).lower()
        if not include_named and entry in named_entries:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        text = overlay_identifier_text(text, name_overlay)
        name = names_by_entry.get(entry, path.stem)
        tokens, calls, constants, strings = extract_features(text, name, include_small_constants)
        records.append(
            FunctionRecord(
                platform="oot3d",
                name=name,
                path=rel(path),
                entry=entry,
                line_count=text.count("\n") + 1,
                tokens=tokens,
                calls=calls,
                constants=constants,
                strings=strings,
            )
        )
    return records


def build_inverted_index(n64_records: list[FunctionRecord]) -> dict[str, list[tuple[int, float]]]:
    index: dict[str, list[int]] = defaultdict(list)
    for idx, record in enumerate(n64_records):
        for const in record.constants:
            index[f"const:{const}"].append(idx)
        for string in record.strings:
            index[f"str:{string}"].append(idx)
        for token in record.tokens:
            index[f"tok:{token}"].append(idx)
        for call in record.calls:
            for token in split_identifier(call):
                index[f"calltok:{token}"].append(idx)

    weighted: dict[str, list[tuple[int, float]]] = {}
    total = max(1, len(n64_records))
    for feature, ids in index.items():
        unique_ids = sorted(set(ids))
        if len(unique_ids) > total * 0.35:
            continue
        prefix = feature.split(":", 1)[0]
        base = {"const": 6.0, "str": 20.0, "tok": 4.0, "calltok": 6.0}[prefix]
        idf = math.log((total + 1) / (len(unique_ids) + 1)) + 1.0
        weighted[feature] = [(idx, base * idf) for idx in unique_ids]
    return weighted


def candidate_features(record: FunctionRecord) -> list[str]:
    features = []
    features.extend(f"const:{value}" for value in record.constants)
    features.extend(f"str:{value}" for value in record.strings)
    features.extend(f"tok:{token}" for token in record.tokens)
    for call in record.calls:
        features.extend(f"calltok:{token}" for token in split_identifier(call))
    return features


def shared_evidence(left: FunctionRecord, right: FunctionRecord) -> dict[str, list[str]]:
    left_call_tokens = {token for call in left.calls for token in split_identifier(call)}
    right_call_tokens = {token for call in right.calls for token in split_identifier(call)}
    return {
        "constants": sorted(left.constants & right.constants)[:12],
        "strings": sorted(left.strings & right.strings)[:8],
        "tokens": sorted(left.tokens & right.tokens)[:16],
        "call_tokens": sorted(left_call_tokens & right_call_tokens)[:16],
    }


def score_size(oot3d: FunctionRecord, n64: FunctionRecord) -> float:
    if oot3d.line_count <= 0 or n64.line_count <= 0:
        return 0.0
    ratio = min(oot3d.line_count, n64.line_count) / max(oot3d.line_count, n64.line_count)
    return 10.0 * ratio


def rank_candidates(
    oot3d_records: list[FunctionRecord],
    n64_records: list[FunctionRecord],
    index: dict[str, list[tuple[int, float]]],
    top_n64_per_oot3d: int,
    max_oot3d: int,
    min_score: float,
    domain: str | None,
) -> list[dict[str, object]]:
    domain_re = re.compile(domain, re.IGNORECASE) if domain else None
    rows: list[dict[str, object]] = []
    for oot3d in oot3d_records:
        if domain_re:
            haystack = " ".join([oot3d.name, oot3d.path, *oot3d.tokens, *oot3d.constants])
            if not domain_re.search(haystack):
                continue
        scores_by_kind: dict[int, Counter[str]] = defaultdict(Counter)
        for feature in candidate_features(oot3d):
            kind = feature.split(":", 1)[0]
            for idx, weight in index.get(feature, []):
                scores_by_kind[idx][kind] += weight
        capped_scores: Counter[int] = Counter()
        caps = {"const": 90.0, "str": 140.0, "tok": 90.0, "calltok": 120.0}
        for idx, kind_scores in scores_by_kind.items():
            capped_scores[idx] = sum(min(value, caps[kind]) for kind, value in kind_scores.items())
        ranked = []
        for idx, base_score in capped_scores.most_common(top_n64_per_oot3d * 12):
            n64 = n64_records[idx]
            if domain_re:
                n64_haystack = " ".join([n64.name, n64.path, *n64.tokens, *n64.constants])
                if not domain_re.search(n64_haystack):
                    continue
            total_score = float(base_score) + score_size(oot3d, n64)
            if total_score < min_score:
                continue
            evidence = shared_evidence(oot3d, n64)
            ranked.append(
                {
                    "score": round(total_score, 2),
                    "n64_name": n64.name,
                    "n64_path": n64.path,
                    "n64_lines": n64.line_count,
                    "evidence": evidence,
                }
            )
        ranked = sorted(ranked, key=lambda row: (-float(row["score"]), str(row["n64_path"]), str(row["n64_name"])))
        if not ranked:
            continue
        rows.append(
            {
                "oot3d_entry": oot3d.entry,
                "oot3d_name": oot3d.name,
                "oot3d_path": oot3d.path,
                "oot3d_lines": oot3d.line_count,
                "candidates": ranked[:top_n64_per_oot3d],
            }
        )
    rows.sort(key=lambda row: -float(row["candidates"][0]["score"]))
    return rows[:max_oot3d]


def make_markdown(report: dict[str, object]) -> str:
    lines = [
        "# N64 Port Candidate Rank",
        "",
        "Generated from `scripts/rank_n64_port_candidates.py`.",
        "",
        "## Inputs",
        "",
        f"- N64 root: `{report['n64_root']}`",
        f"- N64 functions indexed: {report['n64_function_count']}",
        f"- OOT3D functions scanned: {report['oot3d_function_count']}",
        f"- OOT3D functions with candidates: {len(report['matches'])}",
        f"- Domain filter: `{report['domain'] or 'none'}`",
        f"- Manual symbol overlay: `{report['symbol_overlay']}` ({report['manual_symbol_count']} symbols)",
        "",
        "## Workflow",
        "",
        "1. Pick the first high-scoring OOT3D row below.",
        "2. Open the OOT3D decompile and the N64 source side by side.",
        "3. Verify control flow and table/constant meaning, then promote only the symbol name with `scripts/promote_manual_symbol.py`.",
        "4. Re-run this ranker or `scripts\\refresh-n64-porting-plan.ps1`; manual-symbol overlay makes new names visible before the next Ghidra export.",
        "",
        "## Top Candidates",
        "",
    ]
    for row in report["matches"]:
        lines.extend(
            [
                f"### `{row['oot3d_entry']}` `{row['oot3d_name']}`",
                "",
                f"- OOT3D: `{row['oot3d_path']}` ({row['oot3d_lines']} lines)",
                "",
                "| Score | N64 function | N64 source | Evidence |",
                "| ---: | --- | --- | --- |",
            ]
        )
        for candidate in row["candidates"]:
            evidence = candidate["evidence"]
            bits = []
            for label in ("constants", "strings", "tokens", "call_tokens"):
                values = evidence[label]
                if values:
                    bits.append(f"{label}: " + ", ".join(f"`{value}`" for value in values[:6]))
            evidence_text = "<br>".join(bits) if bits else "-"
            lines.append(
                f"| {candidate['score']} | `{candidate['n64_name']}` | "
                f"`{candidate['n64_path']}` ({candidate['n64_lines']} lines) | {evidence_text} |"
            )
        lines.append("")
    return "\n".join(lines)


def write_csv(rows: list[dict[str, object]], out_csv: Path) -> None:
    out_csv.parent.mkdir(parents=True, exist_ok=True)
    with out_csv.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=[
                "oot3d_entry",
                "oot3d_name",
                "oot3d_path",
                "score",
                "n64_name",
                "n64_path",
                "evidence",
            ],
        )
        writer.writeheader()
        for row in rows:
            for candidate in row["candidates"]:
                evidence = candidate["evidence"]
                evidence_text = "; ".join(
                    f"{label}={','.join(values)}" for label, values in evidence.items() if values
                )
                writer.writerow(
                    {
                        "oot3d_entry": row["oot3d_entry"],
                        "oot3d_name": row["oot3d_name"],
                        "oot3d_path": row["oot3d_path"],
                        "score": candidate["score"],
                        "n64_name": candidate["n64_name"],
                        "n64_path": candidate["n64_path"],
                        "evidence": evidence_text,
                    }
                )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--n64-root", type=Path, default=DEFAULT_N64_ROOT)
    parser.add_argument("--source-glob", action="append", default=["src/**/*.c"])
    parser.add_argument("--domain", help="Regex filter applied to OOT3D and N64 function metadata/features")
    parser.add_argument("--include-named", action="store_true", help="Include already manually named OOT3D functions")
    parser.add_argument("--include-small-constants", action="store_true", help="Include constants below 0x100")
    parser.add_argument("--no-symbol-overlay", action="store_true", help="Use raw Ghidra names instead of manual-symbol overlay")
    parser.add_argument("--top-n64-per-oot3d", type=int, default=5)
    parser.add_argument("--max-oot3d", type=int, default=80)
    parser.add_argument("--min-score", type=float, default=24.0)
    parser.add_argument("--out-json", type=Path, default=ANALYSIS / "n64_port_candidate_rank.json")
    parser.add_argument("--out-md", type=Path, default=ANALYSIS / "n64_port_candidate_rank.md")
    parser.add_argument("--out-csv", type=Path, default=ANALYSIS / "n64_port_candidate_rank.csv")
    args = parser.parse_args()

    if not args.n64_root.is_dir():
        raise SystemExit(f"N64 OoT root not found: {args.n64_root}")

    n64_records = load_n64_functions(args.n64_root, args.source_glob, args.include_small_constants)
    use_symbol_overlay = not args.no_symbol_overlay
    symbols = load_manual_symbols(MANUAL_SYMBOLS)
    oot3d_records = load_oot3d_functions(
        include_named=args.include_named,
        include_small_constants=args.include_small_constants,
        use_symbol_overlay=use_symbol_overlay,
    )
    index = build_inverted_index(n64_records)
    matches = rank_candidates(
        oot3d_records,
        n64_records,
        index,
        top_n64_per_oot3d=args.top_n64_per_oot3d,
        max_oot3d=args.max_oot3d,
        min_score=args.min_score,
        domain=args.domain,
    )
    report = {
        "n64_root": rel(args.n64_root),
        "n64_function_count": len(n64_records),
        "oot3d_function_count": len(oot3d_records),
        "source_globs": args.source_glob,
        "domain": args.domain,
        "include_small_constants": args.include_small_constants,
        "symbol_overlay": use_symbol_overlay,
        "manual_symbol_count": len(symbols),
        "matches": matches,
    }
    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    args.out_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    args.out_md.write_text(make_markdown(report), encoding="utf-8")
    write_csv(matches, args.out_csv)
    print(
        f"indexed {len(n64_records)} N64 functions; "
        f"ranked {len(matches)} OOT3D functions with candidates"
    )
    print(f"wrote {args.out_json}")
    print(f"wrote {args.out_md}")
    print(f"wrote {args.out_csv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
