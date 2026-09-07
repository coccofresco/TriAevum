#!/usr/bin/env python3
"""Classify target data declarations from Clang AST, without regex rewriting."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from collections import Counter, defaultdict
import json
from pathlib import Path
import re
import subprocess


TARGET_DATA_RE = re.compile(r"^DAT_([0-9A-Fa-f]{8})$")
SAFE_SCALARS = {
    "_Bool",
    "char",
    "signed char",
    "unsigned char",
    "short",
    "unsigned short",
    "int",
    "unsigned int",
    "long",
    "unsigned long",
    "long long",
    "unsigned long long",
    "float",
    "double",
}


def parse_concatenated_json(payload: str) -> list[dict]:
    decoder = json.JSONDecoder()
    offset = 0
    documents = []
    while offset < len(payload):
        while offset < len(payload) and payload[offset].isspace():
            offset += 1
        if offset >= len(payload):
            break
        document, offset = decoder.raw_decode(payload, offset)
        documents.append(document)
    return documents


def classify_type(qualified: str, desugared: str) -> str:
    candidate = desugared or qualified
    candidate = re.sub(r"\b(const|volatile|restrict)\b", "", candidate)
    candidate = " ".join(candidate.split())
    if "*" in candidate:
        return "pointer_host_width"
    if "uintptr_t" in qualified or "intptr_t" in qualified:
        return "target_word_host_width"
    array_match = re.fullmatch(r"(.+?)\s*\[[^]]*\]", candidate)
    if array_match:
        element = " ".join(array_match.group(1).split())
        return "absolute_array" if element in SAFE_SCALARS else "aggregate_array"
    if candidate in SAFE_SCALARS:
        return "absolute_scalar"
    if candidate.startswith("enum "):
        return "absolute_scalar"
    return "aggregate_or_unknown"


def inspect_source(clang: Path, include_root: Path, source_root: Path,
                   relative: str) -> tuple[str, list[dict], str | None]:
    source = source_root / relative
    command = [
        str(clang),
        "--target=x86_64-w64-windows-gnu",
        "-std=gnu11",
        "-w",
        "-Wno-error=incompatible-pointer-types",
        "-Wno-error=int-conversion",
        "-I",
        str(include_root),
        "-Xclang",
        "-ast-dump=json",
        "-Xclang",
        "-ast-dump-filter=DAT_",
        "-fsyntax-only",
        str(source),
    ]
    result = subprocess.run(command, capture_output=True, text=True, errors="replace")
    if result.returncode != 0:
        return relative, [], result.stderr[-4000:]
    records = []
    for node in parse_concatenated_json(result.stdout):
        name = node.get("name", "")
        match = TARGET_DATA_RE.match(name)
        if node.get("kind") != "VarDecl" or not match:
            continue
        type_info = node.get("type", {})
        qualified = type_info.get("qualType", "")
        desugared = type_info.get("desugaredQualType", qualified)
        records.append({
            "name": name,
            "address": int(match.group(1), 16),
            "qualified_type": qualified,
            "desugared_type": desugared,
            "type_class": classify_type(qualified, desugared),
            "source": relative,
            "line": node.get("loc", {}).get("line"),
            "name_offset": node.get("loc", {}).get("offset"),
            "declaration_offset": node.get("range", {}).get("begin", {}).get("offset"),
            "storage_class": node.get("storageClass"),
        })
    return relative, records, None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--clang", required=True, type=Path)
    parser.add_argument("--snapshot-root", required=True, type=Path)
    parser.add_argument("--snapshot-manifest", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--assembly-output", required=True, type=Path)
    parser.add_argument("--lowered-assembly-output", required=True, type=Path)
    parser.add_argument("--surface-contracts", required=True, type=Path)
    parser.add_argument("--jobs", type=int, default=12)
    args = parser.parse_args()

    snapshot = json.loads(args.snapshot_manifest.read_text(encoding="utf-8"))
    surface = json.loads(args.surface_contracts.read_text(encoding="utf-8"))
    external_data = {
        record["name"]
        for record in surface["external_symbols"]
        if record["category"] == "target_data_address"
    }
    sources = [record["path"] for record in snapshot["canonical_sources"]]
    declarations = []
    failures = []
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as executor:
        futures = [
            executor.submit(
                inspect_source,
                args.clang.resolve(),
                (args.snapshot_root / "include").resolve(),
                args.snapshot_root.resolve(),
                source,
            )
            for source in sources
        ]
        for future in as_completed(futures):
            relative, records, error = future.result()
            declarations.extend(records)
            if error:
                failures.append({"source": relative, "error": error})

    by_symbol: dict[str, list[dict]] = defaultdict(list)
    for declaration in declarations:
        by_symbol[declaration["name"]].append(declaration)

    symbols = []
    class_counts = Counter()
    safe_symbols = []
    lowered_safe_symbols = []
    external_class_counts = Counter()
    for name in sorted(by_symbol):
        records = by_symbol[name]
        classes = sorted({record["type_class"] for record in records})
        types = sorted({record["desugared_type"] for record in records})
        if len(classes) > 1 or len(types) > 1:
            disposition = "conflicting_declarations"
        elif classes[0] in ("absolute_scalar", "absolute_array"):
            disposition = "absolute_symbol"
        else:
            disposition = classes[0]
        class_counts[disposition] += 1
        if name in external_data:
            external_class_counts[disposition] += 1
            if disposition == "absolute_symbol":
                safe_symbols.append((name, records[0]["address"]))
                lowered_safe_symbols.append((name, records[0]["address"]))
            elif disposition == "target_word_host_width":
                lowered_safe_symbols.append((name, records[0]["address"]))
        symbols.append({
            "name": name,
            "address": records[0]["address"],
            "disposition": disposition,
            "types": types,
            "declarations": records,
        })

    report = {
        "schema_version": 1,
        "decomp_revision": snapshot["decomp_revision"],
        "source_count": len(sources),
        "parsed_source_count": len(sources) - len(failures),
        "failed_source_count": len(failures),
        "declaration_count": len(declarations),
        "symbol_count": len(symbols),
        "disposition_counts": dict(sorted(class_counts.items())),
        "external_disposition_counts": dict(sorted(external_class_counts.items())),
        "failures": sorted(failures, key=lambda item: item["source"]),
        "symbols": symbols,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n"
    )

    assembly = [
        "# Generated from Clang AST declarations; do not edit.",
        ".text",
    ]
    for name, address in safe_symbols:
        assembly.extend([f".globl {name}", f".set {name}, 0x{address:08x}"])
    args.assembly_output.parent.mkdir(parents=True, exist_ok=True)
    args.assembly_output.write_text(
        "\n".join(assembly) + "\n", encoding="utf-8", newline="\n"
    )
    lowered_assembly = [
        "# Generated after target-word declaration lowering; do not edit.",
        ".text",
    ]
    for name, address in lowered_safe_symbols:
        lowered_assembly.extend(
            [f".globl {name}", f".set {name}, 0x{address:08x}"]
        )
    args.lowered_assembly_output.parent.mkdir(parents=True, exist_ok=True)
    args.lowered_assembly_output.write_text(
        "\n".join(lowered_assembly) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    print(json.dumps({
        "parsed_source_count": report["parsed_source_count"],
        "failed_source_count": report["failed_source_count"],
        "symbol_count": report["symbol_count"],
        "disposition_counts": report["disposition_counts"],
        "external_disposition_counts": report["external_disposition_counts"],
        "output": str(args.output.resolve()),
        "assembly_output": str(args.assembly_output.resolve()),
        "lowered_assembly_output": str(args.lowered_assembly_output.resolve()),
    }, indent=2))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
