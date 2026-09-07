#!/usr/bin/env python3
"""Audit DAT_* references against native OOT3D code.bin words."""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CODE_BASE = 0x00100000
DEFAULT_CODE_BIN_CANDIDATES = [
    ROOT / "work" / "extract" / "exefs" / "code.bin",
    ROOT.parent / "work" / "extract" / "exefs" / "code.bin",
    Path(r"E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin"),
]
DAT_RE = re.compile(r"\bDAT_([0-9A-Fa-f]{8})\b")


def resolve_code_bin(path: Path | None) -> Path:
    if path is not None:
        return path
    for candidate in DEFAULT_CODE_BIN_CANDIDATES:
        if candidate.exists():
            return candidate
    searched = ", ".join(str(candidate) for candidate in DEFAULT_CODE_BIN_CANDIDATES)
    raise SystemExit(f"code.bin not found; pass --code-bin. Searched: {searched}")


def read_functions(path: Path) -> dict[int, str]:
    functions = read_function_csv(path)
    for extra in (
        ROOT / "symbols" / "manual_symbols.csv",
        ROOT / "analysis" / "c_conversion_readiness.csv",
        ROOT / "analysis" / "convertible_ports.csv",
        ROOT / "analysis" / "native_source_decompilation_queue.csv",
    ):
        functions.update(read_function_csv(extra))
    return functions


def read_function_csv(path: Path) -> dict[int, str]:
    if not path.exists() and path.name == "functions.csv":
        selected = path.with_name("functions_selected.csv")
        if selected.exists():
            path = selected
    if not path.exists():
        return {}
    functions: dict[int, str] = {}
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            if row.get("kind") and row.get("kind") != "function":
                continue
            entry = row.get("entry") or row.get("oot3d_entry") or row.get("address") or ""
            name = row.get("new_name") or row.get("oot3d_name") or row.get("name") or row.get("function") or ""
            if not entry or not name:
                continue
            try:
                functions[int(entry, 16)] = name
            except ValueError:
                continue
    return functions


def iter_source_files(paths: list[Path]) -> list[Path]:
    files: list[Path] = []
    for path in paths:
        if path.is_file():
            files.append(path)
            continue
        for pattern in ("*.c", "*.h"):
            files.extend(path.rglob(pattern))
    return sorted(set(files))


def collect_symbols(files: list[Path]) -> dict[str, set[str]]:
    refs: dict[str, set[str]] = {}
    for path in files:
        text = path.read_text(encoding="utf-8", errors="replace")
        rel = path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else str(path)
        for match in DAT_RE.finditer(text):
            refs.setdefault(match.group(0), set()).add(rel)
    return refs


def read_word(image: bytes, code_base: int, address: int) -> int | None:
    offset = address - code_base
    if offset < 0 or offset + 4 > len(image):
        return None
    return struct.unpack_from("<I", image, offset)[0]


def classify_word(
    word: int | None,
    code_base: int,
    code_size: int,
    runtime_end: int,
    functions: dict[int, str],
) -> tuple[str, str]:
    if word is None:
        return "outside-code-bin", ""
    if word == 0:
        return "zero", ""
    if word in functions:
        return "function-pointer", functions[word]
    if code_base <= word < code_base + code_size:
        return "code-bin-address", ""
    if code_base <= word < runtime_end:
        return "runtime-address", ""
    float_value = struct.unpack("<f", struct.pack("<I", word))[0]
    if math.isfinite(float_value) and abs(float_value) >= 0.000001 and abs(float_value) < 1000000.0:
        high = (word >> 24) & 0xFF
        if 0x30 <= high <= 0x4F:
            return "float32-literal", f"{float_value:.9g}"
    if word < 0x10000:
        return "small-literal", str(word)
    return "unknown-word", ""


def format_hex(value: int | None) -> str:
    return "" if value is None else f"0x{value:08X}"


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = ["symbol", "address", "word", "classification", "target_name", "sources"]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: row.get(key, "") for key in fieldnames})


def write_md(path: Path, rows: list[dict[str, object]], code_bin: Path, code_base: int, runtime_end: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# Native Literal Pool Audit",
        "",
        "This report scans maintained OOT3D source files for `DAT_XXXXXXXX` references and reads the matching word from native `code.bin`.",
        "It is static OOT3D evidence for source/data interpretation; N64 source is not used.",
        "",
        f"- code.bin: `{code_bin}`",
        f"- code base: `0x{code_base:08X}`",
        f"- runtime address ceiling: `0x{runtime_end:08X}`",
        f"- symbols: {len(rows)}",
        "",
        "| Symbol | Address | Word | Classification | Target | Sources |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for row in rows:
        lines.append(
            f"| `{row['symbol']}` | `{row['address']}` | `{row['word']}` | "
            f"`{row['classification']}` | `{row['target_name']}` | {row['sources']} |"
        )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--code-bin", type=Path)
    parser.add_argument("--code-base", type=lambda value: int(value, 0), default=DEFAULT_CODE_BASE)
    parser.add_argument("--runtime-end", type=lambda value: int(value, 0), default=0x00600000)
    parser.add_argument("--functions", type=Path, default=ROOT / "ghidra_export" / "functions.csv")
    parser.add_argument("--source", type=Path, action="append", default=[ROOT / "src"])
    parser.add_argument("--out-json", type=Path, default=ROOT / "analysis" / "native_literal_pool_audit.json")
    parser.add_argument("--out-csv", type=Path, default=ROOT / "analysis" / "native_literal_pool_audit.csv")
    parser.add_argument("--out-md", type=Path, default=ROOT / "analysis" / "native_literal_pool_audit.md")
    args = parser.parse_args()

    code_bin = resolve_code_bin(args.code_bin)
    image = code_bin.read_bytes()
    functions = read_functions(args.functions)
    refs = collect_symbols(iter_source_files(args.source))

    rows: list[dict[str, object]] = []
    for symbol in sorted(refs, key=lambda item: int(item[4:], 16)):
        address = int(symbol[4:], 16)
        word = read_word(image, args.code_base, address)
        classification, target_name = classify_word(word, args.code_base, len(image), args.runtime_end, functions)
        rows.append(
            {
                "symbol": symbol,
                "address": f"0x{address:08X}",
                "word": format_hex(word),
                "classification": classification,
                "target_name": target_name,
                "sources": "<br>".join(sorted(refs[symbol])),
            }
        )

    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    args.out_json.write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
    write_csv(args.out_csv, rows)
    write_md(args.out_md, rows, code_bin, args.code_base, args.runtime_end)
    print(f"audited {len(rows)} DAT symbols")
    print(args.out_md)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
