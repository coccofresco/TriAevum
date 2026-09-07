#!/usr/bin/env python3
"""Inventory external contracts of the complete source-native archive."""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re
import subprocess


DATA_ADDRESS_RE = re.compile(
    r"^(?:(?:DAT|LAB|UNK)_[0-9A-Fa-f]+|"
    r"PTR_.+_[0-9A-Fa-f]+|[su]_.+_[0-9A-Fa-f]+|iRam[0-9A-Fa-f]+)$"
)
COMPILER_RUNTIME_RE = re.compile(
    r"^(?:__aeabi_|__gnu_|___chkstk|__chkstk|_?alloca$|_?fltused$)"
)
LIBC_SYMBOLS = {
    "abort", "calloc", "cos", "cosf", "free", "malloc", "memchr", "memcmp",
    "memcpy", "memmove", "memset", "pow", "powf", "realloc", "sin", "sinf",
    "snprintf", "sqrt", "sqrtf", "strlen", "strncpy", "vsnprintf",
}


def classify(symbol: str) -> str:
    undecorated = symbol[1:] if symbol.startswith("_") else symbol
    if DATA_ADDRESS_RE.match(undecorated):
        return "target_data_address"
    if undecorated.startswith("FUN_"):
        return "target_function_address"
    if undecorated.startswith("gOot3d") or re.match(r"^g[A-Z]", undecorated):
        return "source_global"
    if COMPILER_RUNTIME_RE.match(symbol):
        return "compiler_runtime"
    if undecorated.startswith("oot3d_host_"):
        return "host_platform_hook"
    if undecorated == "getThreadCommandBuffer":
        return "ctr_platform"
    if undecorated in LIBC_SYMBOLS:
        return "host_c_runtime"
    if undecorated.startswith(("nn", "svc", "GX", "GSP", "DSP", "FS_", "os")):
        return "ctr_platform"
    return "source_or_platform_function"


def read_symbols(llvm_nm: Path, archive: Path, mode: str) -> tuple[set[str], dict[str, set[str]]]:
    result = subprocess.run(
        [str(llvm_nm), mode, "--format=posix", str(archive)],
        check=True,
        capture_output=True,
        text=True,
        errors="replace",
    )
    symbols: set[str] = set()
    owners: dict[str, set[str]] = defaultdict(set)
    current_object = ""
    for raw_line in result.stdout.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        if line.endswith(":"):
            current_object = line[:-1]
            continue
        fields = line.split()
        if len(fields) < 2 or len(fields[1]) != 1:
            continue
        symbol = fields[0]
        symbols.add(symbol)
        if current_object:
            owners[symbol].add(current_object)
    return symbols, owners


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--llvm-nm", required=True, type=Path)
    parser.add_argument("--archive", required=True, type=Path)
    parser.add_argument("--snapshot-manifest", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--process-manifest", type=Path)
    args = parser.parse_args()

    undefined, owners = read_symbols(args.llvm_nm.resolve(), args.archive.resolve(), "--undefined-only")
    defined, _ = read_symbols(args.llvm_nm.resolve(), args.archive.resolve(), "--defined-only")
    external = sorted(undefined - defined)
    categories = Counter(classify(symbol) for symbol in external)
    snapshot = json.loads(args.snapshot_manifest.read_text(encoding="utf-8"))

    report = {
        "schema_version": 1,
        "decomp_revision": snapshot["decomp_revision"],
        "archive": str(args.archive.resolve()),
        "canonical_source_count": len(snapshot["canonical_sources"]),
        "undefined_symbol_count": len(undefined),
        "defined_symbol_count": len(defined),
        "external_symbol_count": len(external),
        "category_counts": dict(sorted(categories.items())),
        "external_symbols": [
            {
                "name": symbol,
                "category": classify(symbol),
                "referenced_by": sorted(owners.get(symbol, ())),
            }
            for symbol in external
        ],
    }
    if args.process_manifest:
        process_manifest = json.loads(
            args.process_manifest.read_text(encoding="utf-8")
        )
        process = process_manifest["process"]
        ranges = [
            (item["address"], item["address"] + item["mapped_size"], item["name"])
            for item in [*process["segments"], *process["system_regions"]]
        ]
        for name in ("heap", "linear_heap"):
            item = process.get(name)
            if item:
                ranges.append((
                    item["base_address"], item["base_address"] + item["size"], name
                ))
        mapped = []
        unmapped = []
        for symbol in external:
            if classify(symbol) != "target_data_address":
                continue
            match = re.search(r"([0-9A-Fa-f]+)$", symbol)
            address = int(match.group(1), 16)
            region = next((name for start, end, name in ranges if start <= address < end), None)
            record = {"name": symbol, "address": address}
            if region is None:
                unmapped.append(record)
            else:
                record["region"] = region
                mapped.append(record)
        report["target_data_coverage"] = {
            "mapped_count": len(mapped),
            "unmapped_count": len(unmapped),
            "unmapped_symbols": unmapped,
        }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n"
    )
    print(json.dumps({
        "external_symbol_count": len(external),
        "category_counts": report["category_counts"],
        "target_data_coverage": report.get("target_data_coverage"),
        "output": str(args.output.resolve()),
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
